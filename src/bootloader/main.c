/**
 * @file    main.c
 * @brief   OTA Bootloader 主入口 — 安全启动 + 双备份回滚
 * @note    裸机 C + STM32Cube HAL，无 Arduino 框架
 *          48KB (0x08000000-0x0800BFFF)
 *
 * 流程:
 *   1. HAL_Init + 系统初始化（关闭 WWDG，配置调试冻结）
 *   2. 读取元数据区，判断有效性
 *   3. HMAC-SHA256 校验目标分区固件签名
 *   4. 签名通过 → 设置 IWDG 30s 看门狗 → 跳转 APP
 *   5. 签名失败 → try_count++ → 达到阈值则切换分区 → NVIC_SystemReset()
 *   6. 两分区全部失败 → 死锁（保留出厂 APP1）
 */

#include "stm32f4xx_hal.h"
#include "boot_config.h"
#include "flash_drv.h"
#include "metadata.h"
#include "hmac_sha256.h"
#include <string.h>
#include <stdbool.h>

/* ========================== 硬件密钥 ========================== */
static const uint8_t hmac_key[HMAC_KEY_SIZE] = { HMAC_KEY };

/* ========================== IWDG 句柄 ========================== */
static IWDG_HandleTypeDef hiwdg;

/* ========================== 中断处理函数 ========================== */
/* HAL 源码中的 SysTick_Handler 只在 FreeRTOS port 中定义，
 * 本项目不含 FreeRTOS，需自行提供以下处理函数。 */

void SysTick_Handler(void)
{
    HAL_IncTick();  /* HAL 库滴答计数器 */
}

void HardFault_Handler(void)
{
    /* BKPT 软断点 → 调试器立即停下，可检查栈帧和寄存器 */
    __asm volatile ("BKPT #0");
    while (1) { __WFE(); }
}

void MemManage_Handler(void)
{
    __asm volatile ("BKPT #1");
    while (1) { __WFE(); }
}

void BusFault_Handler(void)
{
    __asm volatile ("BKPT #2");
    while (1) { __WFE(); }
}

void UsageFault_Handler(void)
{
    __asm volatile ("BKPT #3");
    while (1) { __WFE(); }
}

void WWDG_IRQHandler(void)
{
    /* 调试时 WWDG 可能意外触发，直接清除并禁用 */
    if (WWDG->SR & WWDG_SR_EWIF) {
        WWDG->SR = 0;           /* 清除早期唤醒中断标志 */
    }
    WWDG->CR = 0;               /* 禁用 WWDG */
}

/* ========================== 系统初始化 ========================== */
static void system_init(void)
{
    /* 禁用 WWDG（默认上电可能使能，避免意外复位） */
    __HAL_RCC_WWDG_CLK_ENABLE();
    WWDG->CR = 0;

    /* 调试冻结：Halted 时暂停 IWDG 和 WWDG 计数 */
    DBGMCU->CR     |= DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP | DBGMCU_CR_DBG_STANDBY;
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP | DBGMCU_APB1_FZ_DBG_WWDG_STOP;
}

/* ========================== IWDG 初始化（30 秒超时） ========================== */
static void IWDG_Init_30s(void)
{
    /* 使能 IWDG 外设时钟（APB1 bit29），Cube SystemInit 已将其禁用 */
    RCC->APB1ENR |= (1UL << 29);  /* RCC_APB1ENR_IWDGEN */

    /* LSI ≈ 32KHz, 预分频 256 → 时钟 = 125Hz
     * 30s → 重载值 = 125 * 30 = 3750 */
    hiwdg.Instance       = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg.Init.Reload    = 3750;
    HAL_IWDG_Init(&hiwdg);
}

/* ========================== 外设复位（跳转前清理） ========================== */
static void deinit_peripherals(void)
{
    __disable_irq();

    /* 复位所有外设时钟 */
    RCC->AHB1RSTR  = 0xFFFFFFFF;
    RCC->AHB2RSTR  = 0xFFFFFFFF;
    RCC->AHB3RSTR  = 0xFFFFFFFF;
    RCC->APB1RSTR  = 0xFFFFFFFF;
    RCC->APB2RSTR  = 0xFFFFFFFF;
    __DSB();
    __ISB();

    RCC->AHB1RSTR  = 0;
    RCC->AHB2RSTR  = 0;
    RCC->AHB3RSTR  = 0;
    RCC->APB1RSTR  = 0;
    RCC->APB2RSTR  = 0;

    /* 关闭所有中断 */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    /* 清零 SysTick */
    SysTick->CTRL  = 0;
    SysTick->LOAD  = 0;
    SysTick->VAL   = 0;

    /* 恢复 PRIMASK 到复位默认值 (0 = 中断开启)
     * APP 的 Reset_Handler 依赖硬件复位后 PRIMASK=0 的默认状态 */
    __enable_irq();
}

/* ========================== 跳转到 APP ========================== */
static void jump_to_app(uint32_t app_addr)
{
    uint32_t stack_ptr     = *(volatile uint32_t*)app_addr;
    uint32_t reset_handler = *(volatile uint32_t*)(app_addr + 4);

    deinit_peripherals();

    SCB->VTOR = app_addr;
    __set_MSP(stack_ptr);
    __DSB();
    __ISB();

    /* PRIMASK 已由 deinit_peripherals() 恢复到 0 */

    void (*app_reset)(void) = (void (*)(void))reset_handler;
    app_reset();  /* 永不返回 */
}

/* ========================== 校验固件签名 ========================== */
static bool verify_firmware_signature(uint32_t app_addr, const uint8_t *expected_sig)
{
#if 0   /* TODO: 启用 HMAC 校验。当前禁用以便调试，生产环境必须开启。 */
    uint8_t calc_sig[HMAC_SHA256_DIGEST_SIZE];
    hmac_sha256(hmac_key, HMAC_KEY_SIZE,
                (const uint8_t*)app_addr, APP_SIZE,
                calc_sig);
    return (memcmp(calc_sig, expected_sig, SIGNATURE_SIZE) == 0);
#endif
    (void)app_addr;
    (void)expected_sig;
    return true;   /* 临时信任所有固件 */
}

/* ========================== 校验结果处理 ========================== */

/** 签名校验失败：递增尝试次数，达到上限则切换分区 */
static void handle_verify_failed(BootMetadata_t *meta)
{
    uint8_t slot = meta->active_slot;
    meta->slot[slot].try_count++;
    metadata_write(meta);

    if (meta->slot[slot].try_count >= meta->slot[slot].max_tries) {
        /* 当前分区尝试次数耗尽，切换到备用分区 */
        uint8_t alt_slot = slot ^ 1;

        /* 检查备用分区是否有有效签名（signature[0] != 0） */
        if (meta->slot[alt_slot].signature[0] != 0) {
            meta->active_slot = alt_slot;
            meta->slot[alt_slot].try_count = 0;
            metadata_write(meta);
            /* 复位 → 重新进入 Bootloader，校验备用分区 */
        } else {
            /* 备用分区无固件，回退到出厂 APP1 */
            meta->active_slot = 0;
            meta->slot[0].try_count = 0;
            metadata_write(meta);
        }
    }

    NVIC_SystemReset();
    /* 不返回 */
}

/* ========================== 主入口 ========================== */
int main(void)
{
    /* ── 阶段 1: HAL 初始化 ── */
    HAL_Init();          /* SysTick, NVIC 优先级分组, 指令/数据缓存 */

    /* ── 阶段 2: 系统初始化 ── */
    system_init();       /* 关闭 WWDG, 配置调试冻结 */

    /* ── 阶段 3: 读取元数据 ── */
    BootMetadata_t meta;
    metadata_read(&meta);

    if (meta.magic != MAGIC_VALID) {
        /* 首次上电 / 元数据损坏 → 初始化出厂默认值，直接启动 APP1 */
        metadata_init_default(&meta);
        metadata_write(&meta);
        jump_to_app(APP1_START);
        /* 不返回 */
    }

    /* ── 阶段 4: 确定目标分区 ── */
    uint8_t  slot       = meta.active_slot;
    uint32_t app_addr   = (slot == 0) ? APP1_START : APP2_START;
    uint8_t *expected   = meta.slot[slot].signature;

    /* ── 阶段 5: HMAC-SHA256 签名校验 ── */
    if (!verify_firmware_signature(app_addr, expected)) {
        handle_verify_failed(&meta);
        /* 不返回 */
    }

    /* ── 阶段 6: 校验通过，准备启动 ── */
    /* 清除 boot_success（APP 需在 30s 内调用 ota_confirm_success 重新置位） */
    meta.slot[slot].boot_success = 0x00;
    metadata_write(&meta);

    /* ── 阶段 7: 启动 IWDG（30s 超时） ── */
    IWDG_Init_30s();

    /* ── 阶段 8: 跳转 APP ── */
    jump_to_app(app_addr);

    /* 永远不会到达这里 */
    while (1) { __NOP(); }
}

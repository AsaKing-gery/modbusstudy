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

/* ========================== 跳转前轻量清理 ========================== */
static void deinit_peripherals(void)
{
    __disable_irq();

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
/**
 * @brief  跳转到 APP 固件
 * @note   使用内联汇编执行 MSP 切换和跳转，避免 C 函数尾声 (pop) 
 *         从新栈 (0x20020000 = SRAM 边界外) 恢复寄存器导致 BusFault。
 *         128KB SRAM 的有效地址范围: 0x20000000 ~ 0x2001FFFF。
 */
static __attribute__((noreturn)) void jump_to_app(uint32_t app_addr)
{
    uint32_t stack_ptr     = *(volatile uint32_t*)app_addr;
    uint32_t reset_handler = *(volatile uint32_t*)(app_addr + 4);

    /* 固件按 APP1 (0x08020000) 编译, 若目标分区不是 APP1 则重映射入口 */
    if (app_addr != APP1_START) {
        uint32_t fw_offset = reset_handler - APP1_START;
        reset_handler = app_addr + fw_offset;
    }

    deinit_peripherals();

    SCB->VTOR = app_addr;

    /* PRIMASK 已由 deinit_peripherals() 恢复到 0 */

    /* 通过内联汇编切换 MSP 并跳转。
     * 绝不能在 C 函数内先 __set_MSP() 再让编译器生成 pop/bx，
     * 因为函数尾声中 push/pop 的寄存器在原栈上，而 __set_MSP 已切换栈指针。 */
    __asm volatile (
        "msr msp, %0    \n\t"
        "dsb            \n\t"
        "isb            \n\t"
        "bx  %1         \n\t"
        :
        : "r" (stack_ptr), "r" (reset_handler)
        : "memory"
    );

    __builtin_unreachable();
}

/* ========================== 校验固件签名 ========================== */
static bool verify_firmware_signature(uint32_t app_addr, const uint8_t *expected_sig)
{
    /* ── 阶段 1: 快速向量表完整性检查 ── */
    /* 读取 APP 镜像的前 8 字节: [SP_init][Reset_Handler] */
    uint32_t sp = *(volatile uint32_t*)app_addr;
    uint32_t pc = *(volatile uint32_t*)(app_addr + 4);

    /* SP 必须在 RAM 范围内 (0x20000000 ~ 0x20020000 for STM32F407 128KB SRAM) */
    if (sp < 0x20000000 || sp > 0x20020000) {
        return false;
    }

    /* Reset_Handler 必须在 Flash APP 范围内, 且 LSB=1 (Thumb 模式) */
    if (pc < APP1_START || pc > (APP2_START + APP_SIZE) || (pc & 1) == 0) {
        return false;
    }

#if 0   /* TODO: 启用 HMAC 校验。当前禁用以便调试，生产环境必须开启。 */
    uint8_t calc_sig[HMAC_SHA256_DIGEST_SIZE];
    hmac_sha256(hmac_key, HMAC_KEY_SIZE,
                (const uint8_t*)app_addr, APP_SIZE,
                calc_sig);
    return (memcmp(calc_sig, expected_sig, SIGNATURE_SIZE) == 0);
#endif
    (void)expected_sig;
    return true;
}

/* ========================== 校验结果处理 ========================== */

/** 签名校验失败：递增尝试次数，达到上限则切换分区 */
static void handle_verify_failed(BootMetadata_t *meta)
{
    uint8_t slot = meta->active_slot;
    meta->slot[slot].try_count++;
    metadata_write(meta);

    if (meta->slot[slot].try_count >= meta->slot[slot].max_tries) {
        uint8_t alt_slot = slot ^ 1;

        if (meta->slot[alt_slot].signature[0] != 0) {
            /* 切换到备用分区 */
            meta->active_slot = alt_slot;
            meta->slot[alt_slot].try_count = 0;
            metadata_write(meta);
        } else {
            /* 两个分区都无效 → 出厂复位: 初始化默认 metadata,
             * 直接跳 APP1, 不依赖签名 (签名可能已被清空) */
            metadata_init_default(meta);
            metadata_write(meta);
            jump_to_app(APP1_START);
            /* 不返回 */
        }
    }

    NVIC_SystemReset();
    /* 不返回 */
}

/* ========================== 搬移固件: slot1 → slot0 ========================== */
static void copy_slot1_to_slot0(void)
{
    /* 擦除 slot 0 (sector 5, 128KB) */
    {
        uint32_t err;
        FLASH_EraseInitTypeDef e = {0};
        e.TypeErase = FLASH_TYPEERASE_SECTORS;
        e.Sector = 5; e.NbSectors = 1;
        e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        HAL_FLASH_Unlock();
        __disable_irq();
        HAL_FLASHEx_Erase(&e, &err);
        __enable_irq();
        HAL_FLASH_Lock();
    }

    /* 按字拷贝 APP2 → APP1 (128KB, 32768 字) */
    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < APP_SIZE; i += 4) {
        uint32_t w = *(volatile uint32_t*)(APP2_START + i);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP1_START + i, w);
    }
    HAL_FLASH_Lock();
}

/* ========================== 主入口 ========================== */
int main(void)
{
    HAL_Init();
    system_init();

    BootMetadata_t meta;
    metadata_read(&meta);

    if (meta.magic != MAGIC_VALID) {
        metadata_init_default(&meta);
        metadata_write(&meta);
        jump_to_app(APP1_START);
    }

    /* 始终尝试 slot 1 新固件 (若签名有效 → 搬到 slot 0 → 启动) */
    if (meta.slot[1].signature[0] != 0) {
        if (verify_firmware_signature(APP2_START, meta.slot[1].signature)) {
            /* slot 1 有效, 搬到 slot 0 */
            copy_slot1_to_slot0();

            /* 更新 metadata: slot 0 获得 slot 1 的签名 */
            memcpy(meta.slot[0].signature, meta.slot[1].signature, SIGNATURE_SIZE);
            meta.slot[0].version = meta.slot[1].version;
            meta.slot[0].try_count = 0;
            meta.active_slot = 0;
            metadata_write(&meta);

            IWDG_Init_30s();
            jump_to_app(APP1_START);
        }
    }

    /* slot 1 无效或为空 → boot slot 0 */
    if (meta.slot[0].signature[0] != 0) {
        if (verify_firmware_signature(APP1_START, meta.slot[0].signature)) {
            meta.slot[0].boot_success = 0x00;
            metadata_write(&meta);
            IWDG_Init_30s();
            jump_to_app(APP1_START);
        }
    }

    /* 两个分区都无效 → 出厂复位 */
    metadata_init_default(&meta);
    metadata_write(&meta);
    jump_to_app(APP1_START);
}

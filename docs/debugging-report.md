# Bootloader → APP 启动失败排查报告

**日期**: 2026-07-14
**平台**: STM32F407VET6 / PlatformIO
**问题**: Bootloader 成功跳转到 APP 后，APP 不输出串口日志，"卡死"

---

## 一、现象

GDB 调试时，Bootloader 从 `app_reset()` 跳入 APP 后，PC 停在 0x0802a6a0 ~ 0x0802a6ac 附近，串口无输出：

```
Breakpoint 8, main () at src\bootloader\main.c:186
186          app_reset();
Info : halted: PC: 0x0802adf4        ← APP Reset_Handler 入口（正确）
halted: PC: 0x0802adf4

Program received signal SIGINT, Interrupt.
0x0802a6a8 in ?? ()                  ← "卡住"
```

每次中断后 PC 值略有不同（0x0802a6a0, 0x0802a6a6, 0x0802a6a8, 0x0802a6aa, 0x0802a6ac），表面看起来像是"随机崩溃"。

---

## 二、排查路径

### 2.1 链接脚本修复（第 1 个问题）

**排查方向**：怀疑 APP 的向量表和代码段地址不正确。

检查 `platformio.ini`：
```ini
board_build.offset = 0x20000
```

使用 `arm-none-eabi-objdump -h firmware.elf` 验证：
```
.isr_vector   08020000  08020000    ← 看起来正确
.text         08020190  08020190    ← 看起来正确
```

但用 `readelf` 检查入口地址：
```
Entry point address:  0x0800ADF5    ← 错误！应为 0x0802ADF5
```

**根因**：Arduino STM32 框架的 `board_build.offset` 变量的值是通过 `build.flash_offset` 传递的，而该变量在 board 定义中默认为 0。`-Wl,--defsym=LD_FLASH_OFFSET=0x20000` 也无法覆盖框架内部已定义的值。

**修复**：创建自定义链接脚本 `src/bootloader/app_offset.ld`，硬编码：

```ld
MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08020000, LENGTH = 384K
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}
```

验证通过：
```
.isr_vector   08020000  08020000   ✓
.text         08020190  08020190   ✓
Entry point:  0x0802ADF5           ✓
```

### 2.2 PRIMASK 中断屏蔽（第 2 个问题 — 根因）

链接脚本修复后仍然"卡死"，且所有中断位置（0x0802a6a0 ~ 0x0802a6ac）非常接近。用 objdump 反汇编定位：

```
802a6a0:  ldrh.w  r2, [r4, #262]   ← 读 tx_head
802a6a4:  uxth    r3, r3
802a6a6:  cmp     r2, r3           ← 比较 tx_head vs tx_tail
802a6a8:  beq.n   802a6c0          ← 相等则退出（flush 完成）
802a6aa:  cmp     r5, #0           ← 超时检查
802a6ac:  beq.n   802a6a0          ← timeout=0 → 无限循环！
```

全部在 `HardwareSerial::flush()` 内 — 不是随机崩溃，是**一个死循环**！

`flush()` 在等 TX 中断（TXE）处理完发送缓冲区，但中断永远不来。回看 Bootloader 代码：

```c
// src/bootloader/main.c - 极简版
__disable_irq();     // ← PRIMASK = 1 !!
{
    uint32_t sp = ...;
    uint32_t pc = ...;
    SCB->VTOR = target_addr;
    __set_MSP(sp);
    void (*app_reset)(void) = (void (*)(void))pc;
    app_reset();     // BLX 跳转，PRIMASK 仍然是 1
}
```

| 场景 | PRIMASK | USART2 TX 中断 | 结果 |
|---|---|---|---|
| 硬件复位 → APP | **0**（硬件默认） | 正常触发 | flush() 正常退出 |
| `__disable_irq()` → APP | **1**（中断屏蔽） | **无法触发** | flush() 死循环 |

Cortex-M4 硬件复位后 PRIMASK = 0。`app_reset()` 是 BLX 函数调用，不会恢复 PRIMASK。APP 的 `Reset_Handler` 预期 PRIMASK 已是 0（正常复位行为），不主动清零。

**修复**：
1. 极简版移除 `__disable_irq()`
2. `deinit_peripherals()` 末尾加 `__enable_irq()` 恢复到复位默认值

---

## 三、总结

| 问题 | 根因 | 修复 |
|---|---|---|
| 向量表入口地址错误 | `board_build.offset` 在 Arduino STM32 框架不生效 | 自定义 `app_offset.ld` 硬编码 FLASH origin |
| APP "卡死"无串口输出 | `__disable_irq()` 导致 PRIMASK=1，TX 中断被屏蔽 | 移除不必要的 `__disable_irq()`；deinit 后恢复 |

**教训**：
- 用 objdump/addr2line 定位 GDB 中断时的实际函数，不要假设是"崩溃"
- Cortex-M 上 BLX 跳转不改变 PRIMASK，需要匹配硬件复位状态
- Arduino STM32 框架的 `board_build.offset` 不可靠，建议直接用自定义链接脚本

# SPI 通信故障分析报告

## 1. 问题现象

ESP32 (SPI Master) 与 STM32F407 (SPI Slave) 之间 SPI 通信，STM32 端 `[SPI.DIAG]` 诊断输出：

```
[SPI.DIAG] total=603  ok=0  bad_crc=0  hs=0  cmd=00
[SPI.DIAG] total=1421 ok=0  bad_crc=0  hs=0  cmd=00
[SPI.DIAG] total=2199 ok=0  bad_crc=0  hs=0  cmd=00
```

- `total` 递增 → **SPI 物理层通信正常**（NSS 引脚被 ESP32 拉低 1400+ 次）
- `ok=0` → **STM32 从未检测到任何有效帧**
- `bad_crc=0` → 帧头 `[AA][55]` 从未被检测到

ESP32 端持续超时重试：

```
[HS] Sent HANDSHAKE_REQ (attempt 1)
[HS] Timeout, retry 2/3
[HS] Sent HANDSHAKE_REQ (attempt 2)
[HS] Timeout, retry 3/3
[HS] Sent HANDSHAKE_REQ (attempt 3)
[HS] Max retries, will retry later
```

---

## 2. 硬件与配置

| 参数 | ESP32 (Master) | STM32F407 (Slave) |
|------|---------------|-------------------|
| SPI | VSPI (HSPI) | SPI2 |
| SCK | GPIO 12 | PB13 (AF5) |
| MISO | GPIO 14 | PB14 (AF5) |
| MOSI | GPIO 13 | PB15 (AF5) |
| CS/NSS | GPIO 15 | PB12 (硬件NSS) |
| 时钟 | 1 MHz | 1 MHz (slave) |
| 模式 | MODE0 (CPOL=0, CPHA=0) | MODE0 |
| 数据位 | 8-bit, MSB first | 8-bit, MSB first |

---

## 3. 通信时序

ESP32 发送一帧的流程：

```
digitalWrite(CS, LOW);           // t=0, NSS拉低
for (i = 0; i < 700; i++) {
    SPI.transfer(txBuf[i]);      // 每字节 8µs @ 1MHz
}
digitalWrite(CS, HIGH);          // t≈5600µs, NSS拉高
```

- 帧长度：**700 字节**
- 传输时间：700 × 8µs = **5.6ms**
- 帧结构（当前版本）：前 695 字节 0xFF 填充，末尾 5 字节 `[AA][55][type][seq][crc]`

STM32 端 FreeRTOS 任务轮询：

```c
void esp32_task(void *pvParameters) {
    for (;;) {
        rx_len = spi_poll_rx_frame();   // 阻塞轮询（NSS低时进入紧循环）
        if (rx_len > 0) process_frame();
        // ... 握手状态机、OAT超时检测 ...
        vTaskDelay(pdMS_TO_TICKS(1));   // 睡眠 1ms ← 关键问题！
    }
}
```

---

## 4. 根因分析

### 4.1 核心问题：STM32F4 SPI 只有单字节接收缓冲

**STM32F407 的 SPI 外设默认只有 1 字节深度的接收缓冲**（无硬件 FIFO，或 FIFO 未使能，需要显式调用 `HAL_SPIEx_EnableFifo()` 且代码中未见此调用）。

这意味着：
- 收到第 1 个字节 → 存入 DR 寄存器，RXNE 置位
- 收到第 2 个字节（8µs 后）→ DR 仍被占用 → **OVR (Overrun) 立即触发**
- **OVR 触发后，所有后续字节全部丢失**（硬件停止接收直到 OVR 被清除）

### 4.2 致命时间差

```
时间轴 (µs):
0   8   16  24  32  ...                   1000         ...                    5600
|──|──|──|──|──────────────────────────────────────────────────────────────────|
B0  B1  B2  B3  B4                                                          NSS↑
    ^
    └─ OVR! B1..B699 全部丢失

STM32任务:  [睡眠中.................................................................................]→醒来→spi_poll_rx_frame()
                                                                                 1000
            ↑ NSS低，但任务在睡觉，SPI硬件已OVR，只捕获了1个字节(0xFF)
```

- 任务睡眠时间：**1000µs (1ms)**
- 单字节溢出窗口：**8µs**
- 差距：**125 倍**

### 4.3 为什么"帧头放末尾"方案也不起作用

理论分析：任务在 t≈1000µs 时醒来，清除 OVR，进入紧循环。ESP32 仍在发送字节 125~699。任务应能捕获约 575 个字节，其中包括末尾的 `[AA][55][type][seq][crc]`。

但实际上 `ok=0`，可能的原因：

1. **ESP32 `SPI.transfer()` 存在字节间间隙**
   
   Arduino ESP32 的 `SPI.transfer()` 对每个字节执行：进临界区→写DR→等完成→读DR→出临界区。连续 700 次调用存在累积延迟，实际传输时间可能远大于 5.6ms。
   
   如果字节间间隙导致 STM32 紧循环中的 `RXNE` 等待超时（`RX_BUF_LEN * 100` 次 NOP = 约 0.4ms），循环会提前退出，无法捕获到末尾的帧头。

2. **任务醒来时 NSS 可能已经拉高**
   
   如果 FreeRTOS 调度器恰好让任务睡眠超过 5.6ms（比如被其他更高优先级任务抢占），那么醒来时整个传输已经完成，NSS 为高，`spi_poll_rx_frame()` 检查第一行 NSS 就直接返回 0。

3. **OVR 清除后的同步丢失**
   
   清除 OVR 后，SPI 从机可能需要 1 个字节的时间重新与主机时钟同步。这期间接收的数据可能错位（bit-shifted），导致后续所有字节都是"垃圾数据"，`[AA][55]` 永远不会匹配。

4. **STM32 任务实际调度间隔不可靠**
   
   `vTaskDelay(1ms)` 只是"至少 1ms"，实际睡眠时间取决于 FreeRTOS tick 精度（通常 1ms/tick）和其他任务的优先级。如果 `esp32_task` 优先级较低，可能被长时间饿死。

---

## 5. 根本矛盾

| 需求 | 现实 | 差距 |
|------|------|------|
| 1MHz SPI，每字节 8µs | 任务睡眠 1ms | 125:1 |
| 单字节接收缓冲 | 上一字节必须 8µs 内读走 | 不可能在睡眠时做到 |
| 700 字节帧，5.6ms 传输 | 任务 5~7 次醒来 | 每次醒来都丢失数据 |

**结论：基于 `vTaskDelay` 的轮询架构从根本上无法可靠接收 SPI 数据。这不是代码 bug，而是架构缺陷。**

---

## 6. 推荐解决方案（按优先级排序）

### 方案 A：SPI DMA 接收（推荐，工业级最佳方案）

**原理**：STM32 的 DMA 控制器可以在完全不占用 CPU 的情况下，将 SPI 接收的每个字节自动存入内存缓冲区。

**优点**：
- CPU 完全解放，不需要紧循环轮询
- 零数据丢失（DMA 逐字节搬运，响应时间 < 1µs）
- 支持任意长度的帧
- 任务可以继续用 `vTaskDelay` 做其他事

**实现要点**：
- 配置 SPI2 的 DMA RX 通道（DMA1_Stream3 / Channel 0）
- 使用 NSS 上升沿触发 DMA 传输完成中断
- 在中断中设置标志量，任务轮询该标志量

```c
// 伪代码示意
HAL_SPI_Receive_DMA(&g_spi, rx_buf, RX_BUF_LEN);
// DMA 自动将每个 SPI 字节存入 rx_buf
// NSS 上升沿 → SPI 传输结束 → DMA TC 中断 → 通知任务
```

### 方案 B：NSS EXTI 中断 + 紧循环（中等方案）

**原理**：用 NSS 下降沿触发外部中断，中断中进入紧循环读取，不依赖任务调度延迟。

**优点**：
- 响应快（中断延迟 < 1µs）
- 不依赖 FreeRTOS 调度
- 实现相对简单

**缺点**：
- 中断中禁用调度器 5.6ms，影响系统实时性
- 依旧使用 CPU 紧循环，浪费 CPU 资源

### 方案 C：降低 SPI 时钟 + 减小帧长度（简单但不推荐）

将 SPI 时钟降到足够低，使 1ms 内的字节数 < 接收缓冲深度。

- 要使 1ms 内字节数 ≤ 1（单缓冲），需将 SPI 时钟降到 **8 kHz**
- 700 字节传输时间 = 700ms，不实用
- 如果先使能 4 级 FIFO（`HAL_SPIEx_EnableFifo(&g_spi)`），1ms/4 = 250µs/byte = 4 kHz，依旧不实用

---

## 7. 附加建议

1. **使能 SPI FIFO**（即使不用 DMA）：`HAL_SPIEx_EnableFifo(&g_spi)` 可将缓冲从 1 字节扩展到 4 字节，提供少量容错空间。
2. **提高 esp32_task 优先级**：确保不被其他任务长期抢占。
3. **ESP32 端使用 DMA 发送**：减少字节间间隙，确保传输时序紧凑。
4. **添加帧序号**：帮助诊断丢帧率。

---

## 8. 文件清单

| 文件 | 说明 |
|------|------|
| `src/modules/esp32.cpp` | STM32 SPI 从机通信 + 握手状态机 |
| `src/modules/esp32.h` | 握手协议常量定义 |
| `D:\Apro\master\esp\esp32test\src\main.cpp` | ESP32 SPI 主机 + MQTT + 握手协议 |

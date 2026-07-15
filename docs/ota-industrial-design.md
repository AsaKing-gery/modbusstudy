# 工业级 OTA 固件升级设计方案

> 参考：Espressif OTA 官方文档、ESP-Hosted Slave OTA、MCUBoot A/B 分区模式



## 1. 架构概览

```
   ┌──────────────┐                  ┌──────────────────────┐
   │  HTTP 服务器  │                  │     STM32F407        │
   │  firmware.bin │                  │                      │
   │  version.txt  │                  │  ┌────────────────┐  │
   │  firmware.sig │                  │  │  Bootloader    │  │
   └──────┬───────┘                  │  │  (选分区+回滚)  │  │
          │                          │  └───────┬────────┘  │
          │                          │          │            │
   ┌──────┴───────┐                  │  ┌───────┴────────┐  │
   │   ESP32      │     SPI          │  │  App A / App B │  │
   │  (OTA Gateway)│◄═══════════════►│  │  (A/B 双分区)  │  │
   │              │  CS/MISO/MOSI/SCK│  └───────┬────────┘  │
   │  ┌────────┐  │                  │          │            │
   │  │WiFi/MQTT│  │                  │  ┌───────┴────────┐  │
   │  │HTTP下载 │  │                  │  │  元数据扇区    │  │
   │  │版本决策 │  │                  │  │  (版本+签名+   │  │
   │  │chunk发送│  │                  │  │   启动状态)    │  │
   │  └────────┘  │                  │  └────────────────┘  │
   └──────────────┘                  └──────────────────────┘
```

**职责划分：**

| 功能 | 负责方 | 说明 |
|------|--------|------|
| WiFi / HTTP / MQTT | ESP32 | 联网能力 |
| 版本对比决策 | ESP32 | 对比 server vs STM32，决定是否升级 |
| 固件下载 | ESP32 | HTTP 流式下载，chunk 级别重试 |
| 固件发送 | ESP32 | SPI Master，握手→数据→结果 |
| 版本报告 | STM32 | 上电后主动报告当前版本 |
| Flash 写入 | STM32 | 接收→校验→写入备用分区 |
| 启动管理 | STM32 | Bootloader 选分区 + 回滚 |



## 2. 核心设计原则

### 原则 1：版本真相在 STM32

- ESP32 **不硬编码** STM32 版本号
- 每次上电，STM32 通过 SPI MISO 主动报告当前版本（BOOT_REPORT 帧 0x02）
- ESP32 收到报告后，对比服务器版本，决定是否 OTA

### 原则 2：A/B 分区 + 启动确认

- STM32 Flash 分 App A（扇区5）和 App B（扇区6）
- 元数据扇区记录：active_slot、各分区版本/签名/try_count/boot_success
- 新固件首次启动：调用 `ota_confirm_success()` 确认 → 写入 boot_success
- 若启动后 N 次看门狗复位 → try_count 超限 → Bootloader 回滚到旧分区

### 原则 3：单次触发，禁止循环

- 每次上电周期**仅执行一次** OTA 版本检查
- OTA 完成后（成功或失败），状态机回到 OTA_IDLE，**不再自动触发**
- 手动 OTA 通过 Modbus 寄存器或云端 MQTT 触发

### 原则 4：可恢复的错误处理

- HTTP chunk 读取失败 → 重试 3 次（重新建立 HTTP 连接）
- OTA_CHECKING / OTA_DOWNLOADING 超时 60 秒 → 自动回到 OTA_IDLE
- Flash 写入失败 → OTA_FAILED → 清元数据 → 回到 IDLE
- ESP32 崩溃/重启 → STM32 超时后自动恢复



## 3. 通信协议

### 3.1 帧类型定义

```
┌──────────────────────────────────────────────────────────────────┐
│ 值     │ 名称          │ 方向        │ 大小     │ 用途           │
├────────┼───────────────┼─────────────┼──────────┼────────────────┤
│ 0x01   │ SENSOR_DATA   │ ESP→STM     │ 14 B     │ 传感器数据      │
│ 0x02   │ BOOT_REPORT   │ STM→ESP     │ 9 B      │ 启动版本报告    │
│ 0xF0   │ OTA_HANDSHAKE │ ESP→STM     │ 522 B    │ OTA 握手/元信息 │
│ 0xF1   │ OTA_DATA      │ ESP→STM     │ 10+512 B │ OTA 数据块      │
│ 0xF2   │ OTA_RESULT    │ ESP→STM     │ 6 B      │ OTA 结果        │
└──────────────────────────────────────────────────────────────────┘
```

### 3.2 BOOT_REPORT 帧 (0x02) — 新增

STM32 上电后通过 MISO 向 ESP32 报告当前运行版本。

```
Byte:  [0]  [1]  [2]  [3]    [4]    [5]    [6]    [7]       [8]
       ┌────┬────┬────┬──────┬──────┬──────┬──────┬──────────┬────┐
       │0xAA│0x55│0x02│ver[3]│ver[2]│ver[1]│ver[0]│boot_stat │CRC8│
       └────┴────┴────┴──────┴──────┴──────┴──────┴──────────┴────┘
                                                      │
                                       ┌──────────────┘
                                       │ 0x00 = 正常启动 (boot_success已确认)
                                       │ 0x01 = OTA后首次启动 (未确认)
                                       │ 0xFF = 回滚启动 (上次OTA失败)
```

**发送方式：** STM32 作为 SPI Slave，在 ESP32 Master 进行任何 SPI 传输时，MISO 字节流中嵌入 BOOT_REPORT。具体做法：在 `ota_status == OTA_CHECKING` 期间，`g_miso_cmd` 持续设置为 `ESP32_CMD_OTA_START`，同时在 MISO 数据字节中循环发送 9 字节 BOOT_REPORT 帧。

ESP32 端通过轮询 MISO（每 500ms 发送 1 字节 dummy），累积收到的字节，检测 0xAA 0x55 0x02 帧头，解析出 STM32 版本。

### 3.3 OTA_HANDSHAKE 帧 (0xF0)

```
Byte:  [0]  [1]  [2]  [3-6]     [7-10]   [11-42]  [43-520]   [521]
       ┌────┬────┬────┬──────────┬────────┬────────┬──────────┬────┐
       │0xAA│0x55│0xF0│file_size │version │sig(32) │0xFF pad  │CRC8│
       └────┴────┴────┴──────────┴────────┴────────┴──────────┴────┘
       ◀────────────────── 522 bytes total ──────────────────────────▶
```

填充到 522 字节的原因：STM32 轮询间隔 ~1ms，44 字节握手帧仅 352μs 会被完全错过。522 字节 = 4.176ms 远大于轮询间隔。

### 3.4 OTA_DATA 帧 (0xF1)

```
Byte:  [0]  [1]  [2]  [3-6]    [7-8]    [9-520]    [521]
       ┌────┬────┬────┬────────┬────────┬──────────┬────┐
       │0xAA│0x55│0xF1│offset  │chunk_len│data(512) │CRC8│
       └────┴────┴────┴────────┴────────┴──────────┴────┘
       ◀──────────────── 522 bytes (含 512B 数据) ─────────────▶
```

### 3.5 OTA_RESULT 帧 (0xF2)

```
Byte:  [0]  [1]  [2]  [3]      [4]    [5]
       ┌────┬────┬────┬────────┬──────┬────┐
       │0xAA│0x55│0xF2│status  │resv  │CRC8│
       └────┴────┴────┴────────┴──────┴────┘
                              │
                 ┌────────────┘
                 │ 0x00 = 成功 (触发 STM32 重启)
                 │ 0x01 = 失败 (STM32 回到 IDLE)
```



## 4. 状态机设计

### 4.1 ESP32 状态机

```
                    ┌─────────────────────────────────────────┐
                    │              OTA_IDLE                    │
                    │  (等待 MISO 中的 BOOT_REPORT)             │
                    └────────────────┬────────────────────────┘
                                     │ 解析到完整 BOOT_REPORT
                                     │ 记录 stm32Version
                                     ▼
                    ┌─────────────────────────────────────────┐
                    │          CHECK_SERVER_VERSION            │
                    │  HTTP GET /version.txt → serverVersion  │
                    └────────────────┬────────────────────────┘
                                     │
               ┌─────────────────────┼─────────────────────┐
               │ server > stm32      │ server <= stm32     │ HTTP 失败
               ▼                     ▼                     ▼
       ┌───────────────┐   ┌─────────────────┐   ┌─────────────────┐
       │DOWNLOAD_FW    │   │   NO_UPDATE      │   │   CHECK_ERROR   │
       │HTTP流打开固件  │   │ 打印"无需更新"    │   │ 打印错误日志     │
       └───────┬───────┘   │ → OTA_IDLE       │   │ → OTA_IDLE      │
               │           └─────────────────┘   └─────────────────┘
               ▼
       ┌───────────────┐
       │SEND_HANDSHAKE │  发送 522B 握手帧 → 等待 200ms (STM32 擦除)
       └───────┬───────┘
               ▼
       ┌───────────────┐
       │ SEND_CHUNKS   │◄──────── 逐个发送 512B 数据块 ────────┐
       │  (OTA_SPI=5ms) │                                       │
       └───────┬───────┘                                       │
               │                                               │
       ┌───────┼───────────────────────────────────────┐       │
       │ chunk │ HTTP 读取失败                           │       │
       │ 成功   │                                        │       │
       │       ▼                                        │       │
       │  ┌──────────────┐                              │       │
       │  │CHUNK_RETRY   │  重新打开 HTTP 连接           │       │
       │  │(最多 3 次)    │  使用 Range 请求从断点续传    │       │
       │  └──────┬───────┘                              │       │
       │         │                                      │       │
       │    ┌────┼────┐                                 │       │
       │    │重试  │重试│                                │       │
       │    │成功  │耗尽│                                │       │
       │    ▼     ▼    │                                │       │
       │  继续  SEND_  │                                │       │
       │  发送  RESULT │                                │       │
       │        (0x01) │                                │       │
       │         →IDLE │                                │       │
       │               │                                │       │
       ▼               │                                │       │
  全部 chunk 发送完成    │                                │       │
       │               │                                │       │
       ▼               │                                │       │
  ┌──────────────┐     │                                │       │
  │SEND_RESULT   │     │                                │       │
  │  (0x00)      │     │                                │       │
  │ 等待 STM32   │     │                                │       │
  │ 重启         │     │                                │       │
  └──────────────┘     │                                │       │
       │               │                                │       │
       ▼               │                                │       │
  STM32 重启后         │                                │       │
  收到新 BOOT_REPORT    │                                │       │
  stm32Version ==      │                                │       │
  serverVersion         │                                │       │
  → OTA_IDLE           │                                │       │
  (不再触发OTA)         │                                │       │
       └───────────────┴────────────────────────────────┘       │
       └────────────────────────────────────────────────────────┘
```

### 4.2 STM32 状态机

```
                         ┌─────────┐
                         │  上电    │
                         └────┬────┘
                              ▼
                    ┌─────────────────┐
                    │   BOOTLOADER     │
                    │ 读取元数据扇区    │
                    │ 选active_slot    │
                    │ 跳转到 App 分区  │
                    └────────┬────────┘
                             ▼
                    ┌─────────────────┐
                    │  APP 启动        │
                    │ 1.初始化外设     │
                    │ 2.ota_check_ver  │
                    │   发送 0x10 cmd  │
                    │   开始 MISO 报告 │
                    │   版本 (0x02帧)  │
                    │ 3.ota_confirm    │
                    │   boot_success   │
                    │ 4.创建任务        │
                    └────────┬────────┘
                             ▼
              ┌──────────────────────────────┐
              │         OTA_IDLE             │◄──────────── 超时/失败 回退 ──┐
              │  正常运行，等待 OTA 帧         │                             │
              └──────────────┬───────────────┘                             │
                             │ 收到 OTA_HANDSHAKE (0xF0)                   │
                             │ CRC 通过                                    │
                             ▼                                             │
              ┌──────────────────────────────┐                             │
              │      OTA_DOWNLOADING          │                             │
              │  擦除备用分区 (Flash Erase)    │                             │
              │  等待 OTA_DATA (0xF1) 帧      │                             │
              │  逐个 chunk 写入 Flash         │                             │
              └──────────────┬───────────────┘                             │
                             │ 收到 OTA_RESULT (0xF2)                      │
                             │                                             │
              ┌──────────────┼──────────────┐                              │
              │ status=0x00  │ status=0x01  │                              │
              ▼              ▼              │                              │
     ┌────────────┐  ┌────────────┐        │                              │
     │  成功       │  │   失败      │        │                              │
     │ 更新元数据  │  │ 清元数据    │        │                              │
     │ active_slot │  │ 回到 IDLE  │────────┼──────────────────────────────┘
     │ = 新分区    │  └────────────┘        │
     │ NVIC_Reset  │                        │
     └────────────┘                         │
          │                                 │
          ▼                                 │
     ┌─────────┐                            │
     │ 重启    │                            │
     │ 进入    │                            │
     │Bootloader│                           │
     │选新分区  │                            │
     └─────────┘                            │
          │                                 │
          ▼                                 │
     新固件运行                               │
     BOOT_REPORT 报告新版本                   │
     ESP32 看到版本已更新 → 不再 OTA ─────────┘
```



## 5. 关键流程时序

### 5.1 正常上电（无需更新）

```
STM32                                    ESP32
  │                                        │
  │ BOOT: 读元数据 → 选 active_slot         │
  │ APP 初始化                              │
  │ esp32_set_miso_cmd(0x10) ──────────────►│ (MISO=0x10)
  │ MISO 持续发 BOOT_REPORT (0x02帧) ──────►│ 累积字节, 解析 0x02 帧
  │ ota_confirm_success()                   │ → stm32Version = 40504
  │ app_create_tasks() 全部完成              │
  │                                        │
  │                                        │ HTTP GET /version.txt
  │                                        │ → serverVersion = 40504
  │                                        │
  │                                        │ 40504 <= 40504 → 无需更新
  │                                        │ otaState = OTA_IDLE
  │                                        │
  │═══ 正常运行 ════════════════════════════│═══ 正常运行 ═══
```

### 5.2 OTA 升级流程（需更新）

```
STM32                                    ESP32
  │  MISO: 0x10 + BOOT_REPORT(ver=40503) ─►│ 解析: stm32Version = 40503
  │                                        │
  │                                        │ HTTP GET /version.txt
  │                                        │ → serverVersion = 40504
  │                                        │ 40504 > 40503 → 需要更新!
  │                                        │
  │                                        │ HTTP GET /firmware.bin (流式)
  │                                        │ otaState = SENDING_HANDSHAKE
  │                                        │
  │◄── OTA_HANDSHAKE (522B) ─────────────  │ 发送握手帧
  │ CRC 通过                                │
  │ ota_status = DOWNLOADING               │
  │ 擦除备用分区                             │
  │                                        │ otaState = SENDING_DATA
  │                                        │
  │◄── OTA_DATA[0] offset=0 ──────────────│ 读 HTTP 流 chunk0
  │ 写 Flash @ 备用分区+0                    │
  │◄── OTA_DATA[1] offset=512 ────────────│ 读 HTTP 流 chunk1
  │ 写 Flash @ 备用分区+512                  │
  │    ...                                  │   ...
  │                                        │
  │   (如果某个 chunk HTTP 读取失败)          │
  │                                        │ ┌─ CHUNK_RETRY ─┐
  │                                        │ │ 重新 HTTP 连接  │
  │                                        │ │ Range: bytes=  │
  │                                        │ │   offset-      │
  │                                        │ │ 重试最多3次     │
  │                                        │ └───────────────┘
  │                                        │
  │◄── OTA_DATA[N] offset=71168 ──────────│ 最后一个 chunk
  │ 写 Flash                                │
  │                                        │
  │◄── OTA_RESULT (status=0x00) ──────────│ 发送结果: 成功
  │ 更新元数据:                              │
  │   active_slot = 新分区                   │
  │   signature = 新签名                     │
  │   version = 40504                       │
  │   boot_success = 0x00 (待确认)           │
  │                                        │
  │ NVIC_SystemReset() ──────────────────  │
  │                                        │
  │ === 重启 ===                            │
  │ Bootloader 选新的 active_slot            │
  │ 新固件 v4.05.04 运行                     │
  │                                        │
  │ MISO: BOOT_REPORT(ver=40504) ──────────►│ 解析: stm32Version = 40504
  │ ota_confirm_success()                   │ 40504 == 40504 → OTA_IDLE
  │ boot_success = 0xAA                     │ (不再触发新一轮OTA!)
  │                                        │
  │═══ 正常运行 (新版本) ════════════════════│═══ 正常运行 ═══
```



## 6. 错误处理矩阵

| 场景 | ESP32 行为 | STM32 行为 | 恢复路径 |
|------|-----------|-----------|---------|
| HTTP 连接失败 | 重试 3 次，间隔 5s | 60s 超时 → OTA_IDLE | 下次手动触发 |
| chunk HTTP 读取失败 | 重新连接 + Range 续传，最多 3 次 | 等待下一 chunk | 续传成功继续，耗尽→失败 |
| chunk CRC 错误 | N/A | 丢弃该 chunk | 触发 ESP32 重发 (MISO=0x20) |
| Flash 擦除/写入失败 | N/A | OTA_FAILED | 元数据不变，旧固件继续运行 |
| OTA_CHECKING 超时 60s | 无操作 | 自动 → OTA_IDLE | 系统恢复正常运行 |
| OTA_DOWNLOADING 60s 无帧 | 无操作 | 自动 → OTA_FAILED | 旧固件继续运行 |
| OTA 期间断电 | ESP32 重启 | Bootloader 检测 boot_success=0 | 回滚到旧分区 |
| ESP32 崩溃重启 | 重启后 OTA_IDLE | 60s 超时 → OTA_IDLE | 系统恢复正常 |
| STM32 看门狗复位 | ESP32 等待 | try_count++ → 超限回滚 | 回滚到旧分区 |



## 7. 实现清单

### 7.1 STM32 端改动

| 文件 | 改动内容 |
|------|---------|
| `esp32.h` | 新增 `ESP32_FRAME_BOOT_REPORT = 0x02` |
| `esp32.cpp` | 新增 `spi_send_boot_report()` — 在 MISO 上循环发送 9 字节 BOOT_REPORT 帧 |
| `esp32.cpp` | `esp32_task()` — OTA_CHECKING 期间持续发送 BOOT_REPORT（与 `g_miso_cmd=0x10` 一起） |
| `esp32.cpp` | `esp32_task()` — 延迟策略：OTA_DOWNLOADING→vTaskDelay(0)，OTA_CHECKING→vTaskDelay(1)，其他保持 |
| `esp32.cpp` | `spi_poll_rx_frame()` — 帧对齐（NSS=LO 入时排空残帧） |
| `ota.cpp` | `ota_check_version()` — 只设置 cmd + 状态，不自动循环 |
| `ota.cpp` | `ota_handle_result()` — status=0x00 时更新元数据后 NVIC_SystemReset() |
| `ota.cpp` | `ota_update_modbus_regs()` — 60s 超时恢复 OTA_IDLE |
| `bsp_config.h` | `TASK_PRIO_ESP32 = 4`（已完成） |

### 7.2 ESP32 端改动

| 文件 | 改动内容 |
|------|---------|
| `main.cpp` | 删除 `CURRENT_FW_VERSION` 宏，改为变量 `stm32FwVersion` |
| `main.cpp` | 新增 `parseBootReport()` — 从 MISO 字节流解析 0x02 帧 |
| `main.cpp` | `esp32CheckMisoCmd()` — 增加 BOOT_REPORT 解析逻辑 |
| `main.cpp` | MISO 轮询改为累积字节 → 检测 0xAA 0x55 0x02 → 解析版本 |
| `main.cpp` | `otaCheckAndDownload()` — 对比 serverVersion vs stm32FwVersion |
| `main.cpp` | `otaSendDataChunk()` — HTTP 流读取失败时：重新连接 + Range 续传，最多 3 次 |
| `main.cpp` | `otaRunStateMachine()` — OTA 成功后回到 IDLE，不自动循环 |
| `main.cpp` | 测试帧：`otaState == OTA_IDLE` 保护（已完成） |
| `main.cpp` | 握手帧：522 字节填充（已完成） |



## 8. 关键常量

```c
// === STM32 ===
#define ESP32_FRAME_BOOT_REPORT  0x02   // 新增
#define ESP32_OTA_FRAME_MAX       522
#define TASK_PRIO_ESP32            4
#define OTA_CHECK_TIMEOUT_MS    60000   // 60s

// === ESP32 ===
#define OTA_CHUNK_SIZE            512
#define OTA_SPI_DELAY_MS            5   // chunk 间延迟
#define OTA_CHUNK_RETRY_MAX         3   // chunk 重试次数
#define OTA_HTTP_RETRY_MAX          3   // HTTP 连接重试
#define MISO_POLL_INTERVAL_MS     500   // MISO 轮询间隔
#define BOOT_REPORT_LEN             9   // BOOT_REPORT 帧长度
```



## 9. 版本号流转示意

```
初始状态: STM32=40503, Server=40504, ESP32 硬编码=40503 (旧问题!)

修复后:
┌──────────┐     ┌──────────┐     ┌──────────┐
│  Server  │     │  ESP32   │     │  STM32   │
│  40504   │     │ stm32Ver │     │  40503   │
└────┬─────┘     │  = ???   │     └────┬─────┘
     │           └────┬─────┘          │
     │                │◄── BOOT_REPORT ─┤ (ver=40503)
     │                │  stm32Ver=40503 │
     │                │                │
     │◄─GET /ver.txt──┤                │
     │── 40504 ──────►│                │
     │                │ 40504 > 40503  │
     │                │ → 开始 OTA     │
     │                │                │
     │── firmware ───►│── chunks ─────►│ 写 Flash
     │                │── result ─────►│ 重启
     │                │                │
     │                │◄── BOOT_REPORT ─┤ (ver=40504)
     │                │  stm32Ver=40504 │
     │                │ 40504 == 40504 │
     │                │ → OTA_IDLE ✓   │
     │                │                │
     │         (不再触发新一轮OTA!)      │
```



## 10. 与当前实现的差异总结

| 方面 | 当前实现（问题） | 新设计 |
|------|-----------------|--------|
| 版本来源 | ESP32 硬编码 `CURRENT_FW_VERSION` | STM32 通过 BOOT_REPORT(0x02) 报告 |
| OTA 触发 | STM32 主动触发，ESP32 被动 | ESP32 收到版本后自主决策 |
| 循环问题 | STM32 不停发 0x10，OTA 完毕后又触发 | 单次检查，完成后不再自动触发 |
| chunk 失败 | 整个 OTA 标记失败 | chunk 级别重试 3 次 + Range 续传 |
| 版本同步 | ESP32 永远不知道 STM32 更新了 | BOOT_REPORT 实时同步 |
| 回滚 | 无 | try_count + boot_success + 自动回滚 |

# 畜畜养殖巡检系统 V4 — OTA 双备份升级方案

**日期**: 2026-07-14  
**平台**: STM32F407VET6 (512KB Flash, 128KB RAM)  
**框架**: Arduino + FreeRTOS + PlatformIO

---

## 一、需求摘要

- 远程固件升级（通过 ESP32 WiFi）
- 双备份 A/B 分区 + 启动失败自动回滚
- HMAC-SHA256 固件签名校验
- 上电自动检查新版本
- 固件最大 128KB（当前 ~70KB，余量充足）

---

## 二、Flash 分区布局

```
地址            扇区    大小      内容
─────────────────────────────────────────────
0x08000000      0       16KB     Bootloader
0x08004000      1       16KB     Bootloader
0x08008000      2       16KB     Bootloader          ← 共 48KB
0x0800C000      3       16KB     元数据区
0x08010000      4       64KB     保留 (预留扩展)
0x08020000      5       128KB    APP1 (出厂/运行中)
0x08040000      6       128KB    APP2 (OTA 目标)
0x08060000      7       128KB    未使用
```

**约束**:
- APP1/APP2 各占整扇区 128KB，擦除时互不影响
- 元数据占独立 16KB 扇区，一次擦写原子更新
- 出厂时 APP1 存有效固件，APP2 为空

---

## 三、元数据结构 (`src/bootloader/shared/boot_config.h`)

Bootloader 与 APP 共享此头文件。

```c
#define APP1_START      0x08020000
#define APP2_START      0x08040000
#define METADATA_ADDR   0x0800C000
#define SIGNATURE_SIZE  32              // HMAC-SHA256
#define BOOT_SUCCESS    0xAA
#define MAX_RETRIES     3
#define MAGIC_VALID     0xA5A5A5A5

typedef struct {
    uint8_t  signature[SIGNATURE_SIZE];
    uint32_t version;
    uint8_t  try_count;
    uint8_t  max_tries;
    uint8_t  boot_success;
    uint8_t  reserved[2];
} AppSlotMeta_t;  // 44 bytes

typedef struct {
    uint8_t       active_slot;
    uint8_t       reserved[3];
    AppSlotMeta_t slot[2];
    uint32_t      magic;
} BootMetadata_t;  // 96 bytes
```

---

## 四、Bootloader 工作流程

### 4.1 主流程

```
上电 / IWDG 复位
    │
    ▼
初始化 SystemClock (HSI 8MHz, 不依赖外部晶振)
    │
    ▼
读元数据区 (0x0800C000)
    │
    ├── magic ≠ MAGIC_VALID → 首次启动:
    │     初始化元数据: active_slot=0, max_tries=3, 其余清零
    │     写回元数据区
    │     跳转到 "启动 APP" 步骤
    │
    ▼
取 active_slot → app_addr = slot0? APP1_START : APP2_START
    │
    ▼
计算 HMAC-SHA256(app_addr 分区内容, 硬编码密钥)
    │
    ▼
与 meta.slot[active_slot].signature 比较
    │
    ├── 匹配:
    │     boot_success = 0x00 (清除成功标志)
    │     写回元数据区
    │     启动 IWDG (30 秒超时)
    │     跳转到 APP
    │
    └── 不匹配:
          try_count++
          若 try_count >= max_tries:
              active_slot ^= 1 (切换分区)
              新 slot.try_count = 0
          写回元数据
          NVIC_SystemReset() → 重新进入 Bootloader
```

### 4.2 跳转到 APP 的实现

```c
void jump_to_app(uint32_t app_addr) {
    uint32_t stack_ptr = *(volatile uint32_t*)app_addr;
    uint32_t reset_handler = *(volatile uint32_t*)(app_addr + 4);

    __disable_irq();

    // 关闭所有外设 (确保 APP 纯净启动)
    // RCC 复位 AHB1/AHB2/AHB3/APB1/APB2 外设
    // ...

    SCB->VTOR = app_addr;
    __set_MSP(stack_ptr);

    // 函数指针跳转到复位向量
    void (*app_reset)(void) = (void (*)(void))reset_handler;
    app_reset();  // 永不返回
}
```

### 4.3 Bootloader 代码组成

| 模块 | 文件 | 说明 |
|------|------|------|
| 入口 | `src/bootloader/main.c` | 主逻辑 |
| Flash 驱动 | `src/bootloader/flash_drv.c` | HAL 封装: 解锁/擦除/写/读 |
| SHA256 | `src/bootloader/sha256.c` | 纯 C 实现 |
| HMAC | `src/bootloader/hmac.c` | HMAC-SHA256 |
| 元数据 | `src/bootloader/metadata.c` | 读写校验元数据区 |
| 链接脚本 | `bootloader.ld` | FLASH 0x08000000 LEN=48K |
| 共享头文件 | `src/bootloader/shared/boot_config.h` | APP/BOOT 共用 |

Bootloader 使用裸机 C + STM32Cube HAL，不依赖 Arduino 框架。

---

## 五、APP 端适配

### 5.1 链接与向量表

- `platformio.ini` 中 `board_build.offset = 0x20000`
- 自定义链接脚本 `app.ld`：FLASH 起始 0x08020000，长度 128KB
- `SystemInit()` 中首条语句：`SCB->VTOR = APP1_START`

### 5.2 OTA 确认机制

APP 在 setup() 末尾调用 `ota_confirm_success()`：

```cpp
void ota_confirm_success(void) {
    BootMetadata_t meta;
    flash_read(METADATA_ADDR, &meta, sizeof(meta));
    if (meta.magic != MAGIC_VALID) return;

    meta.slot[meta.active_slot].boot_success = BOOT_SUCCESS;
    meta.slot[meta.active_slot].try_count = 0;

    flash_unlock();
    flash_erase_sector(3);     // 元数据所在扇区
    flash_write(METADATA_ADDR, &meta, sizeof(meta));
    flash_lock();
}
```

**时序约束**: 30 秒内未调用 → IWDG 复位 → Bootloader 检测失败 → 回滚。

### 5.3 上电自动检查 OTA（核心变更）

APP 启动后在 OTA 确认之前执行版本检查：

```
setup()
  ├── bsp_gpio_init()
  ├── 各模块初始化 (LCD, Modbus, HMI...)
  ├── ota_check_version()  ← 新增
  │     ├── 拉高 ESP32_OTA_REQ
  │     ├── ESP32 连接 WiFi → 请求服务器 /latest.txt
  │     ├── 比较版本号
  │     │     ├── 相同 → 跳过
  │     │     └── 更新 → ota_download()
  │     │           ├── 下载 firmware.bin + firmware.sig
  │     │           ├── 写入备用分区
  │     │           ├── 更新元数据 (签名 + 切换 active_slot)
  │     │           └── NVIC_SystemReset()
  │     └── 失败 → 继续正常运行 (下次再试)
  ├── ota_confirm_success()  ← 必须在 30s 内
  └── 正常业务
```

**超时安全**: 若网络不通或下载卡住，30 秒 IWDG 复位 → Bootloader → 重新启动当前 APP（版本未变，正常进入业务）。

### 5.4 下载到备用分区

```cpp
bool ota_write_firmware_chunk(uint32_t offset, const uint8_t *data, uint16_t len) {
    static bool sector_erased = false;
    uint32_t target = (meta.active_slot == 0) ? APP2_START : APP1_START;

    if (!sector_erased) {
        flash_unlock();
        flash_erase_sector(target);  // 擦除整扇区 128KB
        sector_erased = true;
    }

    flash_write(target + offset, data, len);
    return true;
}
```

### 5.5 Modbus 新增寄存器（状态监控 + 手动触发备用）

```c
REG_OTA_STATUS    = 30,   // [只读] 0=空闲, 1=检查中, 2=下载中, 3=成功, 4=失败
REG_OTA_PROGRESS  = 31,   // [只读] 0-100 百分比
REG_OTA_ERROR     = 32,   // [只读] 错误码: 0=无, 1=网络, 2=签名, 3=Flash, 4=超时
REG_OTA_VERSION   = 33,   // [只读] 当前固件版本号 (如 40501)
REG_OTA_TRIGGER   = 34,   // [读写] 写 1 手动触发检查
```

---

## 六、ESP32 端协议

**ESP32 源码目录**: `D:\Apro\master\esp\esp32test`

### 6.1 硬件连接（无新增引脚）

复用当前 SPI2 连接，不增加额外 GPIO：

- STM32 通过 Modbus 寄存器 `REG_OTA_TRIGGER=1` 或上电自动触发
- ESP32 通过 SPI 传感器帧中的 **status 字段** 获取 OTA 指令
- 或：将 `REG_ESP32_STATUS` 中新增 bit4 = OTA 请求

**更简单方案——利用当前 SPI 帧的 report 字节**：

当前传感器帧 14 字节中有 1 字节 `running_frame_count`（bit2-7 of REG_ESP32_STATUS）。新增 1 字节**命令码**：

```
正常传感器帧 (ESP32→STM32):
  [0xAA][0x55][0x01][temp:2][humi:2][co2:2][nh3:2][lux:2][cmd:1][crc8]  15 bytes

cmd 字节定义:
  0x00 = 仅传感器数据（兼容旧版）
  0x01 = 检查更新请求 (STM32→ESP32, 通过 MISO 返回)
  0x02 = 下载数据帧
  0x03 = 下载完成
  0x04 = 下载失败
```

### 6.2 OTA 下载 SPI 帧（ESP32 → STM32）

```
OTA 握手帧:
  [0xAA][0x55][0xF0][total_chunks:2][file_size:4][version:4][signature:32]
  → 共 45 字节，分多次 SPI 传输

OTA 数据帧:
  [0xAA][0x55][0xF1][chunk_idx:2][len:2][data:N][crc16:2]
  每帧数据 ≤ 256 bytes

OTA 结果帧:
  [0xAA][0x55][0xF2][status:1]
  status: 0x00=成功, 0x01=失败
```

### 6.3 STM32 → ESP32 命令（通过 SPI MISO 响应字节）

ESP32 每次发送传感器帧时，STM32 在 MISO 上同时返回命令码：

```
MISO 响应字节:
  0x00 = 无命令（正常传感器模式）
  0x10 = 请求 OTA 开始 (URL 预先硬编码在 ESP32 中)
  0x20 = 取消 OTA
  0x30 = 请求状态查询
```

### 6.4 ESP32 端逻辑

```
loop:
  正常模式:
    每 2 秒采集传感器 → SPI 发送传感器帧
    同时读 MISO 字节
      若 = 0x10 (OTA 请求):
        连接 WiFi
        HTTP GET /latest.txt → 比较版本
        若需更新:
          HTTP GET /firmware.bin → 缓存
          HTTP GET /firmware.sig → 缓存
          SPI 发送握手帧
          SPI 分块发送数据帧
          SPI 发送结果帧
        否则 SPI 返回 "无需更新" 状态
      否则:
        继续正常模式
```

---

## 七、服务器端

### 7.1 签名生成

```python
# server/sign.py
import hmac, hashlib, sys

KEY = b'your-32-byte-secret-key-here!!'  # 32 bytes, 与 Bootloader 一致

with open(sys.argv[1], 'rb') as f:
    data = f.read()
if len(data) % 4 != 0:
    data += b'\x00' * (4 - len(data) % 4)

sig = hmac.new(KEY, data, hashlib.sha256).digest()

with open(sys.argv[1] + '.sig', 'wb') as f:
    f.write(sig)

print(f"Version: {sys.argv[2]}")
print(f"Size: {len(data)} bytes")
print(f"Signature: {sig.hex()}")
```

### 7.2 HTTP 服务

ESP32 通过 HTTP GET 访问：

```
GET /ota/latest.txt          → 返回 "40502"
GET /ota/v40502/firmware.bin → 返回固件二进制
GET /ota/v40502/firmware.sig → 返回 32 字节 HMAC 签名
```

### 7.3 密钥管理

- 32 字节密钥硬编码在 Bootloader 源码和 `sign.py` 中
- STM32F4 开启 RDP Level 1：禁止 JTAG/SWD 读取 Flash
- 密钥泄露风险：源码泄露。企业内网部署可接受。

---

## 八、PlatformIO 编译配置（实际）

```ini
; ============================ Bootloader ============================
[env:bootloader]
platform = ststm32
board = genericSTM32F407VET6
framework = stm32cube
board_build.f_cpu = 168000000L
board_build.ldscript = src/bootloader/bootloader.ld
build_flags =
    -D BOOTLOADER
    -D STM32F407xx
    -I src/bootloader
    -I src/bootloader/shared
build_src_filter = -<*> +<bootloader/>
debug_tool = stlink
upload_protocol = stlink

; ============================ APP (业务代码) ============================
[env:app]
platform = ststm32
board = genericSTM32F407VET6
framework = arduino
board_build.f_cpu = 168000000L
board_build.ldscript = src/bootloader/app_offset.ld
lib_deps =
    stm32duino/STM32duino FreeRTOS@^10.3.2
    epsilonrt/Modbus-Serial@^2.0.5
    epsilonrt/Modbus-Ethernet@^1.0.3
    bblanchon/ArduinoJson@^7.0.4
debug_tool = stlink
upload_protocol = stlink
monitor_speed = 115200
build_flags =
    -D APP_PARTITION=1
    -D TCP_KEEP_ALIVE
    -Wl,-u,_printf_float
    -I src/bsp
    -I src/drivers
    -I src/modules
    -I src/modules/modbus
    -I src/modules/shared
    -I src/app
    -I src/bootloader/shared
build_src_filter =
    +<*>
    -<bootloader/main.c>
    -<bootloader/flash_drv.c>
    -<bootloader/flash_drv.h>
    -<bootloader/metadata.c>
    -<bootloader/metadata.h>
    -<bootloader/hmac_sha256.c>
    -<bootloader/hmac_sha256.h>
    -<bootloader/sha256.c>
    -<bootloader/sha256.h>
    -<bootloader/bootloader.ld>
    -<bootloader/app_offset.ld>
extra_scripts = Resource/extra_script.py
```

> **注意**: `board_build.offset = 0x20000` 在 Arduino STM32 框架中不生效，改用自定义链接脚本 `app_offset.ld`。详见 `docs/debugging-report.md`。

---

## 九、实施状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| **P1** Bootloader 框架 | Flash 驱动 + 元数据读写 + 最小跳转 | ✅ 完成 |
| **P2** HMAC + 回滚 | SHA256/HMAC 集成 + 签名校验 + try_count 逻辑 | ✅ 完成（HMAC 禁用待启用） |
| **P3** APP 适配 | VTOR + app_offset.ld + ota_confirm | ✅ 完成 |
| **P4** OTA 模块 | SPI OTA 帧接收 + Flash 写入 + 元数据更新 | ✅ 完成 |
| **P5** OTA 触发 | Modbus 触发 + ESP32 MISO 命令 + 自动检查 | ✅ 完成 |
| **P6** ESP32 协议 | HTTP 下载 + SPI OTA Master 端 + MISO 命令检测 | ✅ 完成 |
| **P7** 服务器 | HTTP 文件服务 + sign.py | ✅ 完成 |
| **P8** 测试验证 | 正常/断电/签名错误/回滚/超时 | 🔲 待测试 |
| **P9** 生产安全 | HMAC 启用 + RDP Level 1 | 🔲 待发布前 |

### 9.1 关键实现文件

| 文件 | 作用 |
|------|------|
| `src/bootloader/main.c` | Bootloader 主逻辑：HAL_Init → 元数据 → HMAC → IWDG → 跳转 |
| `src/bootloader/bootloader.ld` | Bootloader 链接脚本 (48KB @ 0x08000000) |
| `src/bootloader/app_offset.ld` | APP 链接脚本 (384KB @ 0x08020000) |
| `src/bootloader/shared/boot_config.h` | Boot/APP 共享配置 (分区、元数据结构、密钥) |
| `src/bootloader/flash_drv.c` | Flash HAL 封装 (解锁/擦除/读/写) |
| `src/bootloader/metadata.c` | 元数据读写 |
| `src/bootloader/hmac_sha256.c` | HMAC-SHA256 签名校验 |
| `src/app/ota.cpp` | APP 端 OTA：SPI 帧处理、Flash 写、元数据管理、Modbus 同步 |
| `src/app/main.cpp` | APP 入口：VTOR 重定位 → 自动检查 → 确认启动 |
| `src/modules/esp32.cpp` | ESP32 SPI 驱动 + MISO 命令响应 |
| `server/sign.py` | HMAC-SHA256 签名工具 |
| `server/ota_server.py` | 本地 HTTP 测试服务器 |
| `server/version.txt` | 当前最新固件版本号 |
| `server/start.bat` | 一键启动（复制 firmware + 签名 + HTTP 服务） |
| `D:\Apro\master\esp\esp32test\src\main.cpp` | ESP32 OTA Master 端完整实现 |

### 9.2 启动流程（完整）

```
上电 Reset
  │
  ▼
Bootloader SystemInit() → PLL 168MHz
  │
  ├── HAL_Init()
  ├── system_init()         禁用 WWDG, 配置 DBGMCU 调试冻结
  ├── metadata_read()       读元数据 (0x0800C000)
  │     └── magic 无效 → 初始化出厂默认 → jump_to_app(APP1)
  ├── verify_firmware()     HMAC-SHA256 校验 (当前禁用, 信任所有)
  │     └── 不通过 → try_count++ → 超限切换分区 → NVIC_SystemReset()
  ├── 清除 boot_success
  ├── IWDG_Init_30s()       启动 30s 看门狗
  └── jump_to_app()
        │
        ▼
APP Reset_Handler → SystemInit() → Arduino init()
  │
  ├── setup()
  │     ├── SCB->VTOR = 0x08020000
  │     ├── 外设初始化 (串口、GPIO、Modbus、ESP32、LCD...)
  │     ├── ota_register_spi_callback()
  │     ├── ota_check_version()        检查服务器新版本
  │     └── ota_confirm_success()      写 boot_success=0xAA ← 关键!
  ├── FreeRTOS 任务创建 + 调度
  │     ├── WDT 任务 (喂 IWDG)
  │     ├── ModRTU 任务
  │     ├── Main 任务 (1Hz 同步 OTA 状态到 Modbus)
  │     ├── ESP32 任务 (轮询 SPI 帧)
  │     └── ...
  └── 正常运行
```

### 9.3 OTA 升级流程

```
正常运行时:
  ┌─ Modbus REG_OTA_TRIGGER 写 1
  │  ──────────── 或 ────────────
  └─ 上电自动 ota_check_version()
        │
        ▼
  STM32 通过 SPI MISO 发送 ESP32_CMD_OTA_START (0x10)
        │
        ▼
  ESP32 收到命令 → WiFi 连接 → HTTP GET /latest.txt → 比较版本
        │
        ├── 版本相同 → MISO 返回空闲 → 继续正常运行
        │
        └── 有新版本 → 下载 firmware.bin + firmware.sig
              │
              ▼
            SPI 发送 OTA 握手帧 (0xF0)
              → STM32 擦除备用分区, 写入元数据
              │
              ▼
            SPI 逐块发送 OTA 数据帧 (0xF1)
              → STM32 写入 Flash, 更新进度
              │
              ▼
            SPI 发送 OTA 结果帧 (0xF2, status=0x00)
              → STM32 标记成功 → NVIC_SystemReset()
              │
              ▼
            Bootloader 校验新分区 → 通过 → 启动新 APP
            → 新 APP setup() → ota_confirm_success() → 正常
```

---

## 十、已知限制与后续工作

| 项目 | 说明 |
|------|------|
| HMAC 校验 | 当前 `#if 0` 禁用。需在发布前启用 (`verify_firmware_signature`) |
| ESP32 OTA Master 端 | 需实现 WiFi + HTTP 下载 + SPI OTA 帧发送 |
| 服务器端 | 需部署 HTTP 服务 + Python sign.py 签名脚本 |
| OTA 超时 | 当前 OTA 下载期间依赖 WDT 任务喂狗，若下载 > 30s 且 WDT 未启动则复位 |
| RDP Level 1 | 发布前应开启 Flash 读保护，防止 SWD 读取密钥 |
| 分区擦除大小 | 当前擦除单扇区 (128KB)，不足需扩展多扇区擦除 |

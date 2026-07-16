# Remote I/O Master — 畜禽养殖环境物联网控制器

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESPRESSIF%20%7C%20STSTM32-blue)](https://platformio.org/)
[![MCU](https://img.shields.io/badge/MCU-STM32F407VET6%20%2B%20ESP32--C6-green)](#)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20v10.3.2-orange)](#)
[![Firmware](https://img.shields.io/badge/Firmware-v4.05.13-brightgreen)](#)
[![License](https://img.shields.io/badge/License-MIT-lightgrey)](LICENSE)

基于 **STM32F407 + ESP32-C6 双 MCU 异构架构** 的工业物联网远程 I/O 控制器，用于畜禽养殖环境监测与自动化控制。支持传感器数据采集、8 路继电器控制、Modbus RTU 工业协议、双屏人机交互，通过 ESP32 对接华为云 IoTDA 实现远程监控与 OTA 固件升级。

---

## 系统架构

```
                    Huawei Cloud IoTDA
                           │
                    MQTT over TLS (8883)
                           │
                   ┌───────▼────────┐
                   │   ESP32-C6     │
                   │  WiFi / MQTT   │
                   │  SPI Master    │
                   └───────┬────────┘
                           │ SPI 1MHz MODE0
                   ┌───────▼────────┐
                   │ STM32F407VET6  │
                   │  FreeRTOS      │
                   │  SPI Slave     │
                   └───┬───┬───┬───┘
                       │   │   │
              RS485    │   │   └── SPI1 → ST7789 LCD (240×240)
            Modbus RTU │   └──── UART → TJC HMI 串口触摸屏
             (从机)    └──────── GPIO → 8路继电器 + LED
```

---

## 硬件规格

| 项目 | 参数 |
|------|------|
| 主控 MCU | STM32F407VET6 (ARM Cortex-M4F, 168MHz, 512KB Flash, 128KB SRAM) |
| 协处理器 | ESP32-C6 (RISC-V, 160MHz, 2MB Flash, 320KB SRAM) |
| 通信接口 | RS485 (Modbus RTU), SPI (MCU 间), UART (HMI 串口屏) |
| 显示 | ST7789 TFT LCD (240×240) + TJC VT 系列串口触摸屏 |
| 输出控制 | 8 路继电器 (灌电流, 低电平有效) |
| 看门狗 | IWDG 独立看门狗 (2s 超时) |
| 调试接口 | SWD (ST-Link) + USART2 调试串口 (115200) |

---

## 软件技术栈

### STM32 端 (`src/`)

| 层级 | 技术 |
|------|------|
| RTOS | FreeRTOS v10.3.2，7 并发任务，多优先级调度 |
| 构建框架 | Arduino Core (APP) + STM32Cube HAL (Bootloader) |
| 工业协议 | Modbus RTU 从站 (35 寄存器表), Modbus TCP 预留 |
| SPI 协议 | 自定义帧协议 — 工业级双向握手 + CRC8 + 心跳保活 |
| OTA 升级 | A/B 双槽分区 + HMAC-SHA256 签名校验 + 自动回滚 |
| 显示驱动 | ST7789 LCD (SPI) + TJC HMI (UART 二进制协议) |
| 存储 | 内部 Flash EEPROM 仿真，参数掉电保存 |
| 看门狗 | IWDG 最高优先级任务喂狗 |

### ESP32 端 (`esp/esp32test/`)

| 层级 | 技术 |
|------|------|
| 底层 SDK | ESP-IDF v5.4.1 |
| 构建框架 | Arduino Core |
| TCP/IP 栈 | LWIP (DHCP / DNS / SNTP) |
| TLS 加密 | mbedTLS 硬件加速 (AES / SHA / ECC) |
| 云平台 | 华为云 IoTDA, MQTT over TLS (8883) |
| JSON 解析 | ArduinoJson v7 |
| OTA 下载 | HTTP Client 下载 firmware.bin |

### 开发工具链

| 工具 | 用途 |
|------|------|
| [PlatformIO](https://platformio.org/) | 构建系统，双环境管理 |
| arm-none-eabi-gcc | STM32 编译 |
| riscv32-esp-elf-gcc | ESP32-C6 编译 |
| ST-Link / CMSIS-DAP | STM32 烧录调试 |
| esptool.py | ESP32 烧录 |
| Python | OTA 上传脚本 / HMAC-SHA256 签名 |

---

## 目录结构

```
remote-io-master/
├── platformio.ini              # PlatformIO 构建配置 (bootloader + app)
├── src/
│   ├── app/                    # 应用层
│   │   ├── main.cpp            # 系统入口, FreeRTOS 初始化
│   │   ├── tasks.cpp           # 7 个 FreeRTOS 任务定义
│   │   ├── param.cpp / .h      # 参数管理 (EEPROM 仿真)
│   │   └── ota.cpp / .h        # OTA 固件升级 (STM32 接收侧)
│   ├── modules/                # 功能模块
│   │   ├── esp32.cpp / .h      # SPI 从机 + 工业握手协议
│   │   ├── modbus/             # Modbus RTU/TCP 协议栈
│   │   │   ├── modbus_core.cpp # 寄存器表共享逻辑
│   │   │   ├── modbus_rtu.cpp  # RS485 从站
│   │   │   └── modbus_tcp.cpp  # TCP 桥接 (占位)
│   │   ├── lcd.cpp / .h        # ST7789 LCD 驱动 + 字体
│   │   ├── hmi.cpp / .h        # TJC 串口屏双向协议
│   │   ├── g4g.cpp / .h        # 4G 模块 (预留)
│   │   ├── k210.cpp / .h       # K210 相机 (预留)
│   │   └── shared/             # 模块间共享类型
│   ├── drivers/                # 硬件驱动层
│   │   ├── relay.cpp / .h      # 8 路继电器控制
│   │   └── led.cpp / .h        # LED 指示
│   ├── bsp/                    # 板级支持包
│   │   ├── bsp_config.h        # 引脚定义, Modbus 寄存器枚举
│   │   ├── bsp_init.h / .cpp   # GPIO/时钟初始化
│   │   └── bsp_debug.h         # 调试宏 (DBG/DBG_FMT)
│   └── bootloader/             # OTA Bootloader (裸机 C)
│       ├── main.c              # 启动引导逻辑
│       ├── bootloader.ld       # 链接脚本 (48KB @ 0x08000000)
│       ├── app_offset.ld       # APP 偏移链接脚本
│       ├── flash_drv.c         # Flash 驱动
│       ├── metadata.c          # OTA 元数据管理
│       ├── hmac_sha256.c       # 固件签名校验
│       └── shared/             # Bootloader 与 APP 共享配置
│           └── boot_config.h   # 密钥, 分区地址, 版本号
├── esp/                         # ESP32-C6 协处理器
│   └── esp32test/
│       ├── platformio.ini       # ESP32 构建配置
│       ├── src/main.cpp         # WiFi / MQTT / SPI Master
│       └── sdkconfig.esp32c6    # ESP-IDF SDK 配置
└── Resource/
    └── extra_script.py         # 构建后处理 (生成 .hex)
```

---

## 快速开始

### 前置要求

- [PlatformIO IDE](https://platformio.org/install) (VS Code 插件) 或 PlatformIO Core CLI
- ST-Link 调试器
- Windows / Linux / macOS

### 编译与烧录

```bash
# 1. 首次烧录 Bootloader (仅需一次)
pio run -e bootloader --target upload

# 2. 日常开发烧录 APP
pio run -e app --target upload

# 3. 查看串口输出
pio device monitor -b 115200
```

### OTA 固件升级

1. ESP32 上电自动连接 Wi-Fi → 对接华为云 IoTDA
2. 定期检查 HTTP 服务器 (`http://10.168.81.18:8080/firmware.bin`) 是否有新固件
3. 自动下载 → SPI 512 字节分块传输 → STM32 写入 APP2 分区
4. HMAC-SHA256 校验通过 → Bootloader 将 APP2 复制到 APP1 → 重启

也可通过 Modbus 寄存器 `REG_OTA_TRIGGER` (地址 34) 手动触发升级。

---

## Modbus 寄存器表 (部分)

| 地址 | 名称 | 类型 | 说明 |
|------|------|------|------|
| 0 | REG_VERSION | RO | 固件版本号 |
| 3 | REG_COMMAND | RW | 命令: 10=保存, 20=重载, 30=重启, 66=恢复出厂 |
| 10 | REG_UPTIME0 | RO | 系统运行时间 (秒, 低16位) |
| 16 | REG_TEMP_X100 | RO | 温度 (×100, °C) |
| 17 | REG_HUMI_X100 | RO | 湿度 (×100, %) |
| 18 | REG_CO2 | RO | CO2 浓度 (ppm) |
| 19 | REG_NH3_X100 | RO | 氨气浓度 (×100, ppm) |
| 20 | REG_LUX_X100 | RO | 光照度 (×100) |
| 30 | REG_OTA_STATUS | RO | OTA 状态: 0=空闲, 1=检查, 2=下载, 3=成功, 4=失败 |
| 34 | REG_OTA_TRIGGER | RW | OTA 触发: 写入 1 开始升级 |

完整 35 寄存器定义见 [bsp_config.h](src/bsp/bsp_config.h)。

---

## SPI 握手协议

```
ESP32 (Master)                          STM32 (Slave)
     │                                       │
     │  [AA][55][FD][seq][crc]               │
     │  ───────── HANDSHAKE_REQ ──────────►  │  HS_INIT → HS_RESPONSE
     │                                       │
     │        MISO 9B 响应帧循环              │
     │  ◄─── [BB][66][state][ver:4B]... ────  │
     │                                       │
     │  [AA][55][FE][seq][crc]               │
     │  ───────── HANDSHAKE_ACK ───────────► │  HS_RESPONSE → HS_READY
     │                                       │
     │       双工数据交换 (传感器 / OTA)       │
     │  ◄══════════════════════════════════► │
     │                                       │
     │  [AA][55][FC][seq][crc]  每 10s       │
     │  ───────── HEARTBEAT ───────────────► │  更新心跳时间戳
```

- 帧结构: `[AA][55][type][payload:N][CRC8]`
- CRC8 = 载荷字节累加和取低 8 位
- 5 秒超时，最大 3 次重试
- 30 秒无心跳断连自动重握手

---

## 屏幕截图

> 待补充 ST7789 LCD 及 TJC HMI 串口屏实拍照片

---

## License

MIT License

---

## 联系方式

- GitHub: [@AsaKing-gery](https://github.com/AsaKing-gery)
- 仓库: [modbusstudy](https://github.com/AsaKing-gery/modbusstudy)

# 项目技术栈总结 — 嵌入式软件工程师简历

## 项目名称

**《基于 STM32 + ESP32 双核架构的畜禽养殖环境物联网远程 I/O 控制器》**

---

## 一、硬件平台

| 项目 | 型号 | 核心 | 主频 | 片上资源 |
|------|------|------|------|----------|
| 主控 MCU | STM32F407VET6 | ARM Cortex-M4F | 168 MHz | 512KB Flash / 128KB SRAM + 64KB CCMRAM |
| 协处理器 | ESP32-C6 | RISC-V 单核 | 160 MHz | 2MB Flash / 320KB SRAM |

---

## 二、嵌入式软件技术栈

### 2.1 实时操作系统（RTOS）

- **FreeRTOS v10.3.2**（ARM Cortex-M4F 移植）
- 7 个并发任务：看门狗喂狗、Modbus RTU 从机、HMI 串口屏通信、SPI 从机帧处理、主业务逻辑、ST7789 LCD 渲染、Modbus TCP
- 多优先级调度（优先级 2~6），互斥量（RS485 总线锁），任务通知（ISR → Task），堆栈溢出检测 Hook
- 内存管理：heap_4.c

### 2.2 工业通信协议

| 协议 | 角色 | 物理层 | 速率 | 说明 |
|------|------|--------|------|------|
| **Modbus RTU** | 从机 | RS485（USART1 + DE 收发控制） | 115200 | 35 个保持寄存器，支持传感器数据、参数配置、OTA 控制 |
| **SPI 自定义帧协议** | STM32 从机 / ESP32 主机 | SPI2，4 线硬件 NSS | 1 MHz MODE0 | 700 字节帧，CRC8 校验，支持传感器帧、OTA 握手/数据/结果帧、心跳帧、握手请求/确认帧 |
| **MQTT** | 客户端 | Wi-Fi / TLS 1.2 | -- | 连接华为云 IoTDA，属性上报 + 命令下发，心跳保活 |

### 2.3 工业级 SPI 握手协议（自主设计）

- **4 状态握手状态机**：INIT → REQ_SENT → GOT_RESPONSE → READY
- **帧结构**：`[AA][55][type][payload:N][crc8]`，CRC8 = 字节累加和取低 8 位
- **心跳机制**：10 秒定时心跳，30 秒超时断连自动重握手
- **超时重试**：5 秒超时，最大 3 次重试，支持异常恢复
- **双向握手验证**：ESP32 发 HANDSHAKE_REQ（0xFD）→ STM32 MISO 返回 9 字节响应（含固件版本、启动状态、CRC）→ ESP32 发 HANDSHAKE_ACK（0xFE）

### 2.4 自定义 OTA 固件升级

**Bootloader 端（裸机 C，48KB）：**
- 双槽 A/B 分区（APP1@0x08020000 + APP2@0x08040000，各 128KB）
- 带元数据存储区（16KB），记录启动成功标志、重试次数、当前活动槽
- HMAC-SHA256 固件签名校验
- 回滚机制：最大 3 次启动失败自动切备用槽
- 工厂兜底：双槽均失败时回退 APP1
- IWDG 30 秒启动看门狗，安全跳转前清理 NVIC、SysTick、MSP/PC

**APP 端（应用侧）：**
- ESP32 通过 HTTP 下载 firmware.bin + 签名 → SPI 512 字节分块传输 → STM32 写入 APP2 → 校验 → 触发 Bootloader 搬运
- Modbus 寄存器暴露 OTA 状态（空闲/检查/下载中/成功/失败）、进度百分比、错误码
- 支持上电自动检查、Modbus 寄存器触发、ESP32 定时轮询三种触发方式

### 2.5 人机交互（HMI）

| 设备 | 接口 | 分辨率 | 说明 |
|------|------|--------|------|
| **ST7789 TFT LCD** | SPI1（仅写模式） | 240×240 | 实时显示传感器数值、继电器状态、系统信息，含中文字库 |
| **TJC 串口触摸屏** | UART（SoftwareSerial） | VT 系列 | 自定义二进制协议，双向交互控制 |

### 2.6 物联网云端对接

- **华为云 IoTDA**（物联网设备接入），区域 cn-east-3
- MQTT over TLS（端口 8883），mbedTLS 硬件加速（AES/SHA/ECC）
- 设备属性上报（JSON 格式，温度/湿度/CO2/NH3/光照）
- 云端命令下发（订阅 `sys/commands/#`）
- LWIP TCP/IP 协议栈（DHCP、DNS、SNTP）

### 2.7 传感器数据处理

- 5 路传感器数据采集：温度、湿度、CO2、氨气（NH3）、光照度
- 模拟量定点放大（×100 转整数），Modbus 寄存器上报
- 传感器阈值告警（高/低限各 4 通道），自动继电器联动控制

### 2.8 参数管理与存储

- AT24Cxx EEPROM 仿真（内部 Flash 模拟），42 字节参数布局
- 支持 Modbus 命令：保存（10）、重载（20）、重启（30）、恢复出厂（66）
- MAC 地址由 MCU 96 位唯一 ID 派生

---

## 三、开发工具链

| 类别 | 技术 |
|------|------|
| 构建系统 | **PlatformIO**（多环境：bootloader + app） |
| STM32 框架 | **Arduino Core**（app）+ **STM32Cube HAL**（bootloader 裸机） |
| ESP32 框架 | **Arduino Core** + **ESP-IDF v5.4.1** 底层 SDK |
| 编译工具链 | arm-none-eabi-gcc（STM32） / riscv32-esp-elf-gcc（ESP32-C6） |
| 调试器 | ST-Link / CMSIS-DAP（SWD），ESP JTAG |
| 烧录工具 | esptool.py（ESP32），OpenOCD（STM32） |
| 代码格式化 | Google Style，4 空格缩进，120 列宽，clang-format |
| 固件签名 | Python sign.py（HMAC-SHA256） |
| 辅助脚本 | Python OTA 上传脚本，固件打包脚本 |

---

## 四、核心驱动与外设

| 外设 | 数量 | 说明 |
|------|------|------|
| GPIO 继电器 | 8 路 | 风机、水泵等设备控制 |
| LED 指示灯 | 2 路 | 运行指示 + 故障指示 |
| IWDG 独立看门狗 | 1 路 | 2 秒超时，最高优先级任务喂狗 |

---

## 五、软件架构

```
应用层 (app/)
  ├── main.cpp          主入口，FreeRTOS 任务创建
  ├── tasks.cpp         7 个任务定义，WDT 看门狗
  ├── param.cpp         参数管理，EEPROM 仿真
  └── ota.cpp           OTA 升级状态机

模块层 (modules/)
  ├── esp32.cpp         SPI 从机 + 工业握手 + DMA 收发
  ├── modbus/           Modbus RTU/TCP 协议栈
  ├── lcd.cpp           ST7789 LCD 驱动 + 字体 + 图片
  ├── hmi.cpp           TJC 串口屏协议
  ├── g4g.cpp           4G 模块（预留）
  └── k210.cpp          K210 相机（预留）

驱动层 (drivers/)
  ├── relay.cpp         8 路继电器
  └── led.cpp           LED 控制

板级支持 (bsp/)
  ├── bsp_config.h      引脚定义，寄存器映射，Modbus 地址
  └── bsp_debug.h       调试串口宏（DBG/DBG_FMT）

Bootloader (bootloader/)
  ├── main.c            裸机 OTA 引导程序
  └── shared/
      └── boot_config.h OTA 签名密钥，分区地址定义
```

---

## 六、精选技术亮点（简历一句话版）

- 基于 **FreeRTOS** 实现 **7 任务多优先级调度**，任务间通过互斥量、任务通知通信
- 设计并实现 **工业级 SPI 双向握手协议**，含 CRC8 校验、心跳保活、超时重试、异常自恢复
- 实现 **自定义 OTA Bootloader**：A/B 双槽分区、HMAC-SHA256 签名校验、3 次失败自动回滚、工厂兜底
- **双 MCU 异构架构**：ESP32-C6 负责云连接（MQTT/TLS/Wi-Fi），STM32F407 负责实时控制（Modbus/继电器/传感器）
- **Modbus RTU 工业协议**：35 寄存器表，支持参数读写、传感器上报、OTA 远程控制
- 对接 **华为云 IoTDA** 平台：MQTT over TLS，设备属性上报，云端命令下发
- OTA 全链路：云端 → HTTP 下载 → ESP32 → SPI 分块传输 → STM32 写入 → Bootloader 校验搬运

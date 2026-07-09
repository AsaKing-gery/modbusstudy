# RemoteIO Master V40501

基于 STM32F407VET6 的远程 IO 主控板，集成传感器采集、HMI 串口屏、Modbus 通信和 LoRa 无线功能。

![图片](/Resource/Image/1.jpg)

---

## 更新日志

### V40501 (当前版本)
- MCU 升级为 **STM32F407VET6**（天空星，168MHz），替代原 STM32F103C8T6
- 新增 **HMI 淘晶驰串口屏** 通信（USART4，自定义帧协议）
- 新增 **传感器状态机**：轮询读取温湿度、CO2、NH3 传感器（RS485 Modbus）
- 新增 **ADS1115** 16位 ADC 采集（4路 AI，I2C 接口）
- 新增 **LoRa SX1262** 无线通信支持（可选）
- 新增 **MQTT/TLS** 物联网接入支持（可选）
- 新增 **K210 摄像头** SPI 通信支持（可选）
- 新增 **ESP32C6 WiFi** 协处理器支持（可选）
- 输出扩展至 **10 路 DO（Y0-Y9）**，其中 Y2-Y9 用于风机/加湿器控制

---

## 硬件规格

| 项目          | 参数                          |
| ------------- | ----------------------------- |
| 主控芯片      | STM32F407VET6，168MHz         |
| 数字输入      | 8 路全隔离 DI（X0-X7），DC24V，兼容 NPN/PNP |
| 数字输出      | 10 路 DO（Y0-Y9），含 NPN 和继电器输出   |
| 模拟输入      | 4 路 AI（0-5V），16位 ADS1115 ADC       |
| RS485         | 1 路，支持 ModbusRTU + 传感器轮询       |
| 以太网        | 1 路，支持 ModbusTCP（可选）             |
| HMI 串口      | USART4（TTL→RS232→淘晶驰串口屏）         |
| LoRa          | SX1262 模块，SPI2 接口（可选）           |
| 供电          | DC 9-24V                                 |
| 开发环境      | PlatformIO + Arduino 框架                |
| 软件架构      | FreeRTOS，看门狗监控                      |

![接口](/Resource/Image/2.png)

---

## 引脚分配

### 通信接口

| 功能       | 引脚    | 说明                      |
| ---------- | ------- | ------------------------- |
| RS485      | PB10/PB11 | RX/TX，ModbusRTU + 传感器 |
| RS485_EN   | PB1     | 方向控制                  |
| 以太网     | SPI1    | W5500/W5100               |
| HMI 串口   | PA0/PA1 | USART4 TX/RX → 串口屏     |
| LoRa       | PB12-PB15, PC0, PD2 | SPI2, SX1262    |
| 调试串口   | PD5/PD6 | UART2，115200 打印输出    |

### 输入输出

| GPIO | 功能 | | GPIO | 功能 |
|------|------|-|------|------|
| PD10 | DI X0 | | PA15 | DO Y0 |
| PD11 | DI X1 | | PB3  | DO Y1 |
| PD12 | DI X2 | | PB4  | DO Y2（加湿器1）|
| PD13 | DI X3 | | PB5  | DO Y3（加湿器2）|
| PA8  | DI X4 | | PB6  | DO Y4（加湿器3）|
| PB0  | DI X5 | | PB7  | DO Y5（加湿器4）|
| PE6  | DI X6 | | PE10 | DO Y6（风机1）  |
| PE4  | DI X7 | | PE11 | DO Y7（风机2）  |
|      |       | | PE12 | DO Y8（风机3）  |
|      |       | | PE13 | DO Y9（风机4）  |

---

## 程序结构

```
src/
├── main.cpp              # 入口：系统初始化、创建任务、启动调度器
├── globals.cpp           # 全局变量定义
├── myTask.cpp            # 任务实现：看门狗、主任务、AD采集、任务创建
├── myModbus.cpp          # ModbusRTU/TCP 协议栈
├── mySensorTask.cpp      # 传感器状态机 + HMI串口屏通信
├── myADS1115.cpp         # ADS1115 ADC 驱动
├── myLoRaTask.cpp        # LoRa 收发任务（可选）
├── myMQTT_TLS.cpp        # MQTT/TLS 客户端（可选）
├── myNetworkConfig.cpp   # 以太网 DHCP/静态IP（可选）
├── myK210.cpp            # K210 摄像头通信（可选）
├── myESP32C6.cpp         # ESP32C6 SPI 协处理器（可选）
├── IO_Setting.cpp        # GPIO 初始化与输入滤波
└── Parameter_Config.cpp  # 参数存储（EEPROM）

include/
├── IO_Setting.h          # 引脚定义
├── Parameter_Config.h    # 参数配置结构体
├── myModbus.h            # Modbus 接口
├── mySensorTask.h        # 传感器与 HMI 协议常量
├── myTask.h              # 任务声明
├── myShowMsg.h           # 调试打印
└── ...
```

### FreeRTOS 任务列表

| 任务名称              | 优先级 | 功能                             |
| --------------------- | ------ | -------------------------------- |
| WatchdogTask          | 6      | 独立看门狗喂狗                   |
| X_filter              | 5      | 数字输入滤波（默认 5ms）         |
| ModbusRTUTask         | 5      | ModbusRTU 从站服务               |
| IICTask               | 3      | ADS1115 ADC 周期性读取           |
| MainTask              | 3      | 主循环：参数管理、IO 刷新        |
| HMIReceiveTask        | 3      | 串口屏命令接收（逐字节解析）     |
| SensorStateMachineTask| 2      | 传感器轮询状态机 + 数据发送至HMI |

---

## HMI 串口屏通信协议

### STM32 → 串口屏（传感器数据）

| 标志 | 数据类型 | 帧格式                    |
| ---- | -------- | ------------------------- |
| 0x03 | 温度     | `[0x03][值][0x03]`        |
| 0x04 | 湿度     | `[0x04][值][0x04]`        |
| 0x05 | CO2      | `[0x05][值][0x05]`        |
| 0x06 | NH3      | `[0x06][值][0x06]`        |

### 串口屏 → STM32（控制命令）

| 命令头    | 帧长度 | 功能                             |
| --------- | ------ | -------------------------------- |
| 0x10~0x80 | 3 字节 | 8 路设备开关（加湿器1-4/风机1-4）|
| 0x0A~0x0D | 3 字节 | 阈值设置 → Modbus Hreg 30-33     |
| 0x0E,0x0F | 4 字节 | 参数控制                         |
| 0x01      | 6 字节 | 16位数值参数                     |
| 0x02      | 4 字节 | 参数组                           |

> **VT 协议过滤**：淘晶驰内部协议帧头 `0xEE` 自动丢弃，不与自定义帧冲突。

### 设备控制位映射

| 设备码 | 输出位 | 引脚 | 对应设备 |
| ------ | ------ | ----| -------- |
| 0x10   | bit2   | PB4  | 加湿器 1 |
| 0x20   | bit3   | PB5  | 加湿器 2 |
| 0x30   | bit4   | PB6  | 加湿器 3 |
| 0x40   | bit5   | PB7  | 加湿器 4 |
| 0x50   | bit6   | PE10 | 风机 1   |
| 0x60   | bit7   | PE11 | 风机 2   |
| 0x70   | bit8   | PE12 | 风机 3   |
| 0x80   | bit9   | PE13 | 风机 4   |

---

## Modbus 寄存器

### 基本寄存器 (0-18)

| 地址 | 名称             | 读写 | 说明                                         |
| ---- | ---------------- | ---- | -------------------------------------------- |
| 0    | 固件版本         | R    | 格式：年尾号+月+日，如 40501                 |
| 1    | 当前站号         | R    | 由 DIP1-3 拨码决定，全 OFF=1                 |
| 2    | 当前波特率       | R    | 由 DIP4-5 拨码决定，全 OFF=115200            |
| 3    | 参数操作         | R/W  | 10=保存 20=加载 30=重启 66=恢复出厂          |
| 4    | 输入滤波时间     | R/W  | 1-100 ms                                     |
| 5-7  | MAC 地址         | R/W  | 上电时由 MCU ID 自动生成                     |
| 8-9  | IP 地址          | R/W  | 默认 192.168.1.168，修改后需重启             |
| 10   | 运行时间         | R    | 秒，0-65535 循环，可用于心跳检测             |
| 11   | DI 输入状态      | R    | bit0-bit7 = X0-X7                            |
| 12   | DO 输出状态      | R/W  | bit0-bit5 = Y0-Y5, bit6-bit9 = Y6-Y9         |
| 13   | 扩展输入状态     | R    | bit0-7=X10-X17, bit8-15=X20-X27              |
| 14   | 扩展输出状态     | R/W  | bit0-7=Y10-Y17, bit8-15=Y20-Y27              |
| 15-18| AI0-AI3 模拟量   | R    | 数字值，换算：电压 = 值 × 0.0001818，32767 = 故障 |

### 传感器寄存器 (25-33)

| 地址 | 名称             | 读写 | 说明                                   |
| ---- | ---------------- | ---- | -------------------------------------- |
| 25   | 温度值           | R    | 放大 10 倍，如 256 = 25.6°C           |
| 26   | 湿度值           | R    | 放大 10 倍，如 650 = 65.0%            |
| 27   | CO2 浓度         | R    | 单位 ppm，原始值                       |
| 28   | NH3 浓度         | R    | 放大 100 倍，如 123 = 1.23 ppm        |
| 29   | 传感器状态       | R    | bit0=温湿度有效, bit1=CO2有效, bit2=NH3有效 |
| 30-33| 阈值设置         | R/W  | 来自串口屏的阈值参数                   |

---

## 编译与下载

### 环境要求

- [PlatformIO IDE](https://platformio.org/)（VS Code 插件）
- ST-Link 下载器（推荐）或 USB-TTL 串口模块

### 编译

```bash
git clone git@github.com:AsaKing-gery/modbusstudy.git
cd modbusstudy
pio run
```

### 下载

**ST-Link 方式**（默认，无需切换 BOOT0）：

```ini
upload_protocol = stlink
```

**串口方式**（需将 BOOT0 置 1 后复位）：

```ini
upload_port = COM13
upload_protocol = serial
upload_speed = 115200
```

> 下载完成后务必将 BOOT0 恢复为 0。

### 串口监视

```ini
monitor_port = COM13
monitor_speed = 115200
```

默认关闭调试打印。在 `myShowMsg.h` 中取消注释 `#define UseSerialPrint` 即可开启。

---

## 依赖库

| 库名                              | 版本   | 用途               |
| --------------------------------- | ------ | ------------------ |
| stm32duino/STM32duino FreeRTOS    | ^10.3.2 | 实时操作系统     |
| epsilonrt/Modbus-Serial           | ^2.0.5  | ModbusRTU 从站   |
| epsilonrt/Modbus-Ethernet         | ^1.0.3  | ModbusTCP 从站   |
| robtillaart/ADS1X15               | ^0.4.2  | ADS1115 ADC 驱动 |
| jgromes/RadioLib                  | ^6.6.0  | SX1262 LoRa 驱动 |
| arduino-libraries/ArduinoMqttClient | ^0.1.8 | MQTT 客户端     |
| openslab-osu/SSLClient            | ^1.2.0  | TLS 加密通信     |
| bblanchon/ArduinoJson             | ^7.0.4  | JSON 解析        |

---

## 注意事项

1. 本程序供学习参考，若用于商业用途请自行承担风险。
2. ModbusRTU 和 ModbusTCP 共用寄存器数据区，已修改 Modbus 库的链表变量为 public。
3. ModbusTCP 需要将 `ModbusEthernet.h` 中的 `TCP_KEEP_ALIVE` 打开。
4. 传感器任务和 ModbusRTU 任务共用 RS485 总线，通过互斥锁保护。
5. HMI 串口屏波特率必须与 `SENSOR_BAUDRATE`（19200）一致。 

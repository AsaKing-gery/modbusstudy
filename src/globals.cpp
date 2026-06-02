#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoMqttClient.h>
#include <RadioLib.h>
#include <EEPROM.h>
#include <ADS1X15.h>
#include "IO_Setting.h"
#include "Parameter_Config.h"

/* 需要先包含myMQTT_TLS.h获取DeviceState定义和LORA_FRAME_LEN */
#include "myMQTT_TLS.h"
#include "myLoRaTask.h"
#include "mySensorTask.h"
#include "myADS1115.h"
#include "myNetworkConfig.h"
#include "myK210.h"
#include "myESP32C6.h"
#include "myTask.h"

/* ==================== IO_Setting.h 全局变量定义 ==================== */
uint8_t idSwitchState = 0;       // 站号拨码开关状态
uint8_t baudRateSwitchState = 0; // 波特率拨码开关状态
SPIClass RA01S_SPI(PB15, PB14, PB13); // SPI2: MOSI, MISO, SCK
GPIO_Port Input;
AnalogStruct myAI;

/* 设置输出模式并设置为低电平 */
void pinMode_OutSetting(uint32_t ulPin)
{
    pinMode(ulPin, OUTPUT_OPEN_DRAIN);
    digitalWrite(ulPin, HIGH);
}

/* GPIO初始化 */
void GPIO_Init()
{
    ShowMsg("GPIO_Initizing", true);
    /*拨码开关初始化*/
    pinMode(SW_B1, INPUT_PULLUP);
    pinMode(SW_B2, INPUT_PULLUP);
    pinMode(SW_B3, INPUT_PULLUP);
    pinMode(SW_B4, INPUT_PULLUP);
    pinMode(SW_B5, INPUT_PULLUP);
    /*输入引脚初始化*/
    pinMode(Temp_X0, INPUT);
    pinMode(Temp_X1, INPUT);
    pinMode(Temp_X2, INPUT);
    pinMode(Temp_X3, INPUT);
    pinMode(Temp_X4, INPUT);
    pinMode(Temp_X5, INPUT);
    pinMode(Temp_X6, INPUT);
    pinMode(Temp_X7, INPUT);
    /*输出引脚初始化*/
    pinMode_OutSetting(Output_Y0);
    pinMode_OutSetting(Output_Y1);
    pinMode_OutSetting(Output_Y2);
    pinMode_OutSetting(Output_Y3);
    pinMode_OutSetting(Output_Y4);
    pinMode_OutSetting(Output_Y5);
    pinMode_OutSetting(Output_Y6);
    pinMode_OutSetting(Output_Y7);
    pinMode_OutSetting(Output_Y8);
    pinMode_OutSetting(Output_Y9);
    /*指示灯引脚初始化*/
    pinMode_OutSetting(ERROR_LED);
    pinMode_OutSetting(RUN_LED);
    /*串口屏引脚初始化*/
    pinMode(HMI_USART_RX, INPUT);
    pinMode(HMI_USART_TX, OUTPUT);
    digitalWrite(HMI_USART_TX, HIGH);
    /*Ra-01S模块引脚初始化*/
    pinMode(RA01S_RESET, OUTPUT);
    digitalWrite(RA01S_RESET, HIGH);
    pinMode(RA01S_DIO1, INPUT_PULLUP); // 使用PD2，配置为上拉输入
    pinMode(RA01S_SPI_NSS, OUTPUT);
    digitalWrite(RA01S_SPI_NSS, HIGH);
    pinMode(RA01S_BUSY, INPUT);
    /*Ra-01S SPI2初始化*/
    RA01S_SPI.begin();
    /*K210 USART6引脚初始化*/
    pinMode(K210_USART_RX, INPUT_PULLUP);
    pinMode(K210_USART_TX, OUTPUT);
    digitalWrite(K210_USART_TX, HIGH);
    /*获取拨码开关状态*/
    idSwitchState = (!digitalRead(SW_B3) << 2) | (!digitalRead(SW_B2) << 1) | !digitalRead(SW_B1);
    baudRateSwitchState = (!digitalRead(SW_B5) << 1) | !digitalRead(SW_B4);
    myPar.SlaveId = (idSwitchState == 0 ? 1 : idSwitchState);
    switch (baudRateSwitchState)
    {
    case 0:
        myPar.Baudrate = 115200;
        break;
    case 1:
        myPar.Baudrate = 9600;
        break;
    case 2:
        myPar.Baudrate = 19200;
        break;
    case 3:
        myPar.Baudrate = 38400;
        break;
    default:
        myPar.Baudrate = 115200;
        break;
    }
    ShowMsg("GPIO_Initized", true);
}

/* 输入滤波函数 - 每1MS调用一次，用来给输入滤波,滤波时间由Input_Filter_Time指定，默认5ms */
void X_filter(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms再启动任务
    ShowMsg("X_filter task started", true);
    static uint8_t x_buffer[8];     // 刷新端口数
    static uint32_t timeRecord = 0; // 记录上一次刷新时间
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1)); // 延时1个滴答
        // 输入滤波
        if (millis() - timeRecord >= 1) // 1ms刷新一次
        {
            timeRecord = millis(); // 更新刷新时间
            (digitalRead(Temp_X0)) ? (x_buffer[0] = 0, Input.X0 = 0) : ((x_buffer[0] < myPar.Input_Filter_Time) ? (x_buffer[0]++) : (Input.X0 = 1));
            (digitalRead(Temp_X1)) ? (x_buffer[1] = 0, Input.X1 = 0) : ((x_buffer[1] < myPar.Input_Filter_Time) ? (x_buffer[1]++) : (Input.X1 = 1));
            (digitalRead(Temp_X2)) ? (x_buffer[2] = 0, Input.X2 = 0) : ((x_buffer[2] < myPar.Input_Filter_Time) ? (x_buffer[2]++) : (Input.X2 = 1));
            (digitalRead(Temp_X3)) ? (x_buffer[3] = 0, Input.X3 = 0) : ((x_buffer[3] < myPar.Input_Filter_Time) ? (x_buffer[3]++) : (Input.X3 = 1));
            (digitalRead(Temp_X4)) ? (x_buffer[4] = 0, Input.X4 = 0) : ((x_buffer[4] < myPar.Input_Filter_Time) ? (x_buffer[4]++) : (Input.X4 = 1));
            (digitalRead(Temp_X5)) ? (x_buffer[5] = 0, Input.X5 = 0) : ((x_buffer[5] < myPar.Input_Filter_Time) ? (x_buffer[5]++) : (Input.X5 = 1));
            (digitalRead(Temp_X6)) ? (x_buffer[6] = 0, Input.X6 = 0) : ((x_buffer[6] < myPar.Input_Filter_Time) ? (x_buffer[6]++) : (Input.X6 = 1));
            (digitalRead(Temp_X7)) ? (x_buffer[7] = 0, Input.X7 = 0) : ((x_buffer[7] < myPar.Input_Filter_Time) ? (x_buffer[7]++) : (Input.X7 = 1));
        }
    }
}

/* ==================== Parameter_Config.h 全局变量定义 ==================== */
Parameter_Config myPar = Parameter_Config();

/* 获取MCU唯一标识符 */
uint32_t GetMCUId(uint8_t offsetIndex)
{
    offsetIndex > 3 ? offsetIndex = 3 : 0;
    // F407 MCU唯一ID地址: 0x1FFF7A10 (F103是0x1FFFF7E8)
    return *(uint32_t *)(0x1FFF7A10 + (offsetIndex * 4));
}

/* 保存参数配置到EEPROM */
void Save_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Saving parameter", true);
    EEPROM.put(0, myPar);
    ShowMsg("Parameter Saved:" + String(millis() - recordTime), true);
}

/* 参数初始化 */
void Parameter_Init()
{
    ShowMsg("Initializing parameter", true);
    myPar = Parameter_Config();
    uint32_t id1 = GetMCUId();  // 获取MCU低32位ID
    uint32_t id2 = GetMCUId(1); // 获取MCU高32位ID
    myPar.mac[0] = id1 & 0xFF;
    myPar.mac[1] = (id1 >> 8) & 0xFF;
    myPar.mac[2] = (id1 >> 16) & 0xFF;
    myPar.mac[3] = (id1 >> 24) & 0xFF;
    myPar.mac[4] = id2 & 0xFF;
    myPar.mac[5] = (id2 >> 8) & 0xFF;
    Save_Parameter();
}

/* 从EEPROM中加载参数配置 */
void Load_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Loading parameter", true);
    EEPROM.get(0, myPar);     // 从EEPROM中读取参数配置
    if (myPar.InitFlag != 66) // 如果是第一次下载程序，myPar.InitFlag肯定不是66，则直接调用默认设置并保存到EEPROM
    {
        Parameter_Init();
    }
    ShowMsg("Parameter loaded：" + String(millis() - recordTime), true);
}

/* ==================== myModbus.h 全局变量定义 ==================== */
TRegister *_regs_head = NULL; // 头指针
TRegister *_regs_last = NULL; // 尾指针
HardwareSerial mbSerial(mbRxPin, mbTxPin);                      // 用于通信的串口重定向
ModbusSerial myModbusRTU(mbSerial, myPar.SlaveId, mbSendEnPin); // 声明一个ModbusRTU实例
ModbusEthernet myModbusTCP; // 声明一个ModbusTCP实例

/* ModbusRTU初始化 */
void ModbusRTU_Initialize()
{
    ShowMsg("ModbusRTU initializing", true);
    myModbusRTU.setSlaveId(myPar.SlaveId);      // 设置站号
    myModbusRTU.config(myPar.Baudrate);         // ModbusRTU开始，需要依靠串口来工作
    mbSerial.begin(myPar.Baudrate, SERIAL_8N1); // 串口开始工作
    // ModbusRTU的保持寄存器配置
    for (int i = 0; i < MaxModbusRegNum; i++) // 添加保持寄存器
    {
        myModbusRTU.addHreg(i, 0); // 添加保持寄存器
    }
    // 将ModbusRTU的寄存器指针保存到全局变量中,便于ModbusTCP访问
    _regs_head = myModbusRTU._regs_head;
    _regs_last = myModbusRTU._regs_last;
    ShowMsg("ModbusRTU initialized", true);
}

/* ModbusTCP初始化 */
void ModbusTCP_Initialize()
{
    ShowMsg("ModbusTCP initializing", true);
    // 使用DHCP获取的IP地址或静态IP
    myModbusTCP.config(myPar.mac, myPar.ip);
    // 全局变量中已经保存了ModbusRTU的寄存器指针，所以ModbusTCP中访问的寄存器指针也指向到ModbusRTU的寄存器
    myModbusTCP._regs_head = _regs_head;
    myModbusTCP._regs_last = _regs_last;
    ShowMsg("ModbusTCP IP: " + String(myPar.ip[0]) + "." + String(myPar.ip[1]) + "." + String(myPar.ip[2]) + "." + String(myPar.ip[3]), true);
    ShowMsg("ModbusTCP initialized", true);
}

/* ModbusRTU任务 - 与传感器任务共用RS485总线，通过互斥锁保护 */
void ModbusRTUTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms再启动任务
    ShowMsg("ModbusRTU task started", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1));

        /* 申请RS485总线使用权 - 与传感器任务互斥 */
        if (xSensorMutex != NULL && xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            myModbusRTU.task(); // ModbusRTU处理函数 - 在互斥保护下执行
            xSemaphoreGive(xSensorMutex); // 释放总线
        }
        /* 如果获取互斥锁失败（传感器正在使用），则下次循环再试 */
    }
}

/* ModbusTCP任务 */
void ModbusTCPTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms再启动任务
    ShowMsg("ModbusTCP task started", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        myModbusTCP.task(); // ModbusTCP处理函数
    }
}

/* ==================== mySensorTask.h 全局变量定义 ==================== */
SemaphoreHandle_t xSensorMutex = NULL;
HardwareSerial HMISerial(HMI_USART_RX, HMI_USART_TX);

uint16_t ModbusCRC16(uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint8_t BuildModbusFrame(uint8_t *frame, uint8_t slaveId, uint8_t funcCode, uint16_t regStart, uint16_t regCount)
{
    frame[0] = slaveId;
    frame[1] = funcCode;
    frame[2] = (regStart >> 8) & 0xFF;
    frame[3] = regStart & 0xFF;
    frame[4] = (regCount >> 8) & 0xFF;
    frame[5] = regCount & 0xFF;
    uint16_t crc = ModbusCRC16(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;
    return 8;
}

void SendSensorFrame(uint8_t *frame, uint8_t len)
{
    digitalWrite(SENSOR_RS485_EN, HIGH); // 切换到发送模式
    vTaskDelay(pdMS_TO_TICKS(1));        // 等待方向切换稳定
    mbSerial.write(frame, len);
    mbSerial.flush();                    // 等待发送完成
    digitalWrite(SENSOR_RS485_EN, LOW);  // 切换回接收模式
}

uint8_t ReceiveSensorResponse(uint8_t *buffer, uint8_t expectedLen, uint32_t timeoutMs)
{
    uint8_t index = 0;
    uint32_t startTime = millis();

    while ((millis() - startTime) < timeoutMs)
    {
        while (mbSerial.available() > 0)
        {
            uint8_t byte = mbSerial.read();

            // 帧头校验：第一个字节必须是01，第二个必须是03
            if (index == 0)
            {
                if (byte != SENSOR_SLAVE_ID)
                {
                    continue; // 等待正确的帧头
                }
            }
            else if (index == 1)
            {
                if (byte != SENSOR_FUNC_CODE)
                {
                    index = 0; // 帧头错误，重置
                    if (byte == SENSOR_SLAVE_ID)
                    {
                        buffer[0] = byte;
                        index = 1;
                    }
                    continue;
                }
            }

            buffer[index++] = byte;

            if (index >= expectedLen)
            {
                return index;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return index;
}

bool VerifyResponseCRC(uint8_t *buffer, uint8_t len)
{
    if (len < 4) return false;
    uint16_t recvCRC = (buffer[len - 1] << 8) | buffer[len - 2];
    uint16_t calcCRC = ModbusCRC16(buffer, len - 2);
    return (recvCRC == calcCRC);
}

void SendToHMI(uint8_t flag, float value, uint8_t decimalPlaces)
{
    char buf[32];
    if (decimalPlaces == 0)
    {
        snprintf(buf, sizeof(buf), "%d\r\n", (int)value);
    }
    else if (decimalPlaces == 1)
    {
        snprintf(buf, sizeof(buf), "%.1f\r\n", value);
    }
    else if (decimalPlaces == 2)
    {
        snprintf(buf, sizeof(buf), "%.2f\r\n", value);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.2f\r\n", value);
    }

    HMISerial.write(flag);
    HMISerial.print(buf);
    HMISerial.flush();
}

void SensorSerial_Init()
{
    ShowMsg("Sensor Serial initializing", true);

    /*
     * 传感器RS485使用板子原有的mbSerial (PB10/PB11)。
     * 板子上有DIP拨码开关，根据读出的值动态更改ModbusRTU波特率大小。
     * 传感器固定使用9600bps，与ModbusRTU波特率可能不同。
     * 由于传感器任务和ModbusRTU任务通过互斥锁互斥访问RS485总线，
     * 传感器任务在持有互斥锁期间将mbSerial切换到9600bps通信，
     * 释放互斥锁前恢复为ModbusRTU波特率(myPar.Baudrate)。
     */

    // 初始化串口屏串口 (USART4, 9600波特率)
    HMISerial.setRx(HMI_USART_RX);
    HMISerial.setTx(HMI_USART_TX);
    HMISerial.begin(SENSOR_BAUDRATE, SERIAL_8N1);

    // 创建传感器互斥锁
    xSensorMutex = xSemaphoreCreateMutex();
    if (xSensorMutex == NULL)
    {
        ShowMsg("Sensor Mutex create failed!", true);
    }
    else
    {
        ShowMsg("Sensor Mutex create OK", true);
    }

    ShowMsg("Sensor Serial initialized", true);
}

void ProcessFanControl(uint8_t cmd)
{
    uint32_t fanPin = 0;
    bool turnOn = false;

    switch (cmd)
    {
    case FAN1_OPEN:
        fanPin = FAN1_PIN;
        turnOn = true;
        break;
    case FAN2_OPEN:
        fanPin = FAN2_PIN;
        turnOn = true;
        break;
    case FAN3_OPEN:
        fanPin = FAN3_PIN;
        turnOn = true;
        break;
    case FAN4_OPEN:
        fanPin = FAN4_PIN;
        turnOn = true;
        break;
    case FAN1_CLOSE:
        fanPin = FAN1_PIN;
        turnOn = false;
        break;
    case FAN2_CLOSE:
        fanPin = FAN2_PIN;
        turnOn = false;
        break;
    case FAN3_CLOSE:
        fanPin = FAN3_PIN;
        turnOn = false;
        break;
    case FAN4_CLOSE:
        fanPin = FAN4_PIN;
        turnOn = false;
        break;
    default:
        ShowMsg("Unknown fan cmd:" + String(cmd, HEX), true);
        return;
    }

    digitalWrite(fanPin, turnOn ? LOW : HIGH);

    // 同步更新Modbus保持寄存器中的输出状态
    uint16_t outputState = myModbusRTU.hreg(12);
    uint16_t bitMask = 0;
    if (fanPin == FAN1_PIN) bitMask = 0x04;
    else if (fanPin == FAN2_PIN) bitMask = 0x08;
    else if (fanPin == FAN3_PIN) bitMask = 0x10;
    else if (fanPin == FAN4_PIN) bitMask = 0x20;

    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;

    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Fan cmd:" + String(cmd, HEX) + " pin:" + String(fanPin) + " " + (turnOn ? "ON" : "OFF"), true);
}

void ProcessHumiControl(uint8_t cmd)
{
    uint32_t humiPin = 0;
    bool turnOn = false;

    switch (cmd)
    {
    case HUMI1_OPEN:
        humiPin = HUMI1_PIN;
        turnOn = true;
        break;
    case HUMI2_OPEN:
        humiPin = HUMI2_PIN;
        turnOn = true;
        break;
    case HUMI3_OPEN:
        humiPin = HUMI3_PIN;
        turnOn = true;
        break;
    case HUMI4_OPEN:
        humiPin = HUMI4_PIN;
        turnOn = true;
        break;
    case HUMI1_CLOSE:
        humiPin = HUMI1_PIN;
        turnOn = false;
        break;
    case HUMI2_CLOSE:
        humiPin = HUMI2_PIN;
        turnOn = false;
        break;
    case HUMI3_CLOSE:
        humiPin = HUMI3_PIN;
        turnOn = false;
        break;
    case HUMI4_CLOSE:
        humiPin = HUMI4_PIN;
        turnOn = false;
        break;
    default:
        ShowMsg("Unknown humi cmd:" + String(cmd, HEX), true);
        return;
    }

    digitalWrite(humiPin, turnOn ? LOW : HIGH);

    // 同步更新Modbus保持寄存器中的输出状态
    uint16_t outputState = myModbusRTU.hreg(12);
    uint16_t bitMask = 0;
    if (humiPin == HUMI1_PIN) bitMask = 0x40;
    else if (humiPin == HUMI2_PIN) bitMask = 0x80;
    else if (humiPin == HUMI3_PIN) bitMask = 0x100;
    else if (humiPin == HUMI4_PIN) bitMask = 0x200;

    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;

    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Humi cmd:" + String(cmd, HEX) + " pin:" + String(humiPin) + " " + (turnOn ? "ON" : "OFF"), true);
}

void HMIReceiveTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(800)); // 延时800ms再启动
    ShowMsg("HMI Receive task started", true);

    while (true)
    {
        // 检查是否有数据到达
        if (HMISerial.available() >= 2)
        {
            uint8_t flag = HMISerial.read();
            if (flag == HMI_FLAG_FAN_CTRL)
            {
                uint8_t cmd = HMISerial.read();
                ProcessFanControl(cmd);
            }
            else if (flag == HMI_FLAG_HUMI_CTRL)
            {
                uint8_t cmd = HMISerial.read();
                ProcessHumiControl(cmd);
            }
            else
            {
                // 不是已知控制标志，丢弃后续数据直到清空或找到下一个标志
                while (HMISerial.available() > 0)
                {
                    uint8_t b = HMISerial.peek();
                    if (b == HMI_FLAG_FAN_CTRL || b == HMI_FLAG_HUMI_CTRL)
                        break;
                    HMISerial.read();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // 每10ms检查一次
    }
}

void SensorStateMachineTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500)); // 延时500ms再启动
    ShowMsg("SensorStateMachine task started", true);

    uint8_t txFrame[8];
    uint8_t rxBuffer[16];
    SensorState state = STATE_READ_TEMP_HUMID;
    uint32_t stateEntryTime = 0;
    uint32_t lastPollTime = 0;

    // 缓存采集到的数据
    float cachedHumidity = 0.0f;
    float cachedTemperature = 0.0f;
    float cachedCO2 = 0.0f;
    float cachedNH3 = 0.0f;
    bool hasTempHumid = false;
    bool hasCO2 = false;
    bool hasNH3 = false;

    while (true)
    {
        switch (state)
        {
        case STATE_READ_TEMP_HUMID:
        {
            // 申请总线使用权
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                uint8_t txLen = BuildModbusFrame(txFrame, SENSOR_SLAVE_ID, SENSOR_FUNC_CODE,
                                                  REG_TEMP_HUMID_START, REG_TEMP_HUMID_COUNT);
                SendSensorFrame(txFrame, txLen);
                vTaskDelay(pdMS_TO_TICKS(100));
                uint8_t rxLen = ReceiveSensorResponse(rxBuffer, RESP_TEMP_HUMID_LEN, 200);

                if (rxLen == RESP_TEMP_HUMID_LEN && VerifyResponseCRC(rxBuffer, rxLen))
                {
                    uint16_t humidRaw = (rxBuffer[3] << 8) | rxBuffer[4];
                    cachedHumidity = humidRaw / 10.0f;
                    uint16_t tempRaw = (rxBuffer[5] << 8) | rxBuffer[6];
                    cachedTemperature = tempRaw / 10.0f;
                    hasTempHumid = true;
                }
                else
                {
                    ShowMsg("TempHumid read failed", true);
                    hasTempHumid = false;
                }
                xSemaphoreGive(xSensorMutex);
            }
            state = STATE_READ_CO2;
            break;
        }

        case STATE_READ_CO2:
        {
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                uint8_t txLen = BuildModbusFrame(txFrame, SENSOR_SLAVE_ID, SENSOR_FUNC_CODE,
                                                  REG_CO2_START, REG_CO2_COUNT);
                SendSensorFrame(txFrame, txLen);
                vTaskDelay(pdMS_TO_TICKS(100));
                uint8_t rxLen = ReceiveSensorResponse(rxBuffer, RESP_CO2_LEN, 200);

                if (rxLen == RESP_CO2_LEN && VerifyResponseCRC(rxBuffer, rxLen))
                {
                    uint16_t co2Raw = (rxBuffer[3] << 8) | rxBuffer[4];
                    cachedCO2 = (float)co2Raw;
                    hasCO2 = true;
                }
                else
                {
                    ShowMsg("CO2 read failed", true);
                    hasCO2 = false;
                }
                xSemaphoreGive(xSensorMutex);
            }
            state = STATE_READ_NH3;
            break;
        }

        case STATE_READ_NH3:
        {
            if (xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                uint8_t txLen = BuildModbusFrame(txFrame, SENSOR_SLAVE_ID, SENSOR_FUNC_CODE,
                                                  REG_NH3_START, REG_NH3_COUNT);
                SendSensorFrame(txFrame, txLen);
                vTaskDelay(pdMS_TO_TICKS(100));
                uint8_t rxLen = ReceiveSensorResponse(rxBuffer, RESP_NH3_LEN, 200);

                if (rxLen == RESP_NH3_LEN && VerifyResponseCRC(rxBuffer, rxLen))
                {
                    uint16_t nh3Raw = (rxBuffer[3] << 8) | rxBuffer[4];
                    cachedNH3 = nh3Raw / 100.0f;
                    hasNH3 = true;
                }
                else
                {
                    ShowMsg("NH3 read failed", true);
                    hasNH3 = false;
                }
                xSemaphoreGive(xSensorMutex);
            }
            state = STATE_SEND_HMI;
            break;
        }

        case STATE_SEND_HMI:
        {
            // 发送所有缓存的数据到串口屏
            if (hasTempHumid)
            {
                SendToHMI(HMI_FLAG_HUMID, cachedHumidity, 1);
                vTaskDelay(pdMS_TO_TICKS(10));
                SendToHMI(HMI_FLAG_TEMP, cachedTemperature, 1);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (hasCO2)
            {
                SendToHMI(HMI_FLAG_CO2, cachedCO2, 0);
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            if (hasNH3)
            {
                SendToHMI(HMI_FLAG_NH3, cachedNH3, 2);
            }

            // 同步传感器数据到Modbus寄存器 (20-24)
            uint16_t sensorStatus = 0;
            if (hasTempHumid)
            {
                myModbusRTU.setHreg(REG_SENSOR_TEMP, (uint16_t)(cachedTemperature * 10.0f));
                myModbusRTU.setHreg(REG_SENSOR_HUMID, (uint16_t)(cachedHumidity * 10.0f));
                sensorStatus |= 0x01;
            }
            if (hasCO2)
            {
                myModbusRTU.setHreg(REG_SENSOR_CO2, (uint16_t)cachedCO2);
                sensorStatus |= 0x02;
            }
            if (hasNH3)
            {
                myModbusRTU.setHreg(REG_SENSOR_NH3, (uint16_t)(cachedNH3 * 100.0f));
                sensorStatus |= 0x04;
            }
            myModbusRTU.setHreg(REG_SENSOR_STATUS, sensorStatus);

            // 同步传感器数据到MQTT设备状态
            if (xSensorDataMutex != NULL && xSemaphoreTake(xSensorDataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                if (hasTempHumid)
                {
                    deviceState.temperature = cachedTemperature;
                    deviceState.humidity = cachedHumidity;
                }
                if (hasCO2)
                {
                    deviceState.co2 = cachedCO2;
                }
                if (hasNH3)
                {
                    deviceState.nh3 = cachedNH3;
                }
                xSemaphoreGive(xSensorDataMutex);
            }

            lastPollTime = millis();
            state = STATE_WAIT_INTERVAL;
            break;
        }

        case STATE_WAIT_INTERVAL:
        {
            // 等待轮询间隔，期间让出CPU
            if (millis() - lastPollTime >= SENSOR_POLL_INTERVAL_MS)
            {
                state = STATE_READ_TEMP_HUMID;
                hasTempHumid = false;
                hasCO2 = false;
                hasNH3 = false;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }

        default:
            state = STATE_WAIT_INTERVAL;
            break;
        }
    }
}

/* ==================== myLoRaTask.h 全局变量定义 ==================== */
SX1262 radio = new Module(RA01S_SPI_NSS, RA01S_DIO1, RA01S_RESET, RA01S_BUSY);
uint8_t loraRxBuffer[LORA_FRAME_LEN];
volatile uint8_t loraRxIndex = 0;
TaskHandle_t xLoRaTaskHandle = NULL;

void LoRa_DIO1_ISR(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xLoRaTaskHandle != NULL)
    {
        vTaskNotifyGiveFromISR(xLoRaTaskHandle, &xHigherPriorityTaskWoken);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void SendToHMI_LoRa(uint8_t flag, float value, uint8_t decimalPlaces)
{
    char buf[32];
    if (decimalPlaces == 0)
    {
        snprintf(buf, sizeof(buf), "%d\r\n", (int)value);
    }
    else if (decimalPlaces == 1)
    {
        snprintf(buf, sizeof(buf), "%.1f\r\n", value);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.1f\r\n", value);
    }

    HMISerial.write(flag);
    HMISerial.print(buf);
    HMISerial.flush();
}

void ParseLoRaFrame(uint8_t *buffer, size_t len)
{
    if (len < LORA_FRAME_LEN)
    {
        ShowMsg("LoRa frame too short: " + String(len), true);
        return;
    }

    uint8_t flag = buffer[0];
    uint16_t rawValue = ((uint16_t)buffer[1] << 8) | buffer[2];
    float actualValue;

    switch (flag)
    {
    case LORA_FLAG_TEMP:
        actualValue = rawValue / 10.0f;
        myModbusRTU.setHreg(LORA_MODBUS_REG_TEMP, rawValue);  // 写入Modbus寄存器 (原始值放大10倍)
        deviceState.loraTemperature = actualValue;             // 同步到MQTT数据结构
        SendToHMI_LoRa(flag, actualValue, 1);
        ShowMsg("LoRa Temp: " + String(actualValue, 1), true);
        break;

    case LORA_FLAG_HUMID:
        actualValue = rawValue / 10.0f;
        myModbusRTU.setHreg(LORA_MODBUS_REG_HUMID, rawValue); // 写入Modbus寄存器 (原始值放大10倍)
        deviceState.loraHumidity = actualValue;                // 同步到MQTT数据结构
        SendToHMI_LoRa(flag, actualValue, 1);
        ShowMsg("LoRa Humid: " + String(actualValue, 1), true);
        break;

    case LORA_FLAG_CO2:
        actualValue = (float)rawValue;
        myModbusRTU.setHreg(LORA_MODBUS_REG_CO2, rawValue);   // 写入Modbus寄存器
        deviceState.loraCo2 = actualValue;                     // 同步到MQTT数据结构
        SendToHMI_LoRa(flag, actualValue, 0);
        ShowMsg("LoRa CO2: " + String((int)actualValue), true);
        break;

    case LORA_FLAG_NH3:
        actualValue = rawValue / 100.0f;                       // NH3放大100倍
        myModbusRTU.setHreg(LORA_MODBUS_REG_NH3, rawValue);   // 写入Modbus寄存器 (原始值放大100倍)
        deviceState.loraNh3 = actualValue;                     // 同步到MQTT数据结构
        SendToHMI_LoRa(flag, actualValue, 2);                  // 保留2位小数
        ShowMsg("LoRa NH3: " + String(actualValue, 2), true);
        break;

    default:
        ShowMsg("LoRa unknown flag: 0x" + String(flag, HEX), true);
        break;
    }
}

int16_t Ra01S_Init(void)
{
    ShowMsg("Ra-01S Init start", true);

    /* 初始化SX1262
     * 参数说明:
     * 433.0 - 载波频率433MHz (根据实际模块调整)
     * 125.0 - 带宽125kHz
     * 9     - 扩频因子SF9
     * 7     - 编码率4/7
     * 0x12  - 同步字 (私有网络)
     * 8     - 发射功率8dBm
     * 8     - 前导码长度8
     * 1.6   - TCXO电压1.6V (Ra-01S使用TCXO)
     */
    int16_t state = radio.begin(433.0, 125.0, 9, 7, 0x12, 8, 8, 1.6);

    if (state == RADIOLIB_ERR_NONE)
    {
        ShowMsg("Ra-01S Init success", true);
    }
    else
    {
        ShowMsg("Ra-01S Init failed, code: " + String(state), true);
    }

    return state;
}

int16_t Ra01S_StartReceive(void)
{
    /* 配置DIO1为RxDone中断输出 - 接收完成时触发 */
    int16_t state = radio.startReceive();

    if (state == RADIOLIB_ERR_NONE)
    {
        ShowMsg("Ra-01S RX mode started", true);
    }
    else
    {
        ShowMsg("Ra-01S RX start failed, code: " + String(state), true);
    }

    return state;
}

void LoRaReceiveTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1秒等待系统稳定
    ShowMsg("LoRa Receive task started", true);

    /* 保存任务句柄，供ISR使用 */
    xLoRaTaskHandle = xTaskGetCurrentTaskHandle();

    /* 初始化Ra-01S模块 */
    int16_t initState = Ra01S_Init();
    if (initState != RADIOLIB_ERR_NONE)
    {
        ShowMsg("LoRa task exit due to init failure", true);
        xLoRaTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* 启动接收模式 */
    int16_t rxState = Ra01S_StartReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ShowMsg("LoRa task exit due to RX start failure", true);
        xLoRaTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* 配置DIO1外部中断 - 上升沿触发 */
    attachInterrupt(digitalPinToInterrupt(RA01S_DIO1), LoRa_DIO1_ISR, RISING);
    ShowMsg("LoRa DIO1 interrupt attached", true);

    while (true)
    {
        /* 等待DIO1中断通知 - 无限期阻塞，几乎不占用CPU */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

        /* 读取接收到的数据 */
        int16_t state = radio.readData(loraRxBuffer, LORA_FRAME_LEN);

        if (state == RADIOLIB_ERR_NONE)
        {
            /* 接收成功，解析数据 */
            ShowMsg("LoRa RX: " + String(loraRxBuffer[0], HEX) + " " +
                    String(loraRxBuffer[1], HEX) + " " +
                    String(loraRxBuffer[2], HEX), true);

            ParseLoRaFrame(loraRxBuffer, LORA_FRAME_LEN);

            /* 重新启动接收 - 准备下一次中断 */
            radio.startReceive();
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            /* CRC错误 */
            ShowMsg("LoRa CRC error", true);
            /* 重新启动接收 */
            radio.startReceive();
        }
        else if (state != RADIOLIB_ERR_NONE)
        {
            /* 其他错误 */
            ShowMsg("LoRa RX error, code: " + String(state), true);
            /* 重新启动接收 */
            radio.startReceive();
        }
        /* RADIOLIB_ERR_RX_TIMEOUT 是正常情况，不需要处理 */
    }
}

/* ==================== myMQTT_TLS.h 全局变量定义 ==================== */
EthernetClient ethernetClient;
static const br_x509_trust_anchor TAs[] = {};
SSLClient sslClient(ethernetClient, TAs, 0, PA0, 1, SSLClient::SSL_WARN);
MqttClient mqttClient(sslClient);
volatile bool mqttConnected = false;
SemaphoreHandle_t xMQTTMutex = NULL;
SemaphoreHandle_t xSensorDataMutex = NULL;
DeviceState deviceState = {{false, false, false, false}, {false, false, false, false}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

void MQTT_TLS_Init()
{
    ShowMsg("MQTT over TLS Initializing...", true);

    /* 创建互斥锁 */
    if (xMQTTMutex == NULL)
    {
        xMQTTMutex = xSemaphoreCreateMutex();
    }
    if (xSensorDataMutex == NULL)
    {
        xSensorDataMutex = xSemaphoreCreateMutex();
    }

    /* 生成唯一的Client ID */
    String clientId = String(MQTT_CLIENT_ID) + String(GetMCUId(), HEX);

    /* 配置SSLClient - CA证书已通过信任锚点配置 */

    /* 配置MQTT客户端 */
    mqttClient.setId(clientId.c_str());
    mqttClient.setUsernamePassword(MQTT_USERNAME, MQTT_PASSWORD);
    mqttClient.setKeepAliveInterval(60 * 1000); // 60秒保活
    mqttClient.setCleanSession(true);

    /* 设置消息回调 */
    mqttClient.onMessage(onMqttMessage);

    ShowMsg("MQTT Client ID: " + clientId, true);
    ShowMsg("MQTT Broker: " + String(MQTT_BROKER_HOST) + ":" + String(MQTT_BROKER_PORT), true);
    ShowMsg("MQTT over TLS Initialized", true);
}

bool MQTT_TLS_Connect()
{
    if (mqttConnected)
    {
        return true;
    }

    ShowMsg("Connecting to MQTT Broker...", true);

    /* 尝试连接 */
    if (!mqttClient.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT))
    {
        ShowMsg("MQTT Connection failed! Error: " + String(mqttClient.connectError()), true);
        return false;
    }

    mqttConnected = true;
    ShowMsg("MQTT Connected successfully!", true);

    /* 订阅控制主题 */
    mqttClient.subscribe(MQTT_SUB_TOPIC_CONTROL);
    ShowMsg("Subscribed to: " + String(MQTT_SUB_TOPIC_CONTROL), true);

    return true;
}

void MQTT_TLS_Disconnect()
{
    if (mqttClient.connected())
    {
        mqttClient.stop();
    }
    mqttConnected = false;
    ShowMsg("MQTT Disconnected", true);
}

void MQTT_TLS_Publish(const char *topic, const char *payload)
{
    if (!mqttConnected || !mqttClient.connected())
    {
        return;
    }

    if (xSemaphoreTake(xMQTTMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        mqttClient.beginMessage(topic);
        mqttClient.print(payload);
        mqttClient.endMessage();
        xSemaphoreGive(xMQTTMutex);
    }
}

void MQTT_TLS_Subscribe(const char *topic)
{
    if (!mqttConnected || !mqttClient.connected())
    {
        return;
    }

    if (xSemaphoreTake(xMQTTMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        mqttClient.subscribe(topic);
        xSemaphoreGive(xMQTTMutex);
        ShowMsg("Subscribed: " + String(topic), true);
    }
}

void UpdateFanState(uint8_t fanIndex, bool turnOn)
{
    if (fanIndex > 3) return;

    uint32_t fanPin = 0;
    uint16_t bitMask = 0;

    switch (fanIndex)
    {
    case 0: fanPin = FAN1_PIN; bitMask = FAN1_BITMASK; break;
    case 1: fanPin = FAN2_PIN; bitMask = FAN2_BITMASK; break;
    case 2: fanPin = FAN3_PIN; bitMask = FAN3_BITMASK; break;
    case 3: fanPin = FAN4_PIN; bitMask = FAN4_BITMASK; break;
    }

    digitalWrite(fanPin, turnOn ? LOW : HIGH);
    deviceState.fan[fanIndex] = turnOn;

    /* 同步更新Modbus保持寄存器中的输出状态 - 与mySensorTask保持一致 */
    uint16_t outputState = myModbusRTU.hreg(12);
    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;
    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Fan" + String(fanIndex + 1) + " set to " + (turnOn ? "ON" : "OFF"), true);
}

void UpdateHumidifierState(uint8_t humiIndex, bool turnOn)
{
    if (humiIndex > 3) return;

    uint32_t humiPin = 0;
    uint16_t bitMask = 0;

    switch (humiIndex)
    {
    case 0: humiPin = HUMI1_PIN; bitMask = HUMI1_BITMASK; break;
    case 1: humiPin = HUMI2_PIN; bitMask = HUMI2_BITMASK; break;
    case 2: humiPin = HUMI3_PIN; bitMask = HUMI3_BITMASK; break;
    case 3: humiPin = HUMI4_PIN; bitMask = HUMI4_BITMASK; break;
    }

    digitalWrite(humiPin, turnOn ? LOW : HIGH);
    deviceState.humidifier[humiIndex] = turnOn;

    /* 同步更新Modbus保持寄存器中的输出状态 - 与mySensorTask保持一致 */
    uint16_t outputState = myModbusRTU.hreg(12);
    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;
    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Humidifier" + String(humiIndex + 1) + " set to " + (turnOn ? "ON" : "OFF"), true);
}

void ProcessControlCommand(const String &payload)
{
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        ShowMsg("JSON parse failed: " + String(error.c_str()), true);
        return;
    }

    /* 解析4个风机控制 */
    if (doc.containsKey("fan1"))
        UpdateFanState(0, doc["fan1"].as<bool>());
    if (doc.containsKey("fan2"))
        UpdateFanState(1, doc["fan2"].as<bool>());
    if (doc.containsKey("fan3"))
        UpdateFanState(2, doc["fan3"].as<bool>());
    if (doc.containsKey("fan4"))
        UpdateFanState(3, doc["fan4"].as<bool>());

    /* 解析4个加湿器控制 */
    if (doc.containsKey("humidifier1"))
        UpdateHumidifierState(0, doc["humidifier1"].as<bool>());
    if (doc.containsKey("humidifier2"))
        UpdateHumidifierState(1, doc["humidifier2"].as<bool>());
    if (doc.containsKey("humidifier3"))
        UpdateHumidifierState(2, doc["humidifier3"].as<bool>());
    if (doc.containsKey("humidifier4"))
        UpdateHumidifierState(3, doc["humidifier4"].as<bool>());

    /* 立即上报状态 */
    String statusJson = BuildStatusJson();
    MQTT_TLS_Publish(MQTT_PUB_TOPIC_DATA, statusJson.c_str());
}

void UpdateDeviceOutputs()
{
    UpdateFanState(0, deviceState.fan[0]);
    UpdateFanState(1, deviceState.fan[1]);
    UpdateFanState(2, deviceState.fan[2]);
    UpdateFanState(3, deviceState.fan[3]);

    UpdateHumidifierState(0, deviceState.humidifier[0]);
    UpdateHumidifierState(1, deviceState.humidifier[1]);
    UpdateHumidifierState(2, deviceState.humidifier[2]);
    UpdateHumidifierState(3, deviceState.humidifier[3]);
}

String BuildStatusJson()
{
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;

    doc["deviceId"] = String(MQTT_CLIENT_ID) + String(GetMCUId(), HEX);

    /* 4个风机状态 */
    doc["fan1"] = deviceState.fan[0];
    doc["fan2"] = deviceState.fan[1];
    doc["fan3"] = deviceState.fan[2];
    doc["fan4"] = deviceState.fan[3];

    /* 4个加湿器状态 */
    doc["humidifier1"] = deviceState.humidifier[0];
    doc["humidifier2"] = deviceState.humidifier[1];
    doc["humidifier3"] = deviceState.humidifier[2];
    doc["humidifier4"] = deviceState.humidifier[3];

    /* 传感器数据 - 使用互斥锁保护 */
    if (xSemaphoreTake(xSensorDataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        /* 本地传感器数据 */
        JsonObject local = doc.createNestedObject("local");
        local["temperature"] = deviceState.temperature;
        local["humidity"] = deviceState.humidity;
        local["co2"] = deviceState.co2;
        local["nh3"] = deviceState.nh3;

        /* LoRa机器人采集数据 */
        JsonObject robot = doc.createNestedObject("robot");
        robot["temperature"] = deviceState.loraTemperature;
        robot["humidity"] = deviceState.loraHumidity;
        robot["co2"] = deviceState.loraCo2;
        robot["nh3"] = deviceState.loraNh3;

        xSemaphoreGive(xSensorDataMutex);
    }

    doc["timestamp"] = millis() / 1000;

    String output;
    serializeJson(doc, output);
    return output;
}

void onMqttMessage(int messageSize)
{
    String topic = mqttClient.messageTopic();
    String payload = "";

    while (mqttClient.available())
    {
        payload += (char)mqttClient.read();
    }

    ShowMsg("MQTT RX [" + topic + "]: " + payload, true);

    /* 处理控制命令 */
    if (topic == MQTT_SUB_TOPIC_CONTROL)
    {
        ProcessControlCommand(payload);
    }
}

void MQTT_TLS_KeepAlive()
{
    if (mqttClient.connected())
    {
        mqttClient.poll();
    }
}

void MQTT_TLS_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000)); // 延时5秒等待网络就绪
    ShowMsg("MQTT TLS Task started", true);

    uint32_t lastReconnectAttempt = 0;
    uint32_t lastPublishTime = 0;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));

        /* 检查连接状态 */
        if (!mqttClient.connected())
        {
            mqttConnected = false;

            /* 重连逻辑 */
            if (millis() - lastReconnectAttempt > MQTT_RECONNECT_DELAY_MS)
            {
                lastReconnectAttempt = millis();
                ShowMsg("MQTT reconnecting...", true);

                if (MQTT_TLS_Connect())
                {
                    lastReconnectAttempt = 0;
                }
            }
        }
        else
        {
            /* 保持连接活跃，处理接收 */
            MQTT_TLS_KeepAlive();

            /* 定时上报设备状态 */
            if (millis() - lastPublishTime > MQTT_PUBLISH_INTERVAL_MS)
            {
                lastPublishTime = millis();

                String statusJson = BuildStatusJson();
                MQTT_TLS_Publish(MQTT_PUB_TOPIC_DATA, statusJson.c_str());
                ShowMsg("Status published: " + statusJson, true);
            }
        }
    }
}

/* ==================== myNetworkConfig.h 全局变量定义 ==================== */
volatile bool networkInitialized = false;
IPAddress localIP;
IPAddress subnetMask;
IPAddress gatewayIP;
IPAddress dnsServerIP;

bool Network_DHCP_Init()
{
    ShowMsg("Starting DHCP...", true);

    /* 使用Ethernet.begin(mac)启动DHCP */
    /* 这会使用W5500的Socket 0进行DHCP协商 */
    if (Ethernet.begin(myPar.mac) == 0)
    {
        ShowMsg("DHCP Failed!", true);
        return false;
    }

    /* 获取DHCP分配的地址 */
    localIP = Ethernet.localIP();
    subnetMask = Ethernet.subnetMask();
    gatewayIP = Ethernet.gatewayIP();
    dnsServerIP = Ethernet.dnsServerIP();

    ShowMsg("DHCP Success!", true);
    ShowMsg("IP: " + String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + "." + String(localIP[3]), true);
    ShowMsg("Mask: " + String(subnetMask[0]) + "." + String(subnetMask[1]) + "." + String(subnetMask[2]) + "." + String(subnetMask[3]), true);
    ShowMsg("GW: " + String(gatewayIP[0]) + "." + String(gatewayIP[1]) + "." + String(gatewayIP[2]) + "." + String(gatewayIP[3]), true);

    /* 更新全局IP地址 */
    myPar.ip = localIP;

    return true;
}

bool Network_Static_Init()
{
    ShowMsg("Using Static IP...", true);

    /* 配置静态IP */
    Ethernet.begin(myPar.mac, myPar.ip);

    localIP = myPar.ip;
    subnetMask = Ethernet.subnetMask();
    gatewayIP = Ethernet.gatewayIP();

    ShowMsg("Static IP: " + String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + "." + String(localIP[3]), true);

    return true;
}

bool Network_Init()
{
    ShowMsg("Network Initializing...", true);

    /* 初始化Ethernet库（不指定IP，让后续配置决定） */
    Ethernet.init(10);  // 使用默认CS引脚，STM32F407需要根据实际情况修改

    #if USE_DHCP
        /* 尝试DHCP */
        uint32_t dhcpStartTime = millis();
        bool dhcpSuccess = false;

        while (millis() - dhcpStartTime < DHCP_TIMEOUT_MS)
        {
            if (Network_DHCP_Init())
            {
                dhcpSuccess = true;
                break;
            }
            ShowMsg("DHCP retry...", true);
            delay(DHCP_RETRY_DELAY_MS);
        }

        /* DHCP失败则回退到静态IP */
        if (!dhcpSuccess)
        {
            ShowMsg("DHCP timeout, fallback to static IP", true);
            Network_Static_Init();
        }
    #else
        /* 直接使用静态IP */
        Network_Static_Init();
    #endif

    networkInitialized = true;
    ShowMsg("Network Initialized", true);
    return true;
}

void Network_Maintain()
{
    if (!networkInitialized) return;

    #if USE_DHCP
        /* 维护DHCP租约 */
        switch (Ethernet.maintain())
        {
            case 1:
                ShowMsg("DHCP lease renewed", true);
                break;
            case 2:
                ShowMsg("DHCP lease rebind", true);
                break;
            case 3:
                ShowMsg("DHCP lease failed", true);
                break;
            default:
                break;
        }
    #endif
}

/* ==================== myESP32C6.h 全局变量定义 ==================== */
SPIClass ESP32C6_SPI3(ESP32C6_SPI_MOSI, ESP32C6_SPI_MISO, ESP32C6_SPI_SCK);

void ESP32C6_SPI_Init()
{
    ShowMsg("ESP32C6 SPI3 Initizing", true);
    pinMode(ESP32C6_SPI_CS, OUTPUT);
    digitalWrite(ESP32C6_SPI_CS, HIGH);
    pinMode(ESP32C6_EN, OUTPUT);
    digitalWrite(ESP32C6_EN, HIGH);
    /* 使用独立的SPI3实例，避免与Ra-01S的SPI2冲突 */
    ESP32C6_SPI3.begin();
    ShowMsg("ESP32C6 SPI3 Initized", true);
}

/* ==================== myK210.h 全局变量定义 ==================== */
HardwareSerial k210Serial(K210_USART_RX, K210_USART_TX);

void K210_Initialize(uint32_t baudrate)
{
    ShowMsg("K210 initializing", true);

    // 配置USART6 GPIO
    // PC6 -> USART6_TX (AF8)
    // PC7 -> USART6_RX (AF8)
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 初始化串口
    k210Serial.begin(baudrate);

    ShowMsg("K210 initialized", true);
}

void K210_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ShowMsg("K210 task started", true);

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10));

        // TODO: 后续添加K210数据接收和处理逻辑
    }
}

/* ==================== myADS1115.h 全局变量定义 ==================== */
ADS1115 ADS(0x48);

bool InitializeADS1115()
{
    // Wire.setSDA(PB9);   // 设置I2C的SDA和SCL引脚
    // Wire.setSCL(PB8);   // 设置I2C的SDA和SCL引脚
    // Wire.begin();       // 初始化Wire库
    ADS.setGain(1);     // 0: 6.144V, 1: 4.096V, 2: 2.048V, 3: 1.024V, 4: 0.512V, 5: 0.256V
    return ADS.begin(); // 初始化ADS1115
}

float ReadADS1115(int channel)
{
    int16_t val = ADS.readADC(channel);
    float f = ADS.toVoltage(1); //  voltage factor
    ShowMsg("A" + String(channel) + ":" + String(val) + "\t" + String(val * f, 3), true);
    return val * f;
}

void ReadADS1115All(int16_t &val_0, int16_t &val_1, int16_t &val_2, int16_t &val_3)
{
    val_0 = ADS.readADC(0);
    val_1 = ADS.readADC(1);
    val_2 = ADS.readADC(2);
    val_3 = ADS.readADC(3);
}

void ReadADS1115All()
{
    int16_t val_0 = ADS.readADC(0);
    int16_t val_1 = ADS.readADC(1);
    int16_t val_2 = ADS.readADC(2);
    int16_t val_3 = ADS.readADC(3);

    float f = ADS.toVoltage(1); //  voltage factor
    ShowMsg("Read ADS1115:");
    ShowMsg("\nA0:\t" + String(val_0) + "\t" + String(val_0 * f, 3) + "V");
    ShowMsg("\nA1:\t" + String(val_1) + "\t" + String(val_1 * f, 3) + "V");
    ShowMsg("\nA2:\t" + String(val_2) + "\t" + String(val_2 * f, 3) + "V");
    ShowMsg("\nA3:\t" + String(val_3) + "\t" + String(val_3 * f, 3) + "V", true);
}

/* ==================== myTask.h 函数实现 ==================== */

void WatchdogTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ShowMsg("Watchdog Task started", true);
    IWatchdog.begin(1000 * WATCHDOG_TIMEOUT_MS);
    uint32_t lastFeedTime = millis();
    bool witchDogTimeout = false;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (millis() - lastFeedTime > WATCHDOG_TIMEOUT_MS / 2)
        {
            IWatchdog.reload();
            lastFeedTime = millis();
        }
        if (IWatchdog.isReset())
        {
            witchDogTimeout = true;
            IWatchdog.clearReset();
        }
        if (witchDogTimeout)
        {
            digitalWrite(ERROR_LED, LOW);
        }
    }
}

void IICTask(void *pvParameters)
{
    uint8_t ADS1115InitCounter = 0;

    Wire.setSDA(PB9);
    Wire.setSCL(PB8);
    Wire.begin();
    ShowMsg("ADS1115 Init start:", true);
    while (!InitializeADS1115())
    {
        ShowMsg("ADS1115 Init Failed,Try again!", true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ADS1115InitCounter++;
        if (ADS1115InitCounter > 5)
        {
            ShowMsg("ADS1115 Init Failed,Exit!", true);
            myModbusRTU.setHreg(15, 32767);
            myModbusRTU.setHreg(16, 32767);
            myModbusRTU.setHreg(17, 32767);
            myModbusRTU.setHreg(18, 32767);
            vTaskDelete(NULL);
        }
    }
    ShowMsg("ADS1115 Init OK", true);

    while (true)
    {
        static uint32_t delayTime;

        if (millis() - delayTime > 1000)
        {
            ShowMsg("Read ADS1115...");
            ReadADS1115All(myAI.AI0, myAI.AI1, myAI.AI2, myAI.AI3);
            myModbusRTU.setHreg(15, myAI.AI0);
            myModbusRTU.setHreg(16, myAI.AI1);
            myModbusRTU.setHreg(17, myAI.AI2);
            myModbusRTU.setHreg(18, myAI.AI3);
            delayTime = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Load_ParameterTORegister(void)
{
    myModbusRTU.setHreg(0, Version);
    myModbusRTU.setHreg(1, myPar.SlaveId);
    myModbusRTU.setHreg(2, baudRateSwitchState);
    myModbusRTU.setHreg(4, myPar.Input_Filter_Time);
    myModbusRTU.setHreg(5, (myPar.mac[1] << 8) + myPar.mac[0]);
    myModbusRTU.setHreg(6, (myPar.mac[3] << 8) + myPar.mac[2]);
    myModbusRTU.setHreg(7, (myPar.mac[5] << 8) + myPar.mac[4]);
    myModbusRTU.setHreg(8, (myPar.ip[2] << 8) + myPar.ip[3]);
    myModbusRTU.setHreg(9, (myPar.ip[0] << 8) + myPar.ip[1]);
}

void Save_ParameterFromRegister()
{
    myPar.Input_Filter_Time = myModbusRTU.hreg(4);
    myPar.mac[0] = myModbusRTU.hreg(5) & 0xFF;
    myPar.mac[1] = (myModbusRTU.hreg(5) >> 8) & 0xFF;
    myPar.mac[2] = myModbusRTU.hreg(6) & 0xFF;
    myPar.mac[3] = (myModbusRTU.hreg(6) >> 8) & 0xFF;
    myPar.mac[4] = myModbusRTU.hreg(7) & 0xFF;
    myPar.mac[5] = (myModbusRTU.hreg(7) >> 8) & 0xFF;

    myPar.ip[0] = (myModbusRTU.hreg(9) >> 8) & 0xFF;
    myPar.ip[1] = myModbusRTU.hreg(9) & 0xFF;
    myPar.ip[2] = (myModbusRTU.hreg(8) >> 8) & 0xFF;
    myPar.ip[3] = myModbusRTU.hreg(8) & 0xFF;
    Save_Parameter();
}

void MainTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ShowMsg("Main task started", true);
    uint32_t timeRecord = 0;
    uint16_t Input_Temp = 0;
    uint16_t Output_Temp = 0;
    uint16_t Param_Temp = 0;
    bool runLedTemp = false;

    Load_ParameterTORegister();
    ShowMsg("", true);
    ShowMsg("Parameter SaveFlag:" + String(myPar.InitFlag), true);
    ShowMsg("ID:" + String(myPar.SlaveId), true);
    ShowMsg("BaudRate:" + String(myPar.Baudrate), true);
    ShowMsg("IP:" + String(myPar.ip[0]) + "." + String(myPar.ip[1]) + "." + String(myPar.ip[2]) + "." + String(myPar.ip[3]), true);
    ShowMsg("Port:" + String(MODBUSIP_PORT), true);
    ShowMsg("Mac:" + String(myPar.mac[0]) + " " + String(myPar.mac[1]) + " " + String(myPar.mac[2]) + " ");
    ShowMsg(String(myPar.mac[3]) + " " + String(myPar.mac[4]) + " " + String(myPar.mac[5]), true);
    ShowMsg("", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        Param_Temp = myModbusRTU.hreg(3);
        if (Param_Temp != 0)
        {
            myModbusRTU.setHreg(3, 0);
            if (Param_Temp == ParameterOption::Save)
            {
                IWatchdog.reload();
                Save_ParameterFromRegister();
            }
            else if (Param_Temp == ParameterOption::Reload)
            {
                Load_ParameterTORegister();
            }
            else if (Param_Temp == ParameterOption::Reboot)
            {
                NVIC_SystemReset();
            }
            else if (Param_Temp == ParameterOption::Factory_Reset)
            {
                IWatchdog.reload();
                Parameter_Init();
                NVIC_SystemReset();
            }
            Param_Temp = 0;
        }

        SET_BIT_BY_BOOL(Input_Temp, 0, Input.X0);
        SET_BIT_BY_BOOL(Input_Temp, 1, Input.X1);
        SET_BIT_BY_BOOL(Input_Temp, 2, Input.X2);
        SET_BIT_BY_BOOL(Input_Temp, 3, Input.X3);
        SET_BIT_BY_BOOL(Input_Temp, 4, Input.X4);
        SET_BIT_BY_BOOL(Input_Temp, 5, Input.X5);
        SET_BIT_BY_BOOL(Input_Temp, 6, Input.X6);
        SET_BIT_BY_BOOL(Input_Temp, 7, Input.X7);
        myModbusRTU.setHreg(11, Input_Temp);

        if (myModbusRTU.hreg(12) != Output_Temp)
        {
            Output_Temp = myModbusRTU.hreg(12);
            digitalWrite(Output_Y0, (Output_Temp & 0x01) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y1, (Output_Temp & 0x02) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y2, (Output_Temp & 0x04) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y3, (Output_Temp & 0x08) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y4, (Output_Temp & 0x10) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y5, (Output_Temp & 0x20) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y6, (Output_Temp & 0x40) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y7, (Output_Temp & 0x80) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y8, (Output_Temp & 0x100) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y9, (Output_Temp & 0x200) > 0 ? LOW : HIGH);
        }

        if (millis() - timeRecord > 1000)
        {
            timeRecord = millis();
            digitalWrite(RUN_LED, runLedTemp = !runLedTemp);
            myModbusRTU.setHreg(10, timeRecord / 1000);
        }
    }
}

void CreateTaskMethods(void *pvParameters)
{
    xTaskCreate(WatchdogTask, "WatchdogTask", 96, NULL, 6, NULL);
    ShowMsg("Watchdog task created.", true);

    xTaskCreate(X_filter, "X_filter", 96, NULL, 5, NULL);
    ShowMsg("Input filter task created.", true);

    xTaskCreate(ModbusRTUTask, "ModbusRTUSevice", 128, NULL, 5, NULL);
    ShowMsg("ModbusRTU task created.", true);

    xTaskCreate(ModbusTCPTask, "ModbusTCPTask", 128 * 2, NULL, 5, NULL);
    ShowMsg("ModbusTCP task created.", true);

    xTaskCreate(IICTask, "IICTask", 128 * 3, NULL, 3, NULL);
    ShowMsg("IIC task created.", true);

    xTaskCreate(MainTask, "MainTask", 128 * 2, NULL, 3, NULL);
    ShowMsg("MainTask created.", true);

    xTaskCreate(SensorStateMachineTask, "SensorTask", 128 * 2, NULL, 2, NULL);
    ShowMsg("Sensor state machine task created.", true);

    xTaskCreate(HMIReceiveTask, "HMIReceiveTask", 128 * 2, NULL, 3, NULL);
    ShowMsg("HMI Receive task created.", true);

    xTaskCreate(LoRaReceiveTask, "LoRaReceiveTask", 128 * 2, NULL, 4, NULL);
    ShowMsg("LoRa Receive task created.", true);

    xTaskCreate(MQTT_TLS_Task, "MQTT_TLS_Task", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, NULL);
    ShowMsg("MQTT TLS task created.", true);

    ShowMsg("All Task Create Success", true);
    ShowMsg("", true);
    vTaskDelete(NULL);
}



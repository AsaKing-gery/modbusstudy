#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "mySensorTask.h"
#include "myModbus.h"
#include "myDeviceState.h"
#include "myShowMsg.h"

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
    digitalWrite(SENSOR_RS485_EN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(1));
    mbSerial.write(frame, len);
    mbSerial.flush();
    digitalWrite(SENSOR_RS485_EN, LOW);
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
            if (index == 0)
            {
                if (byte != SENSOR_SLAVE_ID)
                {
                    continue;
                }
            }
            else if (index == 1)
            {
                if (byte != SENSOR_FUNC_CODE)
                {
                    index = 0;
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
        snprintf(buf, sizeof(buf), "%d", (int)value);
    }
    else if (decimalPlaces == 1)
    {
        snprintf(buf, sizeof(buf), "%.1f", value);
    }
    else if (decimalPlaces == 2)
    {
        snprintf(buf, sizeof(buf), "%.2f", value);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.2f", value);
    }

    /* 帧格式: [flag][字符串数据][flag]，帧头==帧尾 */
    HMISerial.write(flag);
    HMISerial.print(buf);
    HMISerial.write(flag);
    HMISerial.flush();
}

void SensorSerial_Init()
{
    Serial.println("S"); Serial.flush();
    HMISerial.begin(SENSOR_BAUDRATE);
    xSensorMutex = xSemaphoreCreateMutex();
    if (xSensorMutex == NULL) {
        Serial.println("!"); Serial.flush();
    } else {
        Serial.println("."); Serial.flush();
    }
    Serial.println("s"); Serial.flush();
}

void ProcessDeviceControl(uint8_t head, uint8_t value)
{
    uint8_t bitIndex = getDeviceBitFromHead(head);
    if (bitIndex > 7) {
        ShowMsg("[HMI] Invalid device head: 0x" + String(head, HEX), true);
        return;
    }

    uint16_t outputState = myModbusRTU.hreg(12);
    uint16_t bitMask = (1 << bitIndex);

    if (value != 0)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;

    myModbusRTU.setHreg(12, outputState);

    ShowMsg("[HMI] Device 0x" + String(head, HEX) + " bit" + String(bitIndex)
            + " " + (value ? "ON" : "OFF"), true);
}

/**
 * @brief 判断字节是否为有效命令头
 * @note 有效命令头: 0x01, 0x02, 0x0A~0x0D, 0x0E, 0x0F, 0x10~0x80
 *       VT内部协议帧头 0xEE 和传感器数据标志 0x03~0x06 需要过滤
 */
static bool isValidCommandHead(uint8_t byte)
{
    // 数值参数或参数组
    if (byte == 0x01 || byte == 0x02) return true;
    // 阈值设置
    if (byte >= 0x0A && byte <= 0x0D) return true;
    // 参数控制
    if (byte == 0x0E || byte == 0x0F) return true;
    // 设备控制 (0x10, 0x20, 0x30, ..., 0x80)
    if (byte >= 0x10 && byte <= 0x80 && (byte & 0x0F) == 0) return true;
    return false;
}

/**
 * @brief 根据命令头获取期望帧长度
 */
static uint8_t getExpectedFrameLen(uint8_t head)
{
    if (head == 0x01) return 6;                               // 数值参数
    if (head == 0x02 || head == 0x0E || head == 0x0F) return 4; // 参数组/控制
    // 阈值 0x0A~0x0D 和设备控制 0x10~0x80 都是3字节
    return 3;
}

void HMIReceiveTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    ShowMsg("hT", true);  // HMI task started

    uint8_t rxBuffer[8];
    uint8_t rxIdx = 0;
    uint32_t lastAlivePrint = 0;

    while (true)
    {
        while (HMISerial.available() > 0)
        {
            uint8_t byte = HMISerial.read();

            // 诊断打印（每字节）
            ShowMsg("[HMI] RX byte: 0x" + String(byte, HEX), true);

            if (rxIdx == 0)
            {
                // 首字节过滤：只接受有效命令头，跳过VT协议数据和传感器回显
                if (isValidCommandHead(byte))
                {
                    rxBuffer[0] = byte;
                    rxIdx = 1;
                }
                // 否则丢弃该字节
            }
            else
            {
                rxBuffer[rxIdx++] = byte;
                uint8_t expectedLen = getExpectedFrameLen(rxBuffer[0]);

                if (rxIdx == expectedLen)
                {
                    // 校验帧头==帧尾
                    if (rxBuffer[0] == rxBuffer[expectedLen - 1])
                    {
                        uint8_t head = rxBuffer[0];

                        // --- 设备控制: 0x10~0x80 (3字节) ---
                        if (head >= HMI_CMD_DEV_BASE && head <= HMI_CMD_DEV_END && (head & 0x0F) == 0)
                        {
                            ProcessDeviceControl(head, rxBuffer[1]);
                        }
                        // --- 阈值设置: 0x0A~0x0D (3字节) ---
                        else if (head >= HMI_CMD_THRESHOLD_A && head <= HMI_CMD_THRESHOLD_D)
                        {
                            // 阈值值在 rxBuffer[1]，为文本字符
                            uint16_t hregAddr = 30 + (head - HMI_CMD_THRESHOLD_A);
                            uint16_t thresholdVal = (uint16_t)(rxBuffer[1]);
                            myModbusRTU.setHreg(hregAddr, thresholdVal);
                            ShowMsg("[HMI] Threshold 0x" + String(head, HEX)
                                    + " → Hreg" + String(hregAddr)
                                    + " = " + String(thresholdVal), true);
                        }
                        // --- 参数控制: 0x0E/0x0F (4字节) ---
                        else if (head == HMI_CMD_PARAM_E)
                        {
                            // rxBuffer[1]=val1, rxBuffer[2]=val2
                            ShowMsg("[HMI] Param E: " + String(rxBuffer[1]) + ", " + String(rxBuffer[2]), true);
                        }
                        else if (head == HMI_CMD_PARAM_F)
                        {
                            ShowMsg("[HMI] Param F: " + String(rxBuffer[1]) + ", " + String(rxBuffer[2]), true);
                        }
                        // --- 数值参数: 0x01 (6字节) ---
                        else if (head == HMI_CMD_NUMERIC)
                        {
                            uint16_t co1 = ((uint16_t)rxBuffer[1] << 8) | rxBuffer[2];
                            uint16_t co2 = ((uint16_t)rxBuffer[3] << 8) | rxBuffer[4];
                            ShowMsg("[HMI] Numeric: co1=" + String(co1) + " co2=" + String(co2), true);
                        }
                        // --- 参数组: 0x02 (4字节) ---
                        else if (head == HMI_CMD_PARAM_GROUP)
                        {
                            ShowMsg("[HMI] Param Group: " + String(rxBuffer[1]) + ", " + String(rxBuffer[2]), true);
                        }
                    }
                    else
                    {
                        ShowMsg("[HMI] Frame mismatch: head=0x" + String(rxBuffer[0], HEX)
                                + " tail=0x" + String(rxBuffer[expectedLen - 1], HEX), true);
                    }
                    rxIdx = 0;  // 处理完毕，重置索引
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));

        // 每3秒打印一次心跳，确认任务存活
        if (millis() - lastAlivePrint > 3000)
        {
            lastAlivePrint = millis();
            ShowMsg("[HMI] task alive, HMISerial.available=" + String(HMISerial.available()), true);
        }
    }
}

void SensorStateMachineTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ShowMsg("sT", true);  // Sensor task started

    uint8_t txFrame[8];
    uint8_t rxBuffer[16];
    SensorState state = STATE_READ_TEMP_HUMID;
    uint32_t stateEntryTime = 0;
    uint32_t lastPollTime = 0;

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

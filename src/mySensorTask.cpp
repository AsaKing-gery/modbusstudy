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

    HMISerial.setRx(HMI_USART_RX);
    HMISerial.setTx(HMI_USART_TX);
    HMISerial.begin(SENSOR_BAUDRATE, SERIAL_8N1);

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
    case FAN1_OPEN:  fanPin = FAN1_PIN; turnOn = true;  break;
    case FAN2_OPEN:  fanPin = FAN2_PIN; turnOn = true;  break;
    case FAN3_OPEN:  fanPin = FAN3_PIN; turnOn = true;  break;
    case FAN4_OPEN:  fanPin = FAN4_PIN; turnOn = true;  break;
    case FAN1_CLOSE: fanPin = FAN1_PIN; turnOn = false; break;
    case FAN2_CLOSE: fanPin = FAN2_PIN; turnOn = false; break;
    case FAN3_CLOSE: fanPin = FAN3_PIN; turnOn = false; break;
    case FAN4_CLOSE: fanPin = FAN4_PIN; turnOn = false; break;
    default:
        ShowMsg("Unknown fan cmd:" + String(cmd, HEX), true);
        return;
    }

    uint16_t outputState = myModbusRTU.hreg(12);
    uint16_t bitMask = 0;
    if (fanPin == FAN1_PIN) bitMask = 0x40;
    else if (fanPin == FAN2_PIN) bitMask = 0x80;
    else if (fanPin == FAN3_PIN) bitMask = 0x100;
    else if (fanPin == FAN4_PIN) bitMask = 0x200;

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
    case HUMI1_OPEN:  humiPin = HUMI1_PIN; turnOn = true;  break;
    case HUMI2_OPEN:  humiPin = HUMI2_PIN; turnOn = true;  break;
    case HUMI3_OPEN:  humiPin = HUMI3_PIN; turnOn = true;  break;
    case HUMI4_OPEN:  humiPin = HUMI4_PIN; turnOn = true;  break;
    case HUMI1_CLOSE: humiPin = HUMI1_PIN; turnOn = false; break;
    case HUMI2_CLOSE: humiPin = HUMI2_PIN; turnOn = false; break;
    case HUMI3_CLOSE: humiPin = HUMI3_PIN; turnOn = false; break;
    case HUMI4_CLOSE: humiPin = HUMI4_PIN; turnOn = false; break;
    default:
        ShowMsg("Unknown humi cmd:" + String(cmd, HEX), true);
        return;
    }

    uint16_t outputState = myModbusRTU.hreg(12);
    uint16_t bitMask = 0;
    if (humiPin == HUMI1_PIN) bitMask = 0x04;
    else if (humiPin == HUMI2_PIN) bitMask = 0x08;
    else if (humiPin == HUMI3_PIN) bitMask = 0x10;
    else if (humiPin == HUMI4_PIN) bitMask = 0x20;

    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;

    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Humi cmd:" + String(cmd, HEX) + " pin:" + String(humiPin) + " " + (turnOn ? "ON" : "OFF"), true);
}

void HMIReceiveTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    ShowMsg("HMI Receive task started", true);

    uint8_t hmiRxBuffer[8];
    uint8_t hmiRxIndex = 0;
    uint32_t hmiLastByteTime = 0;

    while (true)
    {
        while (HMISerial.available() > 0)
        {
            if (hmiRxIndex < sizeof(hmiRxBuffer))
            {
                hmiRxBuffer[hmiRxIndex++] = HMISerial.read();
                hmiLastByteTime = millis();
            }
            else
            {
                HMISerial.read(); // 缓冲区满，丢弃
            }
        }

        if (hmiRxIndex >= 2 && (millis() - hmiLastByteTime) >= 5)
        {
            uint8_t flag = hmiRxBuffer[0];
            uint8_t cmd  = hmiRxBuffer[1];

            if (flag == HMI_FLAG_FAN_CTRL)
            {
                ProcessFanControl(cmd);
            }
            else if (flag == HMI_FLAG_HUMI_CTRL)
            {
                ProcessHumiControl(cmd);
            }
            else
            {
                ShowMsg("Unknown HMI flag: 0x" + String(flag, HEX), true);
            }

            // 处理完后清空缓冲区
            hmiRxIndex = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void SensorStateMachineTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ShowMsg("SensorStateMachine task started", true);

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

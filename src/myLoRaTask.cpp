#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <RadioLib.h>
#include "myLoRaTask.h"
#include "myModbus.h"
#include "myDeviceState.h"
#include "mySensorTask.h"
#include "myShowMsg.h"

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
        myModbusRTU.setHreg(LORA_MODBUS_REG_TEMP, rawValue);
        deviceState.loraTemperature = actualValue;
        SendToHMI_LoRa(flag, actualValue, 1);
        ShowMsg("LoRa Temp: " + String(actualValue, 1), true);
        break;

    case LORA_FLAG_HUMID:
        actualValue = rawValue / 10.0f;
        myModbusRTU.setHreg(LORA_MODBUS_REG_HUMID, rawValue);
        deviceState.loraHumidity = actualValue;
        SendToHMI_LoRa(flag, actualValue, 1);
        ShowMsg("LoRa Humid: " + String(actualValue, 1), true);
        break;

    case LORA_FLAG_CO2:
        actualValue = (float)rawValue;
        myModbusRTU.setHreg(LORA_MODBUS_REG_CO2, rawValue);
        deviceState.loraCo2 = actualValue;
        SendToHMI_LoRa(flag, actualValue, 0);
        ShowMsg("LoRa CO2: " + String((int)actualValue), true);
        break;

    case LORA_FLAG_NH3:
        actualValue = rawValue / 100.0f;
        myModbusRTU.setHreg(LORA_MODBUS_REG_NH3, rawValue);
        deviceState.loraNh3 = actualValue;
        SendToHMI_LoRa(flag, actualValue, 2);
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
    vTaskDelay(pdMS_TO_TICKS(1000));
    ShowMsg("LoRa Receive task started", true);

    xLoRaTaskHandle = xTaskGetCurrentTaskHandle();

    int16_t initState = Ra01S_Init();
    if (initState != RADIOLIB_ERR_NONE)
    {
        ShowMsg("LoRa task exit due to init failure", true);
        xLoRaTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int16_t rxState = Ra01S_StartReceive();
    if (rxState != RADIOLIB_ERR_NONE)
    {
        ShowMsg("LoRa task exit due to RX start failure", true);
        xLoRaTaskHandle = NULL;
        vTaskDelete(NULL);
        return;
    }

    attachInterrupt(digitalPinToInterrupt(RA01S_DIO1), LoRa_DIO1_ISR, RISING);
    ShowMsg("LoRa DIO1 interrupt attached", true);

    while (true)
    {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));

        int16_t state = radio.readData(loraRxBuffer, LORA_FRAME_LEN);

        if (state == RADIOLIB_ERR_NONE)
        {
            ShowMsg("LoRa RX: " + String(loraRxBuffer[0], HEX) + " " +
                    String(loraRxBuffer[1], HEX) + " " +
                    String(loraRxBuffer[2], HEX), true);

            ParseLoRaFrame(loraRxBuffer, LORA_FRAME_LEN);

            radio.startReceive();
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            ShowMsg("LoRa CRC error", true);
            radio.startReceive();
        }
        else if (state != RADIOLIB_ERR_NONE)
        {
            ShowMsg("LoRa RX error, code: " + String(state), true);
            radio.startReceive();
        }
    }
}

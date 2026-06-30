#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "myModbus.h"
#include "Parameter_Config.h"
#include "myShowMsg.h"

void ModbusRTU_Initialize()
{
    ShowMsg("ModbusRTU initializing", true);
    myModbusRTU.setSlaveId(myPar.SlaveId);
    myModbusRTU.config(myPar.Baudrate);
    mbSerial.begin(myPar.Baudrate, SERIAL_8N1);
    for (int i = 0; i < MaxModbusRegNum; i++)
    {
        myModbusRTU.addHreg(i, 0);
    }
    // 上电默认：Y2-Y9 全部断开 (bit2-bit9 = 0)
    myModbusRTU.setHreg(12, 0x0000);
    _regs_head = myModbusRTU._regs_head;
    _regs_last = myModbusRTU._regs_last;
    ShowMsg("ModbusRTU initialized", true);
}

void ModbusTCP_Initialize()
{
    ShowMsg("ModbusTCP initializing", true);
    myModbusTCP.config(myPar.mac, myPar.ip);
    myModbusTCP._regs_head = _regs_head;
    myModbusTCP._regs_last = _regs_last;
    ShowMsg("ModbusTCP IP: " + String(myPar.ip[0]) + "." + String(myPar.ip[1]) + "." + String(myPar.ip[2]) + "." + String(myPar.ip[3]), true);
    ShowMsg("ModbusTCP initialized", true);
}

void ModbusRTUTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ShowMsg("ModbusRTU task started", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (xSensorMutex != NULL && xSemaphoreTake(xSensorMutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            myModbusRTU.task();
            xSemaphoreGive(xSensorMutex);
        }
    }
}

void ModbusTCPTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(100));
    ShowMsg("ModbusTCP task started", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
        myModbusTCP.task();
    }
}

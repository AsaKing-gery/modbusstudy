#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <IWatchdog.h>
#include "myTask.h"
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myModbus.h"
#include "mySensorTask.h"
// #include "myMQTT_TLS.h"
// #include "myLoRaTask.h"
#include "myShowMsg.h"

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

void Load_ParameterTORegister(void)
{
    myModbusRTU.setHreg(0, Version);
    myModbusRTU.setHreg(1, myPar.SlaveId);
    myModbusRTU.setHreg(2, myPar.Baudrate);
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
    myPar.mac[5] = myModbusRTU.hreg(7) & 0xFF;

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
    uint16_t Output_Temp = 0;
    uint16_t Param_Temp = 0;
    bool runLedTemp = false;

    Load_ParameterTORegister();
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

        // 删除数字输入，Hreg(11)恒为0
        myModbusRTU.setHreg(11, 0);

        if (myModbusRTU.hreg(12) != Output_Temp)
        {
            Output_Temp = myModbusRTU.hreg(12);
            digitalWrite(Output_Y1, (Output_Temp & 0x01) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y2, (Output_Temp & 0x02) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y3, (Output_Temp & 0x04) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y4, (Output_Temp & 0x08) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y5, (Output_Temp & 0x10) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y6, (Output_Temp & 0x20) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y7, (Output_Temp & 0x40) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y8, (Output_Temp & 0x80) > 0 ? LOW : HIGH);
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
    xTaskCreate(ModbusRTUTask, "ModbusRTUSevice", 128, NULL, 5, NULL);
    // xTaskCreate(ModbusTCPTask, "ModbusTCPTask", 128 * 2, NULL, 5, NULL);
    // IIC/ADS1115 已移除
    xTaskCreate(MainTask, "MainTask", 128 * 2, NULL, 3, NULL);
    xTaskCreate(SensorStateMachineTask, "SensorTask", 128 * 2, NULL, 2, NULL);
    xTaskCreate(HMIReceiveTask, "HMIReceiveTask", 128 * 2, NULL, 3, NULL);
    // xTaskCreate(MQTT_TLS_Task, "MQTT_TLS_Task", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY, NULL);
    ShowMsg("Tasks OK", true);
    vTaskDelete(NULL);
}

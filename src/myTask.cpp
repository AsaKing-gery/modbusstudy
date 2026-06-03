#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <IWatchdog.h>
#include "myTask.h"
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myModbus.h"
#include "mySensorTask.h"
#include "myMQTT_TLS.h"
#include "myLoRaTask.h"
#include "myADS1115.h"
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
            digitalWrite(BOARD_LED, runLedTemp); // 板载运行指示灯PB2，高电平点亮
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

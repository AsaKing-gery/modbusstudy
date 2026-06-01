#ifndef _my_Task_H_
#define _my_Task_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <IWatchdog.h>
#include "IO_Setting.h"
#include "myModbus.h"
#include "myADS1115.h"
// #include "myExternaIO.h"

// 设定字中的位状态
#define SET_BIT_BY_BOOL(uint16_t, bitIndex, value) \
    ((value) ? ((uint16_t) |= (1 << (bitIndex))) : ((uint16_t) &= ~(1 << (bitIndex))))

// 定义是否开启任务堆栈剩余空间测试功能
// #define TaskStackTestEnable 1

// Watchdog超时时间，单位为毫秒
#define WATCHDOG_TIMEOUT_MS 400

/************************************************************************************
任务列表：
************************************************************************************/
/**
 * @brief Watchdog定时任务
 */
void WatchdogTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500)); // 延时500毫秒
    ShowMsg("Watchdog Task started", true);
    IWatchdog.begin(1000 * WATCHDOG_TIMEOUT_MS); // 启动看门狗，单位是微秒
    uint32_t lastFeedTime = millis();            // 记录上次喂狗时间
    bool witchDogTimeout = false;                // 看门狗超时标志

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms
        // 喂狗，更新喂狗时间
        if (millis() - lastFeedTime > WATCHDOG_TIMEOUT_MS / 2)
        {
            IWatchdog.reload(); // 喂狗
            lastFeedTime = millis();
            // ShowMsg("Watchdog Feed", true);
            // if (myModbusRTU.hreg(19) != 0)//用来触发看门狗超时，测试用
            // {
            //     delay(1200); // 延时1秒，等待主程序处理完Modbus数据
            // }
        }
        if (IWatchdog.isReset()) // 看门狗超时被复位过
        {
            witchDogTimeout = true;
            IWatchdog.clearReset(); // 清除复位标志
        }
        if (witchDogTimeout) // 当看门狗超时后，开始闪烁错误LED
        {
            digitalWrite(ERROR_LED, LOW); // 翻转错误LED状态
        }
    }
}

#ifdef TaskStackTestEnable
// 定义任务句柄,用来测试任务堆栈剩余空间,将&taskTest放在任务中获取句柄
TaskHandle_t taskTest;

/**
 * @brief 任务测试函数,用来测试任务堆栈剩余空间
 */
void TaskStackTest(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1000)); // 延时1000ms
    ShowMsg("Task Stack Test Task started", true);
    // 获取任务堆栈的使用情况
    UBaseType_t uxHighWaterMark;
    while (true)
    {
        uxHighWaterMark = uxTaskGetStackHighWaterMark(taskTest);
        ShowMsg("Task Stack High Water Mark " + String(uxHighWaterMark, DEC) + "words by " + pcTaskGetName(taskTest), true);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

// IIC任务,本打算用来读取ADS1115的数值，但总是无法正确读取，这里就先取消
//  /**
//   * @brief IIC任务
//   */
void IICTask(void *pvParameters)
{
    uint8_t ADS1115InitCounter = 0; // ADS1115初始化计数器

    Wire.setSDA(PB9); // 设置I2C的SDA和SCL引脚
    Wire.setSCL(PB8); // 设置I2C的SDA和SCL引脚
    Wire.begin();     // 初始化Wire库
    ShowMsg("ADS1115 Init start:", true);
    while (!InitializeADS1115())
    {
        ShowMsg("ADS1115 Init Failed,Try again!", true);
        vTaskDelay(pdMS_TO_TICKS(1000));
        ADS1115InitCounter++;
        if (ADS1115InitCounter > 5) // 尝试5次初始化失败后退出
        {
            ShowMsg("ADS1115 Init Failed,Exit!", true);
            myModbusRTU.setHreg(15, 32767);
            myModbusRTU.setHreg(16, 32767);
            myModbusRTU.setHreg(17, 32767);
            myModbusRTU.setHreg(18, 32767);
            vTaskDelete(NULL); // 退出任务
        }
    }
    ShowMsg("ADS1115 Init OK", true);

    while (true)
    {
        static uint32_t delayTime; // 延时时间

        if (millis() - delayTime > 1000) // 每隔1秒读取一次模拟量
        {
            ShowMsg("Read ADS1115...");
            ReadADS1115All(myAI.AI0, myAI.AI1, myAI.AI2, myAI.AI3); // 读取ADS1115的4个通道的模拟量,电压等于当前值当前值(Value*4.096/32767)*1.4545或者Value*0.0001818
            myModbusRTU.setHreg(15, myAI.AI0);
            myModbusRTU.setHreg(16, myAI.AI1);
            myModbusRTU.setHreg(17, myAI.AI2);
            myModbusRTU.setHreg(18, myAI.AI3);
            delayTime = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * 将参数加载到MB寄存器中
 */
void Load_ParameterTORegister(void)
{
    // 读取参数到寄存器
    myModbusRTU.setHreg(0, Version);             // 固件版本
    myModbusRTU.setHreg(1, myPar.SlaveId);       // 设备ID
    myModbusRTU.setHreg(2, baudRateSwitchState); // 波特率

    myModbusRTU.setHreg(4, myPar.Input_Filter_Time);            // 输入滤波时间
    myModbusRTU.setHreg(5, (myPar.mac[1] << 8) + myPar.mac[0]); // MAC地址字节1和2
    myModbusRTU.setHreg(6, (myPar.mac[3] << 8) + myPar.mac[2]); // MAC地址字节3和4
    myModbusRTU.setHreg(7, (myPar.mac[5] << 8) + myPar.mac[4]); // MAC地址字节5和6
    myModbusRTU.setHreg(8, (myPar.ip[2] << 8) + myPar.ip[3]);   // IP地址低位1/2
    myModbusRTU.setHreg(9, (myPar.ip[0] << 8) + myPar.ip[1]);   // IP地址高位3/4
}
/**
 * @brief 将MB寄存器参数保存到参数变量中,同时保存到EEPROM
 */
void Save_ParameterFromRegister()
{
    // 保存寄存器参数到参数变量
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

/// @brief 主任务
/// @param pvParameters
void MainTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500)); // 延时500毫秒
    ShowMsg("Main task started", true);
    uint32_t timeRecord = 0;  // 记录时间
    uint16_t Input_Temp = 0;  // 输入状态暂存
    uint16_t Output_Temp = 0; // 输出状态暂存
    uint16_t Param_Temp = 0;  // 参数操作暂存
    bool runLedTemp = false;  // 运行LED状态暂存

    Load_ParameterTORegister(); // 读取参数到寄存器
    /*打印信息*/
    ShowMsg("", true);
    ShowMsg("Parameter SaveFlag:" + String(myPar.InitFlag), true);                                                                  // 打印参数保存标志
    ShowMsg("ID:" + String(myPar.SlaveId), true);                                                                                   // 打印设备ID
    ShowMsg("BaudRate:" + String(myPar.Baudrate), true);                                                                            // 打印波特率
    ShowMsg("IP:" + String(myPar.ip[0]) + "." + String(myPar.ip[1]) + "." + String(myPar.ip[2]) + "." + String(myPar.ip[3]), true); // 打印IP地址
    ShowMsg("Port:" + String(MODBUSIP_PORT), true);                                                                                 // 打印MODBUS-TCP端口
    ShowMsg("Mac:" + String(myPar.mac[0]) + " " + String(myPar.mac[1]) + " " + String(myPar.mac[2]) + " ");                         // 打印MAC地址
    ShowMsg(String(myPar.mac[3]) + " " + String(myPar.mac[4]) + " " + String(myPar.mac[5]), true);                                  // 打印MAC地址
    ShowMsg("", true);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        /********************************参数设置********************************/
        Param_Temp = myModbusRTU.hreg(3); // 读取参数操作寄存器
        if (Param_Temp != 0)              // 非0时进行参数设置
        {
            myModbusRTU.setHreg(3, 0);
            if (Param_Temp == ParameterOption::Save) // 保存参数
            {
                IWatchdog.reload();           // 保存参数需要耗费一定时间，所以需要在保存之前喂一次狗，避免看门狗超时
                Save_ParameterFromRegister(); // 保存参数到EEPROM
            }
            else if (Param_Temp == ParameterOption::Reload) // 重新加载参数
            {
                Load_ParameterTORegister(); // 读取参数到寄存器
            }
            else if (Param_Temp == ParameterOption::Reboot) // 重启系统
            {
                NVIC_SystemReset(); // 重启系统
            }
            else if (Param_Temp == ParameterOption::Factory_Reset) // 工厂设置
            {
                IWatchdog.reload(); // 保存参数需要耗费一定时间，所以需要在保存之前喂一次狗，避免看门狗超时
                Parameter_Init();   // 重置参数
                NVIC_SystemReset(); // 重启系统
            }
            else
            {
            }
            Param_Temp = 0;
        }

        /********************************输入状态刷新********************************/
        // 将多个位字段组合成两个字节
        // 现在 combinedBytes 包含了 Input 结构体的位字段组合成的两个字节的数值
        SET_BIT_BY_BOOL(Input_Temp, 0, Input.X0);
        SET_BIT_BY_BOOL(Input_Temp, 1, Input.X1);
        SET_BIT_BY_BOOL(Input_Temp, 2, Input.X2);
        SET_BIT_BY_BOOL(Input_Temp, 3, Input.X3);
        SET_BIT_BY_BOOL(Input_Temp, 4, Input.X4);
        SET_BIT_BY_BOOL(Input_Temp, 5, Input.X5);
        SET_BIT_BY_BOOL(Input_Temp, 6, Input.X6);
        SET_BIT_BY_BOOL(Input_Temp, 7, Input.X7);
        myModbusRTU.setHreg(11, Input_Temp); // 将输入状态写入寄存器10

        /********************************输出状态刷新********************************/
        if (myModbusRTU.hreg(12) != Output_Temp)
        {
            Output_Temp = myModbusRTU.hreg(12);
            digitalWrite(Output_Y0, (Output_Temp & 0x01) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y1, (Output_Temp & 0x02) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y2, (Output_Temp & 0x04) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y3, (Output_Temp & 0x08) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y4, (Output_Temp & 0x10) > 0 ? LOW : HIGH);
            digitalWrite(Output_Y5, (Output_Temp & 0x20) > 0 ? LOW : HIGH);
        }

        /********************************时间刷新**********/
        if (millis() - timeRecord > 1000) // 每隔1秒刷新一次时间
        {
            timeRecord = millis();
            digitalWrite(RUN_LED, runLedTemp = !runLedTemp); // 翻转运行LED状态
            myModbusRTU.setHreg(10, timeRecord / 1000);      // 写入时间到寄存器10
        }
    }
}

/**
 * @brief 任务初始化函数
 * @note 该函数在系统启动时调用，用来创建任务
 * @return void
 */
void CreateTaskMethods(void *pvParameters)
{
    xTaskCreate(WatchdogTask,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "WatchdogTask", // 这个任务的名字，主要用来调试
                96,             // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,           // 传递给任务函数的参数
                6,              // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL            // 用于存储创建的任务句柄的指针
    );
    ShowMsg("Watchdog task created.", true);

    xTaskCreate(X_filter,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "X_filter", // 这个任务的名字，主要用来调试
                96,         // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,       // 传递给任务函数的参数
                5,          // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL        // 用于存储创建的任务句柄的指针
    );                      // 测试堆栈96还剩余66字节
    ShowMsg("Input filter task created.", true);

    xTaskCreate(ModbusRTUTask,     // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "ModbusRTUSevice", // 这个任务的名字，主要用来调试
                128,               // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,              // 传递给任务函数的参数
                4,                 // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL               // 用于存储创建的任务句柄的指针
    );                             // 测试堆栈128还剩余70字节
    ShowMsg("ModbusRTU task created.", true);

    xTaskCreate(ModbusTCPTask,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "ModbusTCPTask", // 这个任务的名字，主要用来调试
                128 * 2,         // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,            // 传递给任务函数的参数
                4,               // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL             // 用于存储创建的任务句柄的指针
    );                           // 测试堆栈128*2还剩余148字节
    ShowMsg("ModbusTCP task created.", true);

    // xTaskCreate(ModbusTASK,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
    //             "ModbusTASK", // 这个任务的名字，主要用来调试
    //             128 * 3,         // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
    //             NULL,            // 传递给任务函数的参数
    //             2,               // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
    //             NULL              // 用于存储创建的任务句柄的指针
    // );//测试堆栈128*3还剩余276字节
    // ShowMsg("Modbus task created.", true);

    xTaskCreate(IICTask,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "IICTask", // 这个任务的名字，主要用来调试
                128 * 3,   // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,      // 传递给任务函数的参数
                3,         // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL       // 用于存储创建的任务句柄的指针
    );                     // 测试堆栈128*3还剩余186字节
    ShowMsg("IIC task created.", true);

    xTaskCreate(MainTask,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "MainTask", // 这个任务的名字，主要用来调试
                128 * 2,    // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,       // 传递给任务函数的参数
                3,          // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL        // 用于存储创建的任务句柄的指针
    );                      // 测试堆栈128*2还剩余233字节
    ShowMsg("MainTask created.", true);

#ifdef TaskStackTestEnable
    // 创建任务测试函数
    xTaskCreate(TaskStackTest,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
                "TaskStackTest", // 这个任务的名字，主要用来调试
                128 * 2,         // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
                NULL,            // 传递给任务函数的参数
                2,               // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
                NULL             // 用于存储创建的任务句柄的指针
    );
    ShowMsg("TaskStackTest created.", true);
#endif

    ShowMsg("All Task Create Success", true);
    ShowMsg("", true);
    // 删除当前任务
    vTaskDelete(NULL);
}
#endif
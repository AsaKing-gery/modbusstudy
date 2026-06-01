#include <Arduino.h>
#include "myShowMsg.h"
#include "Parameter_Config.h"
#include "IO_Setting.h"
#include "myTask.h"

/*
//由于STM32在Arduino中默认使用的是内部RC时钟,时钟频率64MHz，所以需要修改系统时钟配置
//下面外部时钟配置，晶振频率8MHz，PLL时钟频率72MHz
//将下面的内容替换掉SystemClock_Config()函数中原有的配置
//文件相对路径：.platformio\packages\framework-arduinoststm32\variants\STM32F1xx\F103C8T_F103CB(T-U)\generic_clock.c
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9; // 8MHz * 9 = 72MHz
*/
void ShowSystemInfo()
{
#ifdef UseSerialPrint
  
  // 获取系统时钟配置
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  uint32_t FF;
  HAL_RCC_GetClockConfig(&RCC_ClkInitStruct, &FF);
  ShowMsg("SYSCLKSource:" + String(RCC_ClkInitStruct.SYSCLKSource), true);
  ShowMsg("Clock Type:" + String(RCC_ClkInitStruct.ClockType), true);
  ShowMsg("AHBCLKDivider:" + String(RCC_ClkInitStruct.AHBCLKDivider), true);
  ShowMsg("APB1CLKDivider:" + String(RCC_ClkInitStruct.APB1CLKDivider), true);
  ShowMsg("APB2CLKDivider:" + String(RCC_ClkInitStruct.APB2CLKDivider), true);
  ShowMsg("", true);
  
  // 获取系统时钟配置
  RCC_OscInitTypeDef oscinitstruct;
  HAL_RCC_GetOscConfig(&oscinitstruct);
  ShowMsg("LSEState:" + String(oscinitstruct.LSEState), true);
  ShowMsg("HSEState:" + String(oscinitstruct.HSEState), true);
  ShowMsg("PLL.PLLMUL:" + String(oscinitstruct.PLL.PLLMUL), true);
  ShowMsg("PLL.PLLSource:" + String(oscinitstruct.PLL.PLLSource), true);
  ShowMsg("PLL.PLLState:" + String(oscinitstruct.PLL.PLLState), true);
  ShowMsg("", true);
  
  // 获取系统时钟频率
  ShowMsg("Clock:" + String(HAL_RCC_GetSysClockFreq()), true);
  // 获取MCU ID
  ShowMsg("MCU ID 0:" + String(GetMCUId(), HEX), true);
  ShowMsg("MCU ID 1:" + String(GetMCUId(1), HEX), true);
  ShowMsg("MCU ID 2:" + String(GetMCUId(2), HEX), true);
  ShowMsg("", true);
#endif
}

/**
 * @brief 系统初始化函数
 */
void setup()
{
  /*硬件层初始化*/
  Serial.setTx(PA9);    // 发送信息端口重定向
  Serial.setRx(PA10);   // 发送信息端口重定向
  Serial.begin(115200); // 串口初始化
  Serial.println("System Version:" + String(Version));
  Serial.println("Remote IO System Start...");

  ShowMsg("", true);
  // 显示系统信息
  ShowSystemInfo();
  // 加载参数
  Load_Parameter();
  // GPIO初始化
  GPIO_Init();
  // Modbus应用协议初始化
  ModbusRTU_Initialize();
  ModbusTCP_Initialize();
  ShowMsg("", true);
  ShowMsg("Setup Init Success", true);
  ShowMsg("", true);

  /*任务初始化,创建所有任务*/
  xTaskCreate(CreateTaskMethods,   // 任务函数的指针，用来调用执行的函数，主要该函数内部必须一直循环，否则会触发看门狗定时器
              "CreateTaskMethods", // 这个任务的名字，主要用来调试
              128,                 // 任务的堆栈大小,单位为字，这里分配了128个字堆栈(这个堆栈大小可以通过读取堆栈高水位来检查和调整)
              NULL,                // 传递给任务函数的参数
              0,                   // 任务的优先级(优先级，3 (configMAX_PRIORITIES - 1)是最高的，0是最低的。)
              NULL                 // 用于存储创建的任务句柄的指针
  );
  // 开始任务调度器
  vTaskStartScheduler();
}

void loop()
{
  // NVIC_SystemReset();//重启系统
}

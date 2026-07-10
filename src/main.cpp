#include <Arduino.h>
#include "myShowMsg.h"
#include "Parameter_Config.h"
#include "IO_Setting.h"
// #include "myNetworkConfig.h"
#include "myTask.h"
// #include "myK210.h"
// #include "myESP32C6.h"
// #include "myMQTT_TLS.h"

/*
// ============================================
// F407时钟树配置说明
// ============================================
// 天空星F407VET6板载8MHz外部晶振(HSE)
// 目标: SYSCLK = 168MHz
//
// 时钟路径: HSE(8MHz) → PLL_M(div/8) → VCO_in(1MHz) → PLL_N(mul×336) → VCO_out(336MHz)
//           → PLL_P(div/2) → SYSCLK(168MHz)
//           → PLL_Q(div/7) → USB(48MHz)
//
// 总线分频:
//   AHB (HCLK)  = SYSCLK / 1 = 168MHz
//   APB1 (PCLK1) = HCLK / 4  = 42MHz  (低速总线: USART2/3, I2C, SPI2/3)
//   APB2 (PCLK2) = HCLK / 2  = 84MHz  (高速总线: USART1, SPI1, TIM1, ADC)
//
// Arduino框架自动完成时钟初始化，无需手动修改SystemClock_Config()
// 只需在platformio.ini中设置 board_build.f_cpu = 168000000L
// ============================================
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
  ShowMsg("PLL.PLLM:" + String(oscinitstruct.PLL.PLLM), true);
  ShowMsg("PLL.PLLSource:" + String(oscinitstruct.PLL.PLLSource), true);
  ShowMsg("PLL.PLLState:" + String(oscinitstruct.PLL.PLLState), true);
  ShowMsg("", true);
  
  // 获取系统时钟频率
  ShowMsg("Clock:" + String(HAL_RCC_GetSysClockFreq()), true);
  // 获取MCU ID - 使用裸Serial避免String+ShowMsg开销
  Serial.print("ID0:"); Serial.println(GetMCUId(), HEX); Serial.flush();
  Serial.print("ID1:"); Serial.println(GetMCUId(1), HEX); Serial.flush();
  Serial.print("ID2:"); Serial.println(GetMCUId(2), HEX); Serial.flush();
  Serial.println(); Serial.flush();
#endif
}

/**
 * @brief 系统初始化函数
 */
void setup()
{
  /*硬件层初始化*/
  Serial.setTx(PD5);    // 发送信息端口重定向 (新板PD5=K210_TX, 调试与K210共用)
  Serial.setRx(PD6);   // 发送信息端口重定向 (新板PD6=K210_RX, 调试与K210共用)
  Serial.begin(115200); // 串口初始化
  Serial.println("System Version:" + String(Version));
  Serial.println("Remote IO System Start...");
  // 显示系统信息
    Serial.println("1"); Serial.flush();
    ShowSystemInfo();
    Serial.println("2"); Serial.flush();
    // 加载参数
    Load_Parameter();
    Serial.println("3"); Serial.flush();
    // GPIO初始化
    GPIO_Init();
    Serial.println("4"); Serial.flush();
    // 网络初始化（DHCP或静态IP）
    // Network_Init();
    // Modbus应用协议初始化
    ModbusRTU_Initialize();
    // ModbusTCP_Initialize();
    // ESP32C6 SPI初始化
    // ESP32C6_SPI_Init();
    // K210摄像头模块初始化
    // K210_Initialize();
    // 传感器串口和串口屏串口初始化
    SensorSerial_Init();
    // MQTT over TLS 初始化
    // MQTT_TLS_Init();
    ShowMsg("s", true);
    ShowMsg("OK", true);

    /*任务初始化,创建所有任务*/
    xTaskCreate(CreateTaskMethods,
                "CreateTaskMethods",
                128,
                NULL,
                0,              // 最低优先级，创建完任务后自删除
                NULL
    );
    // 开始任务调度器
    vTaskStartScheduler();
}

void loop()
{
  // NVIC_SystemReset();//重启系统
}

/* FreeRTOS 钩子函数实现 */

/**
 * @brief 堆栈溢出钩子函数
 * @param xTask 发生溢出的任务句柄
 * @param pcTaskName 发生溢出的任务名称
 */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  Serial.println("[ERROR] Stack overflow detected!");
  Serial.print("Task: ");
  Serial.println(pcTaskName);
  // 进入死循环，等待看门狗复位或调试
  for (;;)
  {
  }
}

/**
 * @brief 内存分配失败钩子函数
 */
extern "C" void vApplicationMallocFailedHook(void)
{
  Serial.println("[ERROR] Memory allocation failed!");
  // 进入死循环，等待看门狗复位或调试
  for (;;)
  {
  }
}

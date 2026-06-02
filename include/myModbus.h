#ifndef _my_MODBUS_H_
#define _my_MODBUS_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Parameter_Config.h> //包含参数配置头文件
#include <ModbusSerial.h>     //用于ModbusRTU串行通信
#include <ModbusEthernet.h>   //用于ModbusTCP网络通信

/*Modbus寄存器相关参数
1.这声明的两个指针用于保存Modbus的寄存器指针，可以让ModbusTCP和ModbusRTU共享寄存器,否则RTU和TCP各自有一份寄存器。
2.同时需要将Modbus库中的下面两个寄存器从私有成员中移动到公有成员中。
TRegister *_regs_head;
TRegister *_regs_last;
*/
extern TRegister *_regs_head; // 头指针
extern TRegister *_regs_last; // 尾指针

/* RS485总线互斥锁 - 传感器任务和ModbusRTU任务共用 */
extern SemaphoreHandle_t xSensorMutex;

/*modbusRTU相关参数*/
extern HardwareSerial mbSerial;                      // 用于通信的串口重定向
extern ModbusSerial myModbusRTU; // 声明一个ModbusRTU实例，传入串口编号和站号，如果使用了RS485，并且有发送使能引脚，可以给一个发送使能引脚

/*modbusTCP相关参数*/
/*注意将ModbusEthernet.h中的TCP_KEEP_ALIVE定义打开，否则每次传送完成后都会关闭TCP连接，导致网络通信断开*/
// byte ip[] = {192, 168, 1, 120};//IP地址
extern ModbusEthernet myModbusTCP; // 声明一个ModbusTCP实例

/*ModbusRTU初始化*/
void ModbusRTU_Initialize();

/*modbusTCP初始化*/
void ModbusTCP_Initialize();

/**
 * ModbusRTU任务,在FreeRTOS中创建该任务
 * @note 该任务与传感器任务共用RS485总线，通过互斥锁保护
 */
void ModbusRTUTask(void *pvParameters);

/**
 * ModbusTCP任务,在FreeRTOS中创建该任务
 * 注意：ModbusTCP的初始化需要在该任务中进行
 *
 */
void ModbusTCPTask(void *pvParameters);
// /**
//  * Modbus任务,在FreeRTOS中创建该任务
//  * 该任务同时处理ModbusRTU和ModbusTCP
//  * 注意：建议不要这样使用，可能会因为RTU或者TCP通信失败导致任务卡死，但我没有测试
//  *
//  */
// void ModbusTASK(void *pvParameters)
// {
//   ModbusRTU_Initialize();
//   ModbusTCP_Initialize();
//   while (true)
//   {
//     vTaskDelay(pdMS_TO_TICKS(1));
//     myModbusRTU.task(); // ModbusRTU处理函数
//     myModbusTCP.task(); // ModbusTCP处理函数
//   }
// }

#endif
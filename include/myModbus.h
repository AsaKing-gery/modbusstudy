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
TRegister *_regs_head; // 头指针
TRegister *_regs_last; // 尾指针

/*modbusRTU相关参数*/
HardwareSerial mbSerial(mbRxPin, mbTxPin);                      // 用于通信的串口重定向
ModbusSerial myModbusRTU(mbSerial, myPar.SlaveId, mbSendEnPin); // 声明一个ModbusRTU实例，传入串口编号和站号，如果使用了RS485，并且有发送使能引脚，可以给一个发送使能引脚

/*modbusTCP相关参数*/
/*注意将ModbusEthernet.h中的TCP_KEEP_ALIVE定义打开，否则每次传送完成后都会关闭TCP连接，导致网络通信断开*/
// byte ip[] = {192, 168, 1, 120};//IP地址
ModbusEthernet myModbusTCP; // 声明一个ModbusTCP实例

/*ModbusRTU初始化*/
void ModbusRTU_Initialize()
{
  ShowMsg("ModbusRTU initializing", true);
  myModbusRTU.setSlaveId(myPar.SlaveId);      // 设置站号
  myModbusRTU.config(myPar.Baudrate);         // ModbusRTU开始，需要依靠串口来工作
  mbSerial.begin(myPar.Baudrate, SERIAL_8N1); // 串口开始工作
  // ModbusRTU的保持寄存器配置
  for (int i = 0; i < MaxModbusRegNum; i++) // 添加20个保持寄存器
  {
    // myModbusRTU.addCoil(i, true); // 添加输出线圈
    // myModbusRTU.addIsts(i, true); // 添加离散输入
    // myModbusRTU.addIreg(i, i);    // 添加输入寄存器
    // myModbusRTU.addHreg(i+10, i);//地址可以不从地址0开始
    myModbusRTU.addHreg(i, 0); // 添加保持寄存器
  }
  // 将ModbusRTU的寄存器指针保存到全局变量中,便于ModbusTCP访问，注意这里必须等寄存器初始化完成后再保存，否则保存的是空指针会导致ModbusTCP无法正确访问
  _regs_head = myModbusRTU._regs_head;
  _regs_last = myModbusRTU._regs_last;
  ShowMsg("ModbusRTU initialized", true);
}

/*modbusTCP初始化*/
void ModbusTCP_Initialize()
{
  ShowMsg("ModbusTCP initializing", true);
  myModbusTCP.config(myPar.mac, myPar.ip); // 注意在ModbusEthernet.h中有一个#define TCP_KEEP_ALIVE，需要将保持连接打开，否则网络通信将自动断开
  // 全局变量中已经保存了ModbusRTU的寄存器指针，所以ModbusTCP中访问的寄存器指针也指向到ModbusRTU的寄存器
  myModbusTCP._regs_head = _regs_head;
  myModbusTCP._regs_last = _regs_last;
  // for (int i = 0; i < 100; i++)//这里不再创建数据区，与ModbusRTU共享数据
  // {
  //   // myModbusTCP.addCoil(i, true); // 添加输出线圈
  //   // myModbusTCP.addIsts(i, true); // 添加离散输入
  //   // myModbusTCP.addIreg(i, i);    // 添加输入寄存器
  //   // myModbusTCP.addHreg(i, i * 10);
  //   // myModbusTCP.addHreg(i + 10, i * 20); // 地址可以不从地址0开始
  //   myModbusTCP.addHreg(i, i + 1000);
  // }
  ShowMsg("ModbusTCP initialized", true);
}

/**
 * ModbusRTU任务,在FreeRTOS中创建该任务
 */
void ModbusRTUTask(void *pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms再启动任务
  ShowMsg("ModbusRTU task started", true);
  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1));
    myModbusRTU.task(); // ModbusRTU处理函数
  }
}

/**
 * ModbusTCP任务,在FreeRTOS中创建该任务
 * 注意：ModbusTCP的初始化需要在该任务中进行
 *
 */
void ModbusTCPTask(void *pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(100)); // 延时100ms再启动任务
  ShowMsg("ModbusTCP task started", true);
  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1));
    myModbusTCP.task(); // ModbusTCP处理函数
  }
}
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
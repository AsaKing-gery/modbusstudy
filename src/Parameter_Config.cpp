#include <Arduino.h>
#include <EEPROM.h>
#include "Parameter_Config.h"
#include "myShowMsg.h"

/*
 * EEPROM 布局 (Flash 模拟, 共18字节):
 *   Offset  0: InitFlag          (1B)
 *   Offset  1: SlaveId           (1B)
 *   Offset  2: Baudrate          (4B)
 *   Offset  6: MAC[6]            (6B)
 *   Offset 12: IP[4]             (4B)
 *   Offset 16: Input_Filter_Time (2B)
 */

#define EE_ADDR_FLAG            0
#define EE_ADDR_SLAVEID         1
#define EE_ADDR_BAUDRATE        2
#define EE_ADDR_MAC             6
#define EE_ADDR_IP              12
#define EE_ADDR_FILTER          16

uint32_t GetMCUId(uint8_t offsetIndex)
{
    offsetIndex > 3 ? offsetIndex = 3 : 0;
    return *(uint32_t *)(0x1FFF7A10 + (offsetIndex * 4));
}

void Parameter_Init()
{
    uint32_t id1 = GetMCUId();
    uint32_t id2 = GetMCUId(1);
    myPar.mac[0] = id1 & 0xFF;
    myPar.mac[1] = (id1 >> 8) & 0xFF;
    myPar.mac[2] = (id1 >> 16) & 0xFF;
    myPar.mac[3] = (id1 >> 24) & 0xFF;
    myPar.mac[4] = id2 & 0xFF;
    myPar.mac[5] = (id2 >> 8) & 0xFF;
    // 其他字段使用构造函数默认值 (InitFlag=66, SlaveId=1, Baudrate=115200, IP=192.168.1.168, Filter=5)
}

void Save_Parameter()
{
    EEPROM.update(EE_ADDR_FLAG,    myPar.InitFlag);
    EEPROM.update(EE_ADDR_SLAVEID, myPar.SlaveId);
    EEPROM.put(EE_ADDR_BAUDRATE,   myPar.Baudrate);
    for (int i = 0; i < 6; i++) EEPROM.update(EE_ADDR_MAC + i,  myPar.mac[i]);
    for (int i = 0; i < 4; i++) EEPROM.update(EE_ADDR_IP + i,   myPar.ip[i]);
    EEPROM.put(EE_ADDR_FILTER,     myPar.Input_Filter_Time);
    ShowMsg("Parameter saved", true);
}

void Load_Parameter()
{
    Serial.println("L"); Serial.flush();

    EEPROM.begin();

    if (EEPROM.read(EE_ADDR_FLAG) == 66)
    {
        // EEPROM 中有有效数据
        myPar.SlaveId   = EEPROM.read(EE_ADDR_SLAVEID);
        EEPROM.get(EE_ADDR_BAUDRATE, myPar.Baudrate);
        for (int i = 0; i < 6; i++) myPar.mac[i] = EEPROM.read(EE_ADDR_MAC + i);
        for (int i = 0; i < 4; i++) myPar.ip[i]  = EEPROM.read(EE_ADDR_IP + i);
        EEPROM.get(EE_ADDR_FILTER, myPar.Input_Filter_Time);
        myPar.InitFlag = 66;
    }
    else
    {
        // 首次启动，初始化默认值并写入 EEPROM
        Parameter_Init();
        Save_Parameter();
    }

    Serial.println("P"); Serial.flush();
}

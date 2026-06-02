#include <Arduino.h>
#include <EEPROM.h>
#include "Parameter_Config.h"
#include "myShowMsg.h"

uint32_t GetMCUId(uint8_t offsetIndex)
{
    offsetIndex > 3 ? offsetIndex = 3 : 0;
    return *(uint32_t *)(0x1FFF7A10 + (offsetIndex * 4));
}

void Save_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Saving parameter", true);
    EEPROM.put(0, myPar);
    ShowMsg("Parameter Saved:" + String(millis() - recordTime), true);
}

void Parameter_Init()
{
    ShowMsg("Initializing parameter", true);
    myPar = Parameter_Config();
    uint32_t id1 = GetMCUId();
    uint32_t id2 = GetMCUId(1);
    myPar.mac[0] = id1 & 0xFF;
    myPar.mac[1] = (id1 >> 8) & 0xFF;
    myPar.mac[2] = (id1 >> 16) & 0xFF;
    myPar.mac[3] = (id1 >> 24) & 0xFF;
    myPar.mac[4] = id2 & 0xFF;
    myPar.mac[5] = (id2 >> 8) & 0xFF;
    Save_Parameter();
}

void Load_Parameter()
{
    uint32_t recordTime = millis();
    ShowMsg("Loading parameter", true);
    EEPROM.get(0, myPar);
    if (myPar.InitFlag != 66)
    {
        Parameter_Init();
    }
    ShowMsg("Parameter loaded：" + String(millis() - recordTime), true);
}

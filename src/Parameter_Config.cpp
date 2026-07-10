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
    // EEPROM emulation on STM32F4 is unstable; skip for now.
    // When needed, use FlashStorage library instead.
    ShowMsg("Save parameter skipped (EEPROM disabled)", true);
}

void Parameter_Init()
{
    // Silent init - no prints to avoid Serial buffer issues
    uint32_t id1 = GetMCUId();
    uint32_t id2 = GetMCUId(1);
    myPar.mac[0] = id1 & 0xFF;
    myPar.mac[1] = (id1 >> 8) & 0xFF;
    myPar.mac[2] = (id1 >> 16) & 0xFF;
    myPar.mac[3] = (id1 >> 24) & 0xFF;
    myPar.mac[4] = id2 & 0xFF;
    myPar.mac[5] = (id2 >> 8) & 0xFF;
}

void Load_Parameter()
{
    Serial.println("L"); Serial.flush();
    Parameter_Init();
    Serial.println("P"); Serial.flush();
}

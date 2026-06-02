#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include "myADS1115.h"
#include "myShowMsg.h"

bool InitializeADS1115()
{
    ADS.setGain(1);
    return ADS.begin();
}

float ReadADS1115(int channel)
{
    int16_t val = ADS.readADC(channel);
    float f = ADS.toVoltage(1);
    ShowMsg("A" + String(channel) + ":" + String(val) + "\t" + String(val * f, 3), true);
    return val * f;
}

void ReadADS1115All(int16_t &val_0, int16_t &val_1, int16_t &val_2, int16_t &val_3)
{
    val_0 = ADS.readADC(0);
    val_1 = ADS.readADC(1);
    val_2 = ADS.readADC(2);
    val_3 = ADS.readADC(3);
}

void ReadADS1115All()
{
    int16_t val_0 = ADS.readADC(0);
    int16_t val_1 = ADS.readADC(1);
    int16_t val_2 = ADS.readADC(2);
    int16_t val_3 = ADS.readADC(3);

    float f = ADS.toVoltage(1);
    ShowMsg("Read ADS1115:");
    ShowMsg("\nA0:\t" + String(val_0) + "\t" + String(val_0 * f, 3) + "V");
    ShowMsg("\nA1:\t" + String(val_1) + "\t" + String(val_1 * f, 3) + "V");
    ShowMsg("\nA2:\t" + String(val_2) + "\t" + String(val_2 * f, 3) + "V");
    ShowMsg("\nA3:\t" + String(val_3) + "\t" + String(val_3 * f, 3) + "V", true);
}

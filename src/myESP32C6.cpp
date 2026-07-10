#include <Arduino.h>
#include "myESP32C6.h"
#include "IO_Setting.h"
#include "myShowMsg.h"

void ESP32C6_SPI_Init()
{
    ShowMsg("ESP32C6 SPI2 Initizing", true);
    pinMode(ESP32C6_SPI_CS, OUTPUT);
    digitalWrite(ESP32C6_SPI_CS, HIGH);
    ESP32C6_SPI3.begin(); // 新板使用SPI2 (PB13/PB14/PB15/PB12)
    ShowMsg("ESP32C6 SPI2 Initized", true);
}

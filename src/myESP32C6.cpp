#include <Arduino.h>
#include "myESP32C6.h"
#include "IO_Setting.h"
#include "myShowMsg.h"

void ESP32C6_SPI_Init()
{
    ShowMsg("ESP32C6 SPI3 Initizing", true);
    pinMode(ESP32C6_SPI_CS, OUTPUT);
    digitalWrite(ESP32C6_SPI_CS, HIGH);
    pinMode(ESP32C6_EN, OUTPUT);
    digitalWrite(ESP32C6_EN, HIGH);
    ESP32C6_SPI3.begin();
    ShowMsg("ESP32C6 SPI3 Initized", true);
}

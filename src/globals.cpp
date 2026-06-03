#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoMqttClient.h>
#include <RadioLib.h>
#include <ADS1X15.h>
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myModbus.h"
#include "myMQTT_TLS.h"
#include "myLoRaTask.h"
#include "mySensorTask.h"
#include "myADS1115.h"
#include "myNetworkConfig.h"
#include "trust_anchor.h"
#include "myK210.h"
#include "myESP32C6.h"

#ifdef UseSerialPrint
SemaphoreHandle_t xMutex = NULL;
#endif

/* ==================== IO_Setting.h 全局变量定义 ==================== */
uint8_t idSwitchState = 0;
uint8_t baudRateSwitchState = 0;
SPIClass RA01S_SPI(PB15, PB14, PB13);
GPIO_Port Input;
AnalogStruct myAI;

/* ==================== Parameter_Config.h 全局变量定义 ==================== */
Parameter_Config myPar = Parameter_Config();

/* ==================== myModbus.h 全局变量定义 ==================== */
TRegister *_regs_head = NULL;
TRegister *_regs_last = NULL;
HardwareSerial mbSerial(mbRxPin, mbTxPin);
ModbusSerial myModbusRTU(mbSerial, myPar.SlaveId, mbSendEnPin);
ModbusEthernet myModbusTCP;

/* ==================== mySensorTask.h 全局变量定义 ==================== */
SemaphoreHandle_t xSensorMutex = NULL;
HardwareSerial HMISerial(HMI_USART_RX, HMI_USART_TX);

/* ==================== myLoRaTask.h 全局变量定义 ==================== */
SX1262 radio = new Module(RA01S_SPI_NSS, RA01S_DIO1, RA01S_RESET, RA01S_BUSY);
uint8_t loraRxBuffer[LORA_FRAME_LEN];
volatile uint8_t loraRxIndex = 0;
TaskHandle_t xLoRaTaskHandle = NULL;

/* ==================== myMQTT_TLS.h 全局变量定义 ==================== */
EthernetClient ethernetClient;
SSLClient sslClient(ethernetClient, TAs, TAs_NUM, PA0, 1, SSLClient::SSL_WARN);
MqttClient mqttClient(sslClient);
volatile bool mqttConnected = false;
SemaphoreHandle_t xMQTTMutex = NULL;
SemaphoreHandle_t xSensorDataMutex = NULL;
DeviceState deviceState = {{false, false, false, false}, {false, false, false, false}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

/* ==================== myNetworkConfig.h 全局变量定义 ==================== */
volatile bool networkInitialized = false;
IPAddress localIP;
IPAddress subnetMask;
IPAddress gatewayIP;
IPAddress dnsServerIP;

/* ==================== myESP32C6.h 全局变量定义 ==================== */
SPIClass ESP32C6_SPI3(ESP32C6_SPI_MOSI, ESP32C6_SPI_MISO, ESP32C6_SPI_SCK);

/* ==================== myK210.h 全局变量定义 ==================== */
HardwareSerial k210Serial(K210_USART_RX, K210_USART_TX);

/* ==================== myADS1115.h 全局变量定义 ==================== */
ADS1115 ADS(0x48);

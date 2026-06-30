#ifndef _my_MQTT_TLS_H_
#define _my_MQTT_TLS_H_

#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include "IO_Setting.h"
#include "Parameter_Config.h"
#include "myModbus.h"

/* MQTT Broker 配置 */
#define MQTT_BROKER_HOST     "r5a14a1d.ala.cn-hangzhou.emqxsl.cn"
#define MQTT_BROKER_PORT     8883
#define MQTT_USERNAME        "chick"
#define MQTT_PASSWORD        "donghanmonian38"
#define MQTT_CLIENT_ID       "RemoteIO_"  // 后面会拼接MCU ID

/* MQTT 任务配置 */
#define MQTT_TASK_STACK_SIZE (128 * 4)
#define MQTT_TASK_PRIORITY   3
#define MQTT_RECONNECT_DELAY_MS 5000

/* W5500 Socket 配置
 * 注意: W5500有8个Socket(0-7)。
 * 当前使用情况:
 *   - Socket 0: DHCP客户端 / ModbusTCP服务器 (复用，DHCP只在初始化时使用)
 *   - Socket 1: MQTT over TLS 客户端
 *   - Socket 2-7: 保留/未使用
 *
 * 重要: ModbusEthernet库默认使用Socket 0作为TCP监听端口(502)。
 *       在Network_Init()中，DHCP协商也临时使用Socket 0，
 *       完成后释放。因此DHCP和ModbusTCP不会同时运行。
 *       MQTT使用独立的Socket 1，通过ethernetClient指定。
 *       如果未来Socket资源紧张，可考虑:
 *         1. 禁用DHCP，使用静态IP
 *         2. 修改ModbusEthernet库使用其他Socket
 */
#define MQTT_SOCKET_NUM      1

/* CA证书 - DigiCert Global Root G2
 * 用于验证MQTT Broker的服务器证书。
 *
 * SSLClient库需要使用BearSSL格式的trust anchor，而非原始PEM证书。
 * 转换步骤:
 *   1. 保存CA证书为ca.crt文件
 *   2. 使用SSLClient库自带的pycert_bearssl.py转换:
 *      python pycert_bearssl.py convert ca.crt --output trust_anchor.h
 *   3. 生成的trust_anchor.h包含TAs数组和TAs_NUM宏
 *   4. 将该头文件放入项目include目录并在globals.cpp中包含
 *
 * 当前代码中TAs数组为空，TLS连接将跳过证书验证(不安全)。
 * 生产环境必须生成并包含正确的trust anchor。
 */

/* 订阅主题 */
#define MQTT_SUB_TOPIC_CONTROL "chick/control"

/* 发布主题 */
#define MQTT_PUB_TOPIC_DATA    "chick/data"

/* 状态上报间隔 */
#define MQTT_PUBLISH_INTERVAL_MS 30000  // 30秒上报一次

/* JSON 缓冲区大小 */
#define JSON_BUFFER_SIZE 1024

/* SSLClient 和 MQTT 客户端实例 - 定义在globals.cpp中 */
extern EthernetClient ethernetClient;
extern SSLClient sslClient;
extern MqttClient mqttClient;

/* MQTT 连接状态 */
extern volatile bool mqttConnected;

/* 互斥锁 - 保护MQTT操作 */
extern SemaphoreHandle_t xMQTTMutex;

/* 风机和加湿器引脚定义 - 与mySensorTask.h保持一致 */
#define FAN1_PIN Output_Y6 // 风机1 -> Y6
#define FAN2_PIN Output_Y7 // 风机2 -> Y7
#define FAN3_PIN Output_Y8 // 风机3 -> Y8
#define FAN4_PIN Output_Y9 // 风机4 -> Y9
#define HUMI1_PIN Output_Y2 // 加湿器1 -> Y2
#define HUMI2_PIN Output_Y3 // 加湿器2 -> Y3
#define HUMI3_PIN Output_Y4 // 加湿器3 -> Y4
#define HUMI4_PIN Output_Y5 // 加湿器4 -> Y5

/* 风机和加湿器对应的Modbus寄存器位掩码 - 与mySensorTask.h保持一致 */
#define FAN1_BITMASK 0x40
#define FAN2_BITMASK 0x80
#define FAN3_BITMASK 0x100
#define FAN4_BITMASK 0x200
#define HUMI1_BITMASK 0x04
#define HUMI2_BITMASK 0x08
#define HUMI3_BITMASK 0x10
#define HUMI4_BITMASK 0x20

/* 设备状态变量和互斥锁 - 定义在myDeviceState.h中，被多个模块共享 */
#include "myDeviceState.h"

/* 函数声明 */
void MQTT_TLS_Init();
void MQTT_TLS_Task(void *pvParameters);
bool MQTT_TLS_Connect();
void MQTT_TLS_Disconnect();
void MQTT_TLS_Publish(const char *topic, const char *payload);
void MQTT_TLS_Subscribe(const char *topic);
void MQTT_TLS_KeepAlive();
void onMqttMessage(int messageSize);
void ProcessControlCommand(const String &payload);
String BuildStatusJson();
void UpdateDeviceOutputs();
void UpdateFanState(uint8_t fanIndex, bool turnOn);
void UpdateHumidifierState(uint8_t humiIndex, bool turnOn);

#endif

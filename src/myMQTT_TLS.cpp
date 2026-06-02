#include <Arduino.h>
#include <STM32FreeRTOS.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include "myMQTT_TLS.h"
#include "myModbus.h"
#include "myDeviceState.h"
#include "Parameter_Config.h"
#include "myShowMsg.h"

void MQTT_TLS_Init()
{
    ShowMsg("MQTT over TLS Initializing...", true);

    if (xMQTTMutex == NULL)
    {
        xMQTTMutex = xSemaphoreCreateMutex();
    }
    if (xSensorDataMutex == NULL)
    {
        xSensorDataMutex = xSemaphoreCreateMutex();
    }

    String clientId = String(MQTT_CLIENT_ID) + String(GetMCUId(), HEX);

    mqttClient.setId(clientId.c_str());
    mqttClient.setUsernamePassword(MQTT_USERNAME, MQTT_PASSWORD);
    mqttClient.setKeepAliveInterval(60 * 1000);
    mqttClient.setCleanSession(true);

    mqttClient.onMessage(onMqttMessage);

    ShowMsg("MQTT Client ID: " + clientId, true);
    ShowMsg("MQTT Broker: " + String(MQTT_BROKER_HOST) + ":" + String(MQTT_BROKER_PORT), true);
    ShowMsg("MQTT over TLS Initialized", true);
}

bool MQTT_TLS_Connect()
{
    if (mqttConnected)
    {
        return true;
    }

    ShowMsg("Connecting to MQTT Broker...", true);

    if (!mqttClient.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT))
    {
        ShowMsg("MQTT Connection failed! Error: " + String(mqttClient.connectError()), true);
        return false;
    }

    mqttConnected = true;
    ShowMsg("MQTT Connected successfully!", true);

    mqttClient.subscribe(MQTT_SUB_TOPIC_CONTROL);
    ShowMsg("Subscribed to: " + String(MQTT_SUB_TOPIC_CONTROL), true);

    return true;
}

void MQTT_TLS_Disconnect()
{
    if (mqttClient.connected())
    {
        mqttClient.stop();
    }
    mqttConnected = false;
    ShowMsg("MQTT Disconnected", true);
}

void MQTT_TLS_Publish(const char *topic, const char *payload)
{
    if (!mqttConnected || !mqttClient.connected())
    {
        return;
    }

    if (xSemaphoreTake(xMQTTMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        mqttClient.beginMessage(topic);
        mqttClient.print(payload);
        mqttClient.endMessage();
        xSemaphoreGive(xMQTTMutex);
    }
}

void MQTT_TLS_Subscribe(const char *topic)
{
    if (!mqttConnected || !mqttClient.connected())
    {
        return;
    }

    if (xSemaphoreTake(xMQTTMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        mqttClient.subscribe(topic);
        xSemaphoreGive(xMQTTMutex);
        ShowMsg("Subscribed: " + String(topic), true);
    }
}

void UpdateFanState(uint8_t fanIndex, bool turnOn)
{
    if (fanIndex > 3) return;

    uint32_t fanPin = 0;
    uint16_t bitMask = 0;

    switch (fanIndex)
    {
    case 0: fanPin = FAN1_PIN; bitMask = FAN1_BITMASK; break;
    case 1: fanPin = FAN2_PIN; bitMask = FAN2_BITMASK; break;
    case 2: fanPin = FAN3_PIN; bitMask = FAN3_BITMASK; break;
    case 3: fanPin = FAN4_PIN; bitMask = FAN4_BITMASK; break;
    }

    digitalWrite(fanPin, turnOn ? LOW : HIGH);
    deviceState.fan[fanIndex] = turnOn;

    uint16_t outputState = myModbusRTU.hreg(12);
    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;
    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Fan" + String(fanIndex + 1) + " set to " + (turnOn ? "ON" : "OFF"), true);
}

void UpdateHumidifierState(uint8_t humiIndex, bool turnOn)
{
    if (humiIndex > 3) return;

    uint32_t humiPin = 0;
    uint16_t bitMask = 0;

    switch (humiIndex)
    {
    case 0: humiPin = HUMI1_PIN; bitMask = HUMI1_BITMASK; break;
    case 1: humiPin = HUMI2_PIN; bitMask = HUMI2_BITMASK; break;
    case 2: humiPin = HUMI3_PIN; bitMask = HUMI3_BITMASK; break;
    case 3: humiPin = HUMI4_PIN; bitMask = HUMI4_BITMASK; break;
    }

    digitalWrite(humiPin, turnOn ? LOW : HIGH);
    deviceState.humidifier[humiIndex] = turnOn;

    uint16_t outputState = myModbusRTU.hreg(12);
    if (turnOn)
        outputState |= bitMask;
    else
        outputState &= ~bitMask;
    myModbusRTU.setHreg(12, outputState);

    ShowMsg("Humidifier" + String(humiIndex + 1) + " set to " + (turnOn ? "ON" : "OFF"), true);
}

void ProcessControlCommand(const String &payload)
{
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        ShowMsg("JSON parse failed: " + String(error.c_str()), true);
        return;
    }

    if (doc.containsKey("fan1"))
        UpdateFanState(0, doc["fan1"].as<bool>());
    if (doc.containsKey("fan2"))
        UpdateFanState(1, doc["fan2"].as<bool>());
    if (doc.containsKey("fan3"))
        UpdateFanState(2, doc["fan3"].as<bool>());
    if (doc.containsKey("fan4"))
        UpdateFanState(3, doc["fan4"].as<bool>());

    if (doc.containsKey("humidifier1"))
        UpdateHumidifierState(0, doc["humidifier1"].as<bool>());
    if (doc.containsKey("humidifier2"))
        UpdateHumidifierState(1, doc["humidifier2"].as<bool>());
    if (doc.containsKey("humidifier3"))
        UpdateHumidifierState(2, doc["humidifier3"].as<bool>());
    if (doc.containsKey("humidifier4"))
        UpdateHumidifierState(3, doc["humidifier4"].as<bool>());

    String statusJson = BuildStatusJson();
    MQTT_TLS_Publish(MQTT_PUB_TOPIC_DATA, statusJson.c_str());
}

void UpdateDeviceOutputs()
{
    UpdateFanState(0, deviceState.fan[0]);
    UpdateFanState(1, deviceState.fan[1]);
    UpdateFanState(2, deviceState.fan[2]);
    UpdateFanState(3, deviceState.fan[3]);

    UpdateHumidifierState(0, deviceState.humidifier[0]);
    UpdateHumidifierState(1, deviceState.humidifier[1]);
    UpdateHumidifierState(2, deviceState.humidifier[2]);
    UpdateHumidifierState(3, deviceState.humidifier[3]);
}

String BuildStatusJson()
{
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;

    doc["deviceId"] = String(MQTT_CLIENT_ID) + String(GetMCUId(), HEX);

    doc["fan1"] = deviceState.fan[0];
    doc["fan2"] = deviceState.fan[1];
    doc["fan3"] = deviceState.fan[2];
    doc["fan4"] = deviceState.fan[3];

    doc["humidifier1"] = deviceState.humidifier[0];
    doc["humidifier2"] = deviceState.humidifier[1];
    doc["humidifier3"] = deviceState.humidifier[2];
    doc["humidifier4"] = deviceState.humidifier[3];

    if (xSemaphoreTake(xSensorDataMutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        JsonObject local = doc.createNestedObject("local");
        local["temperature"] = deviceState.temperature;
        local["humidity"] = deviceState.humidity;
        local["co2"] = deviceState.co2;
        local["nh3"] = deviceState.nh3;

        JsonObject robot = doc.createNestedObject("robot");
        robot["temperature"] = deviceState.loraTemperature;
        robot["humidity"] = deviceState.loraHumidity;
        robot["co2"] = deviceState.loraCo2;
        robot["nh3"] = deviceState.loraNh3;

        xSemaphoreGive(xSensorDataMutex);
    }

    doc["timestamp"] = millis() / 1000;

    String output;
    serializeJson(doc, output);
    return output;
}

void onMqttMessage(int messageSize)
{
    String topic = mqttClient.messageTopic();
    String payload = "";

    while (mqttClient.available())
    {
        payload += (char)mqttClient.read();
    }

    ShowMsg("MQTT RX [" + topic + "]: " + payload, true);

    if (topic == MQTT_SUB_TOPIC_CONTROL)
    {
        ProcessControlCommand(payload);
    }
}

void MQTT_TLS_KeepAlive()
{
    if (mqttClient.connected())
    {
        mqttClient.poll();
    }
}

void MQTT_TLS_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    ShowMsg("MQTT TLS Task started", true);

    uint32_t lastReconnectAttempt = 0;
    uint32_t lastPublishTime = 0;

    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (!mqttClient.connected())
        {
            mqttConnected = false;

            if (millis() - lastReconnectAttempt > MQTT_RECONNECT_DELAY_MS)
            {
                lastReconnectAttempt = millis();
                ShowMsg("MQTT reconnecting...", true);

                if (MQTT_TLS_Connect())
                {
                    lastReconnectAttempt = 0;
                }
            }
        }
        else
        {
            MQTT_TLS_KeepAlive();

            if (millis() - lastPublishTime > MQTT_PUBLISH_INTERVAL_MS)
            {
                lastPublishTime = millis();

                String statusJson = BuildStatusJson();
                MQTT_TLS_Publish(MQTT_PUB_TOPIC_DATA, statusJson.c_str());
                ShowMsg("Status published: " + statusJson, true);
            }
        }
    }
}

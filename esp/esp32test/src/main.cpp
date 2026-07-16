#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

// ========== WiFi 配置 ==========
const char *WIFI_SSID = "asa1";
const char *WIFI_PASS = "king233.com";

// ========== 华为云 IoTDA MQTT 配置 ==========
const char *MQTT_HOST = "170f34ff8d.st1.iotda-device.cn-east-3.myhuaweicloud.com";
const int   MQTT_PORT = 8883;
const char *MQTT_CLIENT_ID = "6a3e3d8918855b39c528d5e1_test2_0_0_2026070106";
const char *MQTT_USERNAME   = "6a3e3d8918855b39c528d5e1_test2";
const char *MQTT_PASSWORD   = "140bed6e30d77b05494422b78ad771f1399f832fee09c9f545806f4aa152c61a";

// ========== 华为云 IoT 根证书（DigiCert Global Root CA） ==========
// 华为云 IoTDA 服务器证书由 DigiCert 签发
static const char *ROOT_CA_CERT =
"-----BEGIN CERTIFICATE-----\n"
"MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh\n"
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD\n"
"QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT\n"
"MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
"b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG\n"
"9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeq5Q3SdD51hnM3jWEP5g1F6dPUMBu\n"
"Qb6YfMX9BfWmGsXc/PIq4l1KfLMp1VJxW8ZEW7GGB48z1oXH4AFhZmD0Lx0i3fQJ\n"
"WcGFP5mHm2LkX8fS6L7fC7l0sF4ER5NUtPv4bqDqCqFUQTB4S4DG0+3LSE0B+1Yz\n"
"kF6iHKcO4E1xUoBQwYR1W8XCz5+LAKVx9NXwYXqAjo0RYh3Zo3VN1TOS2n1d0UOX\n"
"b4gPx0s9Zq7l6eEG4N5KDE4+HjkI+pXFuHKKBf0ZTBtTw1GIgD+bsc+9/14uvQwG\n"
"niPwxGXqXG3lFGPj0FEA7G9aHEGeC+Bf9E8GmTqE1NHwIDAQABo2MwYTAOBgNVHQ8B\n"
"Af8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQU9XvzR9Q6N4GPM0Fq\n"
"QJ0xJgJh/6AwHwYDVR0jBBgwFoAU9XvzR9Q6N4GPM0FqQJ0xJgJh/6AwDQYJKoZI\n"
"hvcNAQEFBQADggEBAAo+qg1HkvhMw/z8I6uyvMsi0mcPmUhe+fXbODSJQXTruRo/\n"
"9Myj8oA7E2qs2TeRF3vJ98Nm0N4+Oa/UUCAYOZFkDYHwFAtPxS2BUUo0H0lF6fPz\n"
"E6GFPZBfQkOw6JRHQGQjApP4Iqj0pNvfFoZvbxPRsNoDEg+H1mZ7tHBNfI/6i2dQ\n"
"fbsjBUGu4ImDCP+rv70s9Z3GQ5kQBjL1OmL6sXG0r0I5l5vOiB+P30TMNA/8IMSK\n"
"SV1ZJWnBAHrQsM4Z2Up+c3s3mk6YMlbhjRQWDBVSTw+n6lfMH3qkfhOz2bJJZv62\n"
"+NZdpl99XdP7cqRWoRIUY2AcA7e4jYSx7uxCpBo=\n"
"-----END CERTIFICATE-----\n";

// ========== SPI 配置 (ESP32 Master ↔ STM32F407 Slave) ==========
// 接线：IO12(SCK)→PC10, IO14(MISO)→PC11, IO13(MOSI)→PC12, IO15(CS)→PD3
#define SPI_SCK_PIN   12
#define SPI_MISO_PIN  14
#define SPI_MOSI_PIN  13
#define SPI_CS_PIN    15

// ========== 工业级握手协议常量 (ESP32 ↔ STM32) ==========
// MOSI帧头 (ESP32→STM32)
#define FRAME_HDR0            0xAA
#define FRAME_HDR1            0x55

// MISO握手响应帧头 (STM32→ESP32, 区别于MOSI 0xAA 0x55)
#define MISO_HS_RESP_HDR0     0xBB
#define MISO_HS_RESP_HDR1     0x66
#define MISO_HS_RESP_LEN      9   // [BB][66][state][ver:4BE][boot][crc]

// MOSI帧类型 (ESP32→STM32)
#define FRAME_SENSOR          0x01
#define FRAME_OTA_HANDSHAKE   0xF0
#define FRAME_OTA_DATA        0xF1
#define FRAME_OTA_RESULT      0xF2
#define FRAME_HEARTBEAT       0xFC
#define FRAME_HANDSHAKE_REQ   0xFD
#define FRAME_HANDSHAKE_ACK   0xFE

// MISO命令字节 (STM32→ESP32, 正常MISO输出的第一个字节)
#define STM32_CMD_IDLE        0x00
#define STM32_CMD_OTA_START       0x10
#define STM32_CMD_OTA_CANCEL      0x20
#define STM32_CMD_OTA_CHUNK_ACK   0xA5  /* STM32 块写入完成 ACK */

// 握手超时
#define HS_REQ_TIMEOUT_MS     5000
#define HS_RETRY_MAX          3
#define HEARTBEAT_INTERVAL_MS 10000  // 心跳间隔10s

// OTA服务器
#define OTA_SERVER_HOST       "10.168.81.18"
#define OTA_SERVER_PORT       8080
#define OTA_CHECK_INTERVAL_MS 60000  // 定期检查版本间隔60s

SPIClass mySPI(HSPI);  // 使用 HSPI (也可改用 VSPI)

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// 主题缓存（callback里用来判断是否是消息下发主题）
String mqttUserTopic;

// 诊断：定期发送 SPI 测试帧（固定数据，便于 STM32 侧对齐和校验）
bool sendTestFrameFlag = false;

// ========== 握手状态机变量 ==========
typedef enum {
    ESP_HS_INIT,
    ESP_HS_REQ_SENT,
    ESP_HS_GOT_RESPONSE,
    ESP_HS_READY,
    ESP_HS_OTA_IN_PROGRESS
} EspHsState_t;

static EspHsState_t hsState         = ESP_HS_INIT;
static uint32_t     hsStateEnterMs  = 0;
static int          hsRetryCount    = 0;
static uint32_t     lastHeartbeatMs = 0;
static uint32_t     stm32FwVer      = 0;  // STM32固件版本(握手时获取)

// ========== OTA 状态机变量 ==========
typedef enum {
    OTA_IDLE = 0,
    OTA_CHECKING_VER,
    OTA_DOWNLOADING,
    OTA_SENDING
} OtaState_t;

static OtaState_t otaState     = OTA_IDLE;
static uint32_t   serverFwVer  = 0;
static uint32_t   lastOtaCheck = 0;

// OTA文件缓存
static uint8_t *fwData  = NULL;
static size_t    fwSize  = 0;
static size_t    fwSent  = 0;

// 前向声明
void connectWiFi();
void connectMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void initSPI();
void spiTransfer(const uint8_t *txBuf, uint8_t *rxBuf, size_t len);

void connectWiFi() {
  Serial.print("[WiFi] 正在连接 ");
  Serial.print(WIFI_SSID);
  Serial.println(" ...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retries++;
    if (retries > 40) {
      Serial.println("\n[WiFi] 连接失败，重启中...");
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print("[WiFi] 连接成功！IP 地址: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  Serial.print("[MQTT] 收到消息，主题: ");
  Serial.println(topic);

  // 原始内容打印（便于调试）
  Serial.print("[MQTT] 内容: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // 只处理用户指定主题，解析并转发给 STM32（定点 ×100）
  if (String(topic) == mqttUserTopic) {
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
      Serial.printf("[JSON] 解析失败: %s\n", err.c_str());
      return;
    }

    JsonArray services = doc["services"];
    if (services.isNull()) {
      Serial.println("[JSON] 无 services 字段，忽略");
      return;
    }

    for (JsonObject svc : services) {
      JsonObject props = svc["properties"];
      if (props.isNull()) continue;

      // 只取温湿度、CO2、NH3；按“定点 ×100”编码，不额外缩放
      float temp = props["temp"] | 0.0f;
      float humi = props["humi"] | 0.0f;
      int   co2  = props["co2"]  | 0;
      float nh3  = props["nh3"]  | 0.0f;
      float lux  = props["lux"]  | 0.0f;

      int16_t temp100 = (int16_t)lroundf(temp * 100.0f);
      int16_t humi100 = (int16_t)lroundf(humi * 100.0f);
      int16_t nh3100  = (int16_t)lroundf(nh3  * 100.0f);
      int16_t lux100  = (int16_t)lroundf(lux  * 100.0f);

      // SPI 帧（定点版）：AA 55 01 [temp_x100 2B] [humi_x100 2B] [co2 2B] [nh3_x100 2B] [lux_x100 2B] [sum8 1B]
      uint8_t spiFrame[14];
      spiFrame[0] = 0xAA;
      spiFrame[1] = 0x55;
      spiFrame[2] = 0x01;

      spiFrame[3] = (uint8_t)((temp100 >> 8) & 0xFF);
      spiFrame[4] = (uint8_t)((temp100)      & 0xFF);

      spiFrame[5] = (uint8_t)((humi100 >> 8) & 0xFF);
      spiFrame[6] = (uint8_t)((humi100)      & 0xFF);

      spiFrame[7] = (uint8_t)((co2 >> 8) & 0xFF);
      spiFrame[8] = (uint8_t)((co2)      & 0xFF);

      spiFrame[9]  = (uint8_t)((nh3100 >> 8) & 0xFF);
      spiFrame[10] = (uint8_t)((nh3100)      & 0xFF);

      spiFrame[11] = (uint8_t)((lux100 >> 8) & 0xFF);
      spiFrame[12] = (uint8_t)((lux100)      & 0xFF);

      uint8_t sum8 = 0;
      for (int i = 2; i < 13; i++) sum8 += spiFrame[i];
      spiFrame[13] = sum8;

      uint8_t rxTmp[14] = {0};
      spiTransfer(spiFrame, rxTmp, sizeof(spiFrame));

      Serial.print("[SPI][TX] HEX:");
      for (size_t i = 0; i < sizeof(spiFrame); i++) Serial.printf(" %02X", spiFrame[i]);
      Serial.printf("  (temp_x100=%d  humi_x100=%d  co2=%d  nh3_x100=%d  lux_x100=%d)\n", temp100, humi100, co2, nh3100, lux100);
      Serial.printf("[SPI][DEBUG] len=%d  CS间隔建议>=10ms\n", (int)sizeof(spiFrame));
    }
  }
}

// 华为云/网络/重连信息重点：输出 state 文本
const char* getMqttStateString(int st) {
  switch (st) {
    case MQTT_CONNECTION_TIMEOUT: return "MQTT_CONNECTION_TIMEOUT (TCP/TLS连接超时)";
    case MQTT_CONNECTION_LOST: return "MQTT_CONNECTION_LOST (连接丢失)";
    case MQTT_CONNECT_FAILED: return "MQTT_CONNECT_FAILED (底层连接失败)";
    case MQTT_DISCONNECTED: return "MQTT_DISCONNECTED (未连接)";
    case MQTT_CONNECTED: return "MQTT_CONNECTED (已连接)";
    case MQTT_CONNECT_BAD_PROTOCOL: return "MQTT_CONNECT_BAD_PROTOCOL (协议错误)";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "MQTT_CONNECT_BAD_CLIENT_ID (clientId错误)";
    case MQTT_CONNECT_UNAVAILABLE: return "MQTT_CONNECT_UNAVAILABLE (服务不可用/握手失败)";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT_CONNECT_BAD_CREDENTIALS (用户名密码错误)";
    case MQTT_CONNECT_UNAUTHORIZED: return "MQTT_CONNECT_UNAUTHORIZED (未授权)";
    default: return "UNKNOWN";
  }
}

void testTlsConnect() {
  Serial.println("[TLS] 开始测试到华为云 MQTT 端口的连接...");
  WiFiClientSecure probe;
  probe.setInsecure(); // 只测试连通性，先不校验证书
  bool ok = probe.connect(MQTT_HOST, MQTT_PORT);
  Serial.printf("[TLS] host=%s  port=%d  result=%s\n", MQTT_HOST, MQTT_PORT, ok ? "成功" : "失败");
  probe.stop();
}

void initSPI() {
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);

  mySPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SPI_CS_PIN);
  mySPI.setFrequency(1000000);       // 1 MHz，Slave 是 F407 建议先从低速开始
  mySPI.setDataMode(SPI_MODE0);      // CPOL=0, CPHA=0
  mySPI.setBitOrder(MSBFIRST);       // MSB 先传
}

void spiTransfer(const uint8_t *txBuf, uint8_t *rxBuf, size_t len) {
  digitalWrite(SPI_CS_PIN, LOW);
  delayMicroseconds(10);             // 给 Slave 一点准备时间

  if (rxBuf) {
    for (size_t i = 0; i < len; i++) {
      rxBuf[i] = mySPI.transfer(txBuf[i]);
    }
  } else {
    for (size_t i = 0; i < len; i++) {
      mySPI.transfer(txBuf[i]);
    }
  }

  digitalWrite(SPI_CS_PIN, HIGH);
}

// ========================================================================
//                    工业级握手协议 (ESP32 侧实现)
// ========================================================================

/** CRC8: 字节求和取低8位 (与STM32侧一致) */
static uint8_t crc8sum(const uint8_t *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++) sum += data[i];
  return (uint8_t)(sum & 0xFF);
}

/** 发送握手帧: 帧头放700B末尾，确保STM32从任意时序唤醒都能捕获 */
static void esp32SendHandshakeFrame(uint8_t type, uint8_t seq) {
  uint8_t padded[700];
  /* 前695字节填0xFF (不同于0xAA避免误触发帧检测) */
  memset(padded, 0xFF, 695);
  /* 最后5字节: 握手帧头 [AA][55][type][seq][crc] */
  padded[695] = FRAME_HDR0;
  padded[696] = FRAME_HDR1;
  padded[697] = type;
  padded[698] = seq;
  padded[699] = crc8sum(padded + 697, 2);  // CRC over [type][seq]
  spiTransfer(padded, NULL, 700);
}

/** 发送心跳 (HEARTBEAT) */
static void esp32SendHeartbeat() {
  static uint8_t hbSeq = 0;
  esp32SendHandshakeFrame(FRAME_HEARTBEAT, hbSeq++);
}

/**
 * 解析 STM32 MISO 握手响应
 * 格式(9B): [BB][66][hs_state][ver:4BE][boot_status][crc]
 * @return true=解析成功, 输出ver和boot
 */
static bool esp32ParseHsResponse(const uint8_t *misoBuf, size_t misoLen,
                                  uint32_t *outVer, uint8_t *outBoot) {
  if (misoLen < (size_t)MISO_HS_RESP_LEN) return false;

  /* 扫描MISO字节流, 定位 0xBB 0x66 */
  for (size_t i = 0; i + MISO_HS_RESP_LEN <= misoLen; i++) {
    if (misoBuf[i] != MISO_HS_RESP_HDR0 || misoBuf[i+1] != MISO_HS_RESP_HDR1) continue;

    /* CRC校验 */
    uint8_t crc = crc8sum(misoBuf + i, MISO_HS_RESP_LEN - 1);
    if (crc != misoBuf[i + MISO_HS_RESP_LEN - 1]) {
      Serial.printf("[HS] CRC mismatch: calc=%02X rx=%02X\n", crc, misoBuf[i + MISO_HS_RESP_LEN - 1]);
      continue;
    }

    /* 解析字段 */
    uint32_t ver  = ((uint32_t)misoBuf[i+3] << 24) | ((uint32_t)misoBuf[i+4] << 16)
                  | ((uint32_t)misoBuf[i+5] << 8)  |  (uint32_t)misoBuf[i+6];
    uint8_t  boot = misoBuf[i+7];

    *outVer  = ver;
    *outBoot = boot;

    Serial.printf("[HS] Parsed: state=%d ver=%lu boot=%02X\n", misoBuf[i+2], ver, boot);
    return true;
  }
  return false;
}

/**
 * 读取 STM32 握手响应 (700B padding 确保 STM32 1ms 轮询有时间加载 MISO 字节)
 */
static bool esp32ReadHsResponse(uint32_t *outVer, uint8_t *outBoot) {
  uint8_t dummy[700] = {0};
  uint8_t misoRx[700] = {0};
  spiTransfer(dummy, misoRx, 700);
  return esp32ParseHsResponse(misoRx, 700, outVer, outBoot);
}

/** 握手状态机主循环 (每次loop调用) */
static void esp32RunHandshake() {
  uint32_t now = millis();

  switch (hsState) {
  case ESP_HS_INIT:
    /* 步骤1: 发送握手请求 */
    esp32SendHandshakeFrame(FRAME_HANDSHAKE_REQ, (uint8_t)hsRetryCount);
    hsState        = ESP_HS_REQ_SENT;
    hsStateEnterMs = now;
    hsRetryCount++;
    Serial.printf("[HS] Sent HANDSHAKE_REQ (attempt %d)\n", hsRetryCount);
    break;

  case ESP_HS_REQ_SENT: {
    /* 步骤2: 等待并读取MISO握手响应 */
    uint32_t ver;
    uint8_t  boot;
    if (esp32ReadHsResponse(&ver, &boot)) {
      stm32FwVer     = ver;
      hsState        = ESP_HS_GOT_RESPONSE;
      hsStateEnterMs = now;
      Serial.printf("[HS] STM32 FW ver=%lu.%lu.%lu\n",
                    ver / 10000, (ver / 100) % 100, ver % 100);
    } else if (now - hsStateEnterMs > HS_REQ_TIMEOUT_MS) {
      /* 超时重试 */
      if (hsRetryCount < HS_RETRY_MAX) {
        Serial.printf("[HS] Timeout, retry %d/%d\n", hsRetryCount + 1, HS_RETRY_MAX);
        hsState = ESP_HS_INIT;
      } else {
        Serial.println("[HS] Max retries, will retry later");
        hsState      = ESP_HS_INIT;
        hsRetryCount = 0;
      }
    }
    break;
  }

  case ESP_HS_GOT_RESPONSE:
    /* 步骤3: 发送确认，完成握手 */
    esp32SendHandshakeFrame(FRAME_HANDSHAKE_ACK, (uint8_t)(hsRetryCount - 1));
    hsState         = ESP_HS_READY;
    hsStateEnterMs  = now;
    lastHeartbeatMs = now;
    hsRetryCount    = 0;
    Serial.println("[HS] Handshake COMPLETE!");
    /* 触发版本检查 */
    otaState = OTA_CHECKING_VER;
    break;

  case ESP_HS_READY:
    /* 定时心跳 */
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
      esp32SendHeartbeat();
      lastHeartbeatMs = now;
    }
    break;

  case ESP_HS_OTA_IN_PROGRESS:
    /* OTA进行中，心跳暂停 */
    break;
  }
}

// ========================================================================
//                        OTA 固件升级 (ESP32 侧)
// ========================================================================

/** HTTP GET 请求文本内容 */
static String httpGet(const char *path) {
  HTTPClient http;
  String url = String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + path;
  http.begin(url);
  http.setTimeout(30000);
  int code = http.GET();
  String body = "";
  if (code == HTTP_CODE_OK) body = http.getString();
  http.end();
  return body;
}

/** HTTP 下载二进制文件到内存 */
static bool httpDownload(const char *path, uint8_t **outData, size_t *outSize) {
  HTTPClient http;
  String url = String("http://") + OTA_SERVER_HOST + ":" + OTA_SERVER_PORT + path;
  http.begin(url);
  http.setTimeout(60000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  *outSize = http.getSize();
  *outData = (uint8_t*)malloc(*outSize);
  if (!*outData) { http.end(); return false; }

  WiFiClient *stream = http.getStreamPtr();
  size_t pos = 0;
  while (pos < *outSize && stream->connected()) {
    size_t avail = stream->available();
    if (avail > 0) {
      size_t n = min(avail, *outSize - pos);
      stream->read(*outData + pos, n);
      pos += n;
    }
    delay(1);
  }
  http.end();
  return pos == *outSize;
}

/** OTA握手帧: [AA][55][F0][file_size:4][ver:4][sig:32][crc] */
static void otaSendHandshakeFrame(uint32_t fileSize, uint32_t version,
                                   const uint8_t *sig, size_t sigLen) {
  uint8_t buf[44];
  buf[0] = FRAME_HDR0;
  buf[1] = FRAME_HDR1;
  buf[2] = FRAME_OTA_HANDSHAKE;
  buf[3] = (fileSize >> 24) & 0xFF;
  buf[4] = (fileSize >> 16) & 0xFF;
  buf[5] = (fileSize >> 8)  & 0xFF;
  buf[6] = (fileSize)       & 0xFF;
  buf[7] = (version >> 24) & 0xFF;
  buf[8] = (version >> 16) & 0xFF;
  buf[9] = (version >> 8)  & 0xFF;
  buf[10]= (version)       & 0xFF;
  size_t n = min(sigLen, (size_t)32);
  memcpy(buf + 11, sig, n);
  if (n < 32) memset(buf + 11 + n, 0, 32 - n);
  buf[43] = crc8sum(buf + 2, 41);

  /* 填充到522B确保STM32轮询捕获 */
  uint8_t padded[522];
  memcpy(padded, buf, 44);
  memset(padded + 44, 0xFF, 522 - 45);
  padded[521] = buf[43];

  uint8_t misoRx[522] = {0};
  spiTransfer(padded, misoRx, 522);
  Serial.printf("[OTA] Handshake sent: size=%lu ver=%lu\n", fileSize, version);
}

/** OTA数据块: [AA][55][F1][offset:4][len:2][data:N][crc] */
static bool otaSendDataChunk(uint32_t offset, const uint8_t *data, uint16_t len) {
  if (len > 512) len = 512;
  if (len == 0) return false;

  uint8_t buf[10 + 512 + 1];  // header + max data + crc
  buf[0] = FRAME_HDR0;
  buf[1] = FRAME_HDR1;
  buf[2] = FRAME_OTA_DATA;
  buf[3] = (offset >> 24) & 0xFF;
  buf[4] = (offset >> 16) & 0xFF;
  buf[5] = (offset >> 8)  & 0xFF;
  buf[6] = (offset)       & 0xFF;
  buf[7] = (len >> 8) & 0xFF;
  buf[8] = (len)      & 0xFF;
  memcpy(buf + 9, data, len);
  buf[9 + len] = crc8sum(buf + 2, 7 + len);

  uint8_t misoRx[522] = {0};
  spiTransfer(buf, misoRx, 10 + len);
  return true;
}

/** OTA结果帧: [AA][55][F2][status][reserved][crc] (重复发3次确保收到) */
static void otaSendResult(uint8_t status) {
  uint8_t frame[6];
  frame[0] = FRAME_HDR0;
  frame[1] = FRAME_HDR1;
  frame[2] = FRAME_OTA_RESULT;
  frame[3] = status;
  frame[4] = 0x00;
  frame[5] = crc8sum(frame + 2, 3);

  uint8_t padded[14];
  memcpy(padded, frame, 6);
  memset(padded + 6, 0xFF, 8);

  for (int i = 0; i < 3; i++) {
    uint8_t misoRx[14] = {0};
    spiTransfer(padded, misoRx, 14);
    delay(10);
  }
  Serial.printf("[OTA] Result sent: status=%02X\n", status);
}

/** OTA 状态机主循环 */
static void otaRunStateMachine() {
  switch (otaState) {
  case OTA_IDLE:
    break;

  case OTA_CHECKING_VER: {
    /* 查询服务器版本 */
    String verStr = httpGet("/version.txt");
    verStr.trim();
    serverFwVer = strtoul(verStr.c_str(), NULL, 10);

    Serial.printf("[OTA] Server=%lu  STM32=%lu\n", serverFwVer, stm32FwVer);

    if (serverFwVer > 0 && serverFwVer > stm32FwVer) {
      Serial.println("[OTA] New firmware available, downloading...");

      /* 下载签名 */
      String sigHex = httpGet("/firmware.bin.sig");
      sigHex.trim();
      uint8_t sig[32] = {0};
      size_t sigLen = sigHex.length() / 2;
      if (sigLen > 32) sigLen = 32;
      for (size_t i = 0; i < sigLen; i++) {
        char hex[3] = {(char)sigHex[i*2], (char)sigHex[i*2+1], 0};
        sig[i] = (uint8_t)strtol(hex, NULL, 16);
      }

      /* 下载固件 */
      fwData = NULL; fwSize = 0;
      if (!httpDownload("/firmware.bin", &fwData, &fwSize)) {
        Serial.println("[OTA] Download failed");
        otaState = OTA_IDLE;
        break;
      }

      Serial.printf("[OTA] Downloaded %u bytes\n", fwSize);

      /* 发送OTA握手帧给STM32 */
      hsState = ESP_HS_OTA_IN_PROGRESS;
      otaSendHandshakeFrame(fwSize, serverFwVer, sig, sigLen);
      otaState = OTA_SENDING;
      fwSent   = 0;
    } else {
      Serial.println("[OTA] No update needed");
      otaState  = OTA_IDLE;
      lastOtaCheck = millis();
      otaSendResult(0x00);  /* 通知STM32: 无需更新 */
    }
    break;
  }

  case OTA_SENDING: {
    if (fwSent < fwSize) {
      uint32_t remaining = fwSize - fwSent;
      uint16_t chunk = (remaining > 512) ? 512 : (uint16_t)remaining;

      otaSendDataChunk(fwSent, fwData + fwSent, chunk);
      fwSent += chunk;

      if (fwSent % (512 * 10) == 0 || fwSent >= fwSize) {
        Serial.printf("[OTA] Progress: %u/%u (%d%%)\n",
                      fwSent, fwSize, (int)(fwSent * 100 / fwSize));
      }

      /* 等待 STM32 ACK (MISO 输出 0xA5 表示块写入完成) */
      {
        uint32_t ackStart = millis();
        bool ackOk = false;
        while (millis() - ackStart < 5000) {  /* 5s 超时 */
          uint8_t miso[8], dummy[8] = {0};
          spiTransfer(dummy, miso, 8);
          for (int i = 0; i < 8; i++) {
            if (miso[i] == STM32_CMD_OTA_CHUNK_ACK) { ackOk = true; break; }
          }
          if (ackOk) break;
          delay(50);
        }
        if (!ackOk) {
          Serial.println("[OTA] ACK timeout, aborting download");
          otaState = OTA_IDLE;
          break;
        }
      }
    }

    if (fwSent >= fwSize) {
      delay(500);
      otaSendResult(0x00);

      free(fwData); fwData = NULL; fwSize = 0; fwSent = 0;
      otaState = OTA_IDLE;
      hsState  = ESP_HS_INIT;  /* STM32即将重启 */
      hsRetryCount = 0;
      lastOtaCheck = millis();
      Serial.println("[OTA] Complete!");
    }
    break;
  }
  }
}

void connectMQTT() {
  // 设置根证书（用于 TLS 连接）
  //wifiClient.setCACert(ROOT_CA_CERT);
  // 禁用证书验证（仅用于测试环境）
  wifiClient.setInsecure();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);
  mqttClient.setKeepAlive(60); // 保持连接，更利于观察掉线原因

  Serial.println("[MQTT] 正在连接到华为云 IoTDA ...");
  Serial.printf("[MQTT] host=%s  port=%d  clientId=%s\n", MQTT_HOST, MQTT_PORT, MQTT_CLIENT_ID);
  Serial.printf("[MQTT] username=%s\n", MQTT_USERNAME);

  // 使用 MQTTS 连接（已通过 WiFiClientSecure 配置了 TLS）
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    Serial.println("[MQTT] 连接成功！");

    // 订阅用户指定主题 + 华为云命令主题（便于诊断）
    mqttUserTopic = "/device/common/data";
    String cmdTopic  = String("$oc/devices/") + MQTT_USERNAME + "/sys/commands/#";

    mqttClient.subscribe(mqttUserTopic.c_str());
    mqttClient.subscribe(cmdTopic.c_str());

    Serial.println("[MQTT] 已订阅主题");
    Serial.printf("[MQTT] 用户: %s\n", mqttUserTopic.c_str());
    Serial.printf("[MQTT] 命令: %s\n", cmdTopic.c_str());

    // 上报一条设备上线消息（属性上报）
    String reportTopic = String("$oc/devices/") + MQTT_USERNAME + "/sys/properties/report";
    String reportPayload = "{\"services\":[{\"service_id\":\"device_info\",\"properties\":{\"status\":\"online\"},\"event_time\":null}]}";
    mqttClient.publish(reportTopic.c_str(), reportPayload.c_str());
  } else {
    Serial.print("[MQTT] 连接失败，state=");
    Serial.print(mqttClient.state());
    Serial.print("，含义: ");
    Serial.println(getMqttStateString(mqttClient.state()));
    Serial.println("[MQTT] 5 秒后重试...");
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("========================================");
  Serial.println("  ESP32 + 华为云 IoTDA MQTT 示例");
  Serial.println("========================================");

  // 第一步：连接 WiFi
  connectWiFi();
  Serial.printf("[WiFi] IP=%s  RSSI=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // 第二步：测试到华为云 MQTT 端口的网络/TLS连通性
  testTlsConnect();

  // 第三步：连接华为云 MQTT
  connectMQTT();

  // 第三步：初始化 SPI (ESP32 Master)
  initSPI();

  // 第四步：启动工业级握手协议
  Serial.println("[HS] Starting industrial handshake protocol...");
  hsState      = ESP_HS_INIT;
  hsRetryCount = 0;
}

void loop() {
  static unsigned long startMs = millis();

  // ========== 工业级握手协议 ==========
  esp32RunHandshake();

  // ========== OTA 状态机 ==========
  otaRunStateMachine();

  // 检查STM32的MISO命令 (如OTA触发 0x10) — 仅握手完成后
  if (hsState == ESP_HS_READY) {
    uint8_t dummy = 0x00, miso = 0x00;
    spiTransfer(&dummy, &miso, 1);
    if (miso == STM32_CMD_OTA_START && otaState == OTA_IDLE) {
      Serial.println("[CMD] STM32 requested OTA check");
      otaState = OTA_CHECKING_VER;
    }
  }

  // 定期OTA版本检查 (仅握手完成后)
  if (hsState == ESP_HS_READY && otaState == OTA_IDLE &&
      millis() - lastOtaCheck >= OTA_CHECK_INTERVAL_MS) {
    Serial.println("[OTA] Periodic version check...");
    otaState = OTA_CHECKING_VER;
  }

  // ========== 测试帧已注释 (避免干扰 OTA) ==========

  // 保持 MQTT 连接
  if (!mqttClient.connected()) {
    Serial.println("[MQTT] 连接已断开，正在重连...");
    Serial.printf("[MQTT] 当前 state=%d  含义=%s\n", mqttClient.state(), getMqttStateString(mqttClient.state()));
    connectMQTT();
    delay(3000);
  }
  mqttClient.loop();

  delay(10);
}
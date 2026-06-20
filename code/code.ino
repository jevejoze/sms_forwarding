#include "globals.h"
#include "wifi_config.h"
#include "config.h"
#include "web_handlers.h"
#include "web_handlers.h"
#include "modem.h"
#include "web_handlers.h"
#include "push.h"
#include "web_handlers.h"
#include "sms_process.h"
#include "web_handlers.h"
#include "esim.h"

static bool httpStarted = false;

static void startHttpServer() {
  if (httpStarted) return;

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/tools", handleRoot);
  server.on("/sms", handleRoot);
  server.on("/sendsms", HTTP_POST, handleSendSms);
  server.on("/ping", HTTP_POST, handlePing);
  server.on("/query", handleQuery);
  server.on("/flight", handleFlightMode);
  server.on("/at", handleATCommand);
  server.on("/log", handleLog);
  server.on("/modem", handleModem);
  server.on("/wifi", handleWifi);
  server.on("/esim", handleESim);
  server.begin();

  httpStarted = true;
  logCaptureLn(String("HTTP服务器已启动，等待 WiFi 获取 IP"));
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  // 缩短初始化延时，WiFi连接会处理自己的超时
  delay(200);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
  Serial1.setRxBufferSize(SERIAL_BUFFER_SIZE);
  while (Serial1.available()) Serial1.read();
  initConcatBuffer();
  loadConfig();
  configValid = isConfigValid();
  startHttpServer();

  // ---- WiFi 连接优化 ----
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                    // 关闭 Modem Sleep，提高连接响应速度
  WiFi.setAutoReconnect(true);             // 断线后自动重连
  // 使用快速扫描而非全信道扫描（全信道扫描在空信道上等待超时极慢）
  // 首次连接成功后 ESP32 会自动记住信道，下次启动更快
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  logCaptureLn(String("连接wifi: ") + String(WIFI_SSID));

  // 带超时的等待连接，失败则重启重试
  unsigned long wifiStart = millis();
  unsigned long lastBlink = 0;
  const unsigned long WIFI_TIMEOUT = 20000; // 20秒超时
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT) {
    if (millis() - lastBlink >= 250) {
      lastBlink = millis();
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    }
    server.handleClient();
    delay(25);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logCaptureLn(String("wifi已连接"));
    logCapture(String("IP地址: "));
    logCaptureLn(WiFi.localIP().toString());
    logCapture(String("信号强度(RSSI): "));
    logCaptureLn(String(WiFi.RSSI()) + " dBm");
  } else {
    logCaptureLn(String("⚠️ WiFi连接超时，即将重启重试..."));
    delay(1000);
    ESP.restart();
  }

  // ---- NTP 时间同步 ----
  logCaptureLn(String("正在同步NTP时间..."));
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  int ntpRetry = 0;
  while (time(nullptr) < 100000 && ntpRetry < 100) {
    delay(1);
    server.handleClient();
    ntpRetry++;
  }
  if (time(nullptr) >= 100000) {
    timeSynced = true;
    logCaptureLn(String("NTP时间同步成功"));
    time_t now = time(nullptr);
    logCapture(String("当前UTC时间戳: "));
    logCaptureLn(String(now));
  } else {
    logCaptureLn(String("NTP时间同步失败，将使用设备时间"));
  }

  ssl_client.setInsecure();
  digitalWrite(LED_BUILTIN, LOW);

  // ---- 启动通知（网页已可用，发邮件不会影响用户访问） ----
  if (configValid) {
    logCaptureLn(String("配置有效，发送启动通知..."));
    String subject = "短信转发器已启动";
    String body = "设备已启动\n设备地址: " + getDeviceUrl();
    sendEmailNotification(subject.c_str(), body.c_str());
  }

  // ---- 模组上电（放到 Web 可访问之后，避免拖慢后台启动）----
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();

  // ---- eSIM初始化 ----
  logCaptureLn(String("初始化eSIM..."));
  if (esimInit()) {
    logCaptureLn(String("eSIM初始化成功"));
    char eid[40];
    if (esimGetEID(eid, sizeof(eid))) {
      logCapture(String("EID: "));
      logCaptureLn(eid);
    }
  } else {
    logCaptureLn(String("eSIM初始化失败或未检测到eUICC芯片"));
  }

  // ---- 模组初始化（较慢，但网页已可访问） ----
  modemInit();
}

void loop() {
  server.handleClient();
  if (!configValid) {
    if (millis() - lastPrintTime >= 1000) {
      lastPrintTime = millis();
      logCaptureLn(String("⚠️ 请访问 " + getDeviceUrl() + " 配置系统参数"));
    }
  }
  checkConcatTimeout();
  handleSerialConsole();
  checkSerial1URC();
}

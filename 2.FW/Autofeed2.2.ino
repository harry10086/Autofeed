/*
 * Autofeed 2.2 - ESP32 自动喂食水泵控制系统
 *
 * 硬件: ESP32-WROOM-32D
 * 通信: HiveMQ MQTT (TLS)
 * 配网: ESPTouch SmartConfig
 *
 */

#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

// ==================== HiveMQ 配置 ====================
// HiveMQ Cloud Broker 地址
#define MQTT_BROKER "xxx.hivemq.cloud"
#define MQTT_PORT 8883               // TLS 端口
#define MQTT_USER "name"            // HiveMQ 用户名
#define MQTT_PASS "password"  // HiveMQ 密码
#define MQTT_CLIENT "autofeed-esp32" // 客户端 ID 自定

// ==================== MQTT 主题 ====================
#define TOPIC_CMD "autofeed/cmd"
#define TOPIC_STATUS "autofeed/status"

// ==================== 引脚定义 ====================
#define PWM1_PIN 14       // 电机 PWM1 — HIGH 时正转方向
#define PWM2_PIN 27       // 电机 PWM2 — HIGH 时反转方向
#define PUMP_IN_PIN 19    // 进水泵 — HIGH 工作
#define PUMP_OUT_PIN 18   // 循环泵 — HIGH 工作
#define WATER_LOW_PIN 17  // 低水位传感器 — HIGH 表示水位低
#define WATER_HIGH_PIN 16 // 高水位传感器 — HIGH 表示水位已满

// ==================== NTP 配置 ====================
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET 28800 // UTC+8 秒数
#define DST_OFFSET 0

// ==================== 全局对象 ====================
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);
Preferences prefs;

// ==================== 电机配置 ====================
struct MotorConfig {
  bool enabled;
  int runSeconds;   // 每次运行秒数
  int intervalDays; // 间隔天数 (1=每天, 2=隔天, 3=隔两天...)
  String times[10]; // 每天的执行时间列表 "HH:MM"
  int timeCount;    // 时间点数量
} motorCfg = {false, 30, 1, {}, 0};

// ==================== 进水泵配置 ====================
struct PumpInConfig {
  bool enabled;
} pumpInCfg = {false};

// ==================== 循环泵配置 ====================
struct PumpOutConfig {
  bool enabled;
  int onSeconds;  // 循环开启秒数
  int offSeconds; // 循环关闭秒数
} pumpOutCfg = {false, 30, 30};

// ==================== 运行状态 ====================
bool motorRunning = false;
unsigned long motorStartTime = 0;

bool pumpInRunning = false;
unsigned long pumpInStartTime = 0;
bool pumpOutRunning = false;
unsigned long pumpOutCycleStart = 0;
bool pumpOutCycleState = false;

bool waterLow = false;
bool waterHigh = false;

// ==================== 定时器 ====================
unsigned long lastStatusReport = 0;
const unsigned long STATUS_INTERVAL = 10000; // 10 秒上报一次

unsigned long lastScheduleCheck = 0;
const unsigned long SCHEDULE_INTERVAL = 30000; // 30 秒检查一次调度

unsigned long lastMqttReconnect = 0;
const unsigned long MQTT_RECONNECT_INTERVAL = 5000;

// 调度追踪: 记录今天是否已执行过各时间点
bool scheduledToday[10] = {false};
int lastCheckedDay = -1;

// ==================== 函数前向声明 ====================
void setupWiFi();
void setupMQTT();
void mqttCallback(char *topic, byte *payload, unsigned int length);
void reconnectMQTT();
void handleMotorSchedule();
void updateMotor();
void updatePumpIn();
void updatePumpOut();
void reportStatus();
void applyCommand(const char *json);
void saveConfig();
void loadConfig();
void stopMotor();
void startMotor();

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Autofeed 2.2 启动 ===");

  // 引脚初始化
  pinMode(PWM1_PIN, OUTPUT);
  pinMode(PWM2_PIN, OUTPUT);
  pinMode(PUMP_IN_PIN, OUTPUT);
  pinMode(PUMP_OUT_PIN, OUTPUT);
  pinMode(WATER_LOW_PIN, INPUT);
  pinMode(WATER_HIGH_PIN, INPUT);

  // 初始状态: 全部关闭
  digitalWrite(PWM1_PIN, LOW);
  digitalWrite(PWM2_PIN, LOW);
  digitalWrite(PUMP_IN_PIN, LOW);
  digitalWrite(PUMP_OUT_PIN, LOW);

  // 加载已保存的配置
  loadConfig();

  // WiFi 连接
  setupWiFi();

  // NTP 时间同步
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  Serial.println("等待 NTP 时间同步...");
  struct tm timeinfo;
  int retries = 0;
  while (!getLocalTime(&timeinfo) && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }
  if (retries < 20) {
    Serial.println("\nNTP 同步成功!");
    Serial.printf("当前时间: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                  timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min,
                  timeinfo.tm_sec);
  } else {
    Serial.println("\nNTP 同步失败, 将持续重试");
  }

  // MQTT 连接
  setupMQTT();
}

// ==================== Loop ====================
void loop() {
  // 保持 WiFi 连接
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi 断开, 重新连接...");
    WiFi.reconnect();
    delay(5000);
    return;
  }

  // 保持 MQTT 连接
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnect > MQTT_RECONNECT_INTERVAL) {
      lastMqttReconnect = now;
      reconnectMQTT();
    }
  }
  mqtt.loop();

  // 读取水位传感器
  waterLow = digitalRead(WATER_LOW_PIN) == HIGH;
  waterHigh = digitalRead(WATER_HIGH_PIN) == HIGH;

  // 更新各模块
  handleMotorSchedule();
  updateMotor();
  updatePumpIn();
  updatePumpOut();

  // 定时上报状态
  unsigned long now = millis();
  if (now - lastStatusReport >= STATUS_INTERVAL) {
    lastStatusReport = now;
    reportStatus();
  }
}

// ==================== WiFi SmartConfig ====================
void setupWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.length() > 0) {
    Serial.printf("尝试连接已保存的 WiFi: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nWiFi 已连接! IP: %s\n",
                    WiFi.localIP().toString().c_str());
      return;
    }
    Serial.println("\n已保存的 WiFi 连接失败, 启动 SmartConfig...");
  }

  // SmartConfig 配网
  WiFi.mode(WIFI_STA);
  WiFi.beginSmartConfig(SC_TYPE_ESPTOUCH_V2);
  Serial.println("等待 SmartConfig 配网... (请使用 ESPTouch App)");

  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nSmartConfig 配网完成!");

  // 等待连接
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi 已连接! IP: %s\n", WiFi.localIP().toString().c_str());

  // 保存 WiFi 凭据
  prefs.begin("wifi", false);
  prefs.putString("ssid", WiFi.SSID());
  prefs.putString("pass", WiFi.psk());
  prefs.end();
  Serial.println("WiFi 凭据已保存");
}

// ==================== MQTT 设置 ====================
void setupMQTT() {
  espClient.setInsecure(); // 跳过证书验证 (HiveMQ Cloud)
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
  reconnectMQTT();
}

void reconnectMQTT() {
  if (mqtt.connected())
    return;

  Serial.print("连接 MQTT...");
  if (mqtt.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
    Serial.println("成功!");
    mqtt.subscribe(TOPIC_CMD);
    Serial.printf("已订阅: %s\n", TOPIC_CMD);
    // 连接后立即上报一次状态
    reportStatus();
  } else {
    Serial.printf("失败, rc=%d\n", mqtt.state());
  }
}

// ==================== MQTT 回调 ====================
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char json[1024];
  if (length >= sizeof(json))
    length = sizeof(json) - 1;
  memcpy(json, payload, length);
  json[length] = '\0';

  Serial.printf("收到 [%s]: %s\n", topic, json);

  if (String(topic) == TOPIC_CMD) {
    applyCommand(json);
  }
}

// ==================== 解析并应用命令 ====================
void applyCommand(const char *json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("JSON 解析失败: %s\n", err.c_str());
    return;
  }

  // 电机配置
  if (doc.containsKey("motor")) {
    JsonObject motor = doc["motor"];
    if (motor.containsKey("enabled")) {
      motorCfg.enabled = motor["enabled"].as<bool>();
      if (!motorCfg.enabled) {
        stopMotor();
      }
    }
    if (motor.containsKey("runSeconds")) {
      motorCfg.runSeconds = motor["runSeconds"].as<int>();
    }
    if (motor.containsKey("schedule")) {
      JsonObject schedule = motor["schedule"];
      if (schedule.containsKey("intervalDays")) {
        motorCfg.intervalDays = schedule["intervalDays"].as<int>();
        if (motorCfg.intervalDays < 1)
          motorCfg.intervalDays = 1;
      }
      if (schedule.containsKey("times")) {
        JsonArray times = schedule["times"];
        motorCfg.timeCount = 0;
        for (int i = 0; i < times.size() && i < 10; i++) {
          motorCfg.times[i] = times[i].as<String>();
          motorCfg.timeCount++;
        }
        // 重置今日调度状态，确保新修改/删除后的定时能正确重新匹配
        for (int i = 0; i < 10; i++) {
          scheduledToday[i] = false;
        }
      }
    }
  }

  // 进水泵配置
  if (doc.containsKey("pumpIn")) {
    JsonObject pumpIn = doc["pumpIn"];
    if (pumpIn.containsKey("enabled")) {
      pumpInCfg.enabled = pumpIn["enabled"].as<bool>();
      if (!pumpInCfg.enabled) {
        digitalWrite(PUMP_IN_PIN, LOW);
        pumpInRunning = false;
      }
    }
  }

  // 循环泵配置
  if (doc.containsKey("pumpOut")) {
    JsonObject pumpOut = doc["pumpOut"];
    if (pumpOut.containsKey("enabled")) {
      pumpOutCfg.enabled = pumpOut["enabled"].as<bool>();
      if (!pumpOutCfg.enabled) {
        digitalWrite(PUMP_OUT_PIN, LOW);
        pumpOutRunning = false;
      } else {
        // 启动循环
        pumpOutCycleStart = millis();
        pumpOutCycleState = true;
        digitalWrite(PUMP_OUT_PIN, HIGH);
        pumpOutRunning = true;
      }
    }
    if (pumpOut.containsKey("onSeconds")) {
      pumpOutCfg.onSeconds = pumpOut["onSeconds"].as<int>();
    }
    if (pumpOut.containsKey("offSeconds")) {
      pumpOutCfg.offSeconds = pumpOut["offSeconds"].as<int>();
    }
  }

  // 保存配置到 Flash
  saveConfig();
  Serial.println("配置已更新并保存");

  // 更新后立即上报状态
  reportStatus();
}

// ==================== 电机调度 ====================
void handleMotorSchedule() {
  if (!motorCfg.enabled || motorCfg.timeCount == 0)
    return;
  if (motorRunning)
    return;

  unsigned long now = millis();
  if (now - lastScheduleCheck < SCHEDULE_INTERVAL)
    return;
  lastScheduleCheck = now;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return;

  int today = timeinfo.tm_yday;

  // 新的一天: 重置调度状态
  if (today != lastCheckedDay) {
    lastCheckedDay = today;
    for (int i = 0; i < 10; i++) {
      scheduledToday[i] = false;
    }
  }

  // 检查间隔天数 (基于一年中的第几天)
  // intervalDays=1 每天, =2 隔天 (偶数天执行), =3 每隔两天...
  if (today % motorCfg.intervalDays != 0)
    return;

  // 检查是否到达预设时间
  char currentTime[6];
  snprintf(currentTime, sizeof(currentTime), "%02d:%02d", timeinfo.tm_hour,
           timeinfo.tm_min);

  for (int i = 0; i < motorCfg.timeCount; i++) {
    if (scheduledToday[i])
      continue; // 今天已执行过

    if (motorCfg.times[i] == String(currentTime)) {
      Serial.printf("调度触发: %s\n", currentTime);
      scheduledToday[i] = true;
      startMotor();
      break;
    }
  }
}

// ==================== 电机控制 ====================
void startMotor() {
  if (motorRunning)
    return;
  Serial.println("电机启动 (正转)");
  digitalWrite(PWM1_PIN, HIGH);
  digitalWrite(PWM2_PIN, LOW);
  motorRunning = true;
  motorStartTime = millis();
}

void stopMotor() {
  Serial.println("电机停止");
  digitalWrite(PWM1_PIN, LOW);
  digitalWrite(PWM2_PIN, LOW);
  motorRunning = false;
}

void updateMotor() {
  if (!motorRunning)
    return;

  unsigned long elapsed = millis() - motorStartTime;
  if (elapsed >= (unsigned long)motorCfg.runSeconds * 1000UL) {
    stopMotor();
    reportStatus();
  }
}

// ==================== 进水泵控制 ====================
void updatePumpIn() {
  if (!pumpInCfg.enabled) {
    if (pumpInRunning) {
      digitalWrite(PUMP_IN_PIN, LOW);
      pumpInRunning = false;
    }
    return;
  }

  // 水位逻辑:
  // waterLow=true (GPIO17 HIGH) → 水位低 → 启动进水泵
  // waterHigh=true (GPIO16 HIGH) → 水位已满 → 停止进水泵
  if (waterHigh) {
    // 水位到达最高, 停止进水
    if (pumpInRunning) {
      Serial.println("水位已满, 进水泵停止");
      digitalWrite(PUMP_IN_PIN, LOW);
      pumpInRunning = false;
    }
  } else if (waterLow) {
    // 水位低, 启动进水
    if (!pumpInRunning) {
      Serial.println("水位低, 进水泵启动");
      digitalWrite(PUMP_IN_PIN, HIGH);
      pumpInRunning = true;
      pumpInStartTime = millis(); // 记录启动时间
    }
  }

  // 进水泵运行超过 3 分钟开启保护
  if (pumpInRunning) {
    if (millis() - pumpInStartTime > 180000UL) { // 180,000ms = 3分钟
      Serial.println(
          "!!! 警告: 进水超时(3分钟), 自动关闭并禁用[所有]水泵系统 !!!");

      // 关闭进水泵
      digitalWrite(PUMP_IN_PIN, LOW);
      pumpInRunning = false;
      pumpInCfg.enabled = false;

      // 同步关闭并禁用循环泵
      digitalWrite(PUMP_OUT_PIN, LOW);
      pumpOutRunning = false;
      pumpOutCfg.enabled = false;

      saveConfig();   // 持久化这两个状态
      reportStatus(); // 立即上报异常状态
    }
  }
}

// ==================== 循环泵控制 ====================
void updatePumpOut() {
  if (!pumpOutCfg.enabled) {
    if (pumpOutRunning) {
      digitalWrite(PUMP_OUT_PIN, LOW);
      pumpOutRunning = false;
    }
    return;
  }

  unsigned long now = millis();
  unsigned long elapsed = now - pumpOutCycleStart;

  if (pumpOutCycleState) {
    // 开启阶段
    if (elapsed >= (unsigned long)pumpOutCfg.onSeconds * 1000UL) {
      // 切换到关闭阶段
      pumpOutCycleState = false;
      pumpOutCycleStart = now;
      digitalWrite(PUMP_OUT_PIN, LOW);
      pumpOutRunning = false;
      Serial.println("循环泵: 关闭");
    }
  } else {
    // 关闭阶段
    if (elapsed >= (unsigned long)pumpOutCfg.offSeconds * 1000UL) {
      // 切换到开启阶段
      pumpOutCycleState = true;
      pumpOutCycleStart = now;
      digitalWrite(PUMP_OUT_PIN, HIGH);
      pumpOutRunning = true;
      Serial.println("循环泵: 开启");
    }
  }
}

// ==================== 上报状态 ====================
void reportStatus() {
  if (!mqtt.connected())
    return;

  struct tm timeinfo;
  char timeStr[30] = "N/A";
  if (getLocalTime(&timeinfo)) {
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S+08:00", &timeinfo);
  }

  JsonDocument doc;

  JsonObject motor = doc["motor"].to<JsonObject>();
  motor["enabled"] = motorCfg.enabled;
  motor["running"] = motorRunning;
  motor["runSeconds"] = motorCfg.runSeconds;
  JsonObject schedule = motor["schedule"].to<JsonObject>();
  schedule["intervalDays"] = motorCfg.intervalDays;
  JsonArray times = schedule["times"].to<JsonArray>();
  for (int i = 0; i < motorCfg.timeCount; i++) {
    times.add(motorCfg.times[i]);
  }

  JsonObject pumpIn = doc["pumpIn"].to<JsonObject>();
  pumpIn["enabled"] = pumpInCfg.enabled;
  pumpIn["running"] = pumpInRunning;

  JsonObject pumpOut = doc["pumpOut"].to<JsonObject>();
  pumpOut["enabled"] = pumpOutCfg.enabled;
  pumpOut["running"] = pumpOutRunning;
  pumpOut["onSeconds"] = pumpOutCfg.onSeconds;
  pumpOut["offSeconds"] = pumpOutCfg.offSeconds;

  doc["waterLow"] = waterLow;
  doc["waterHigh"] = waterHigh;
  doc["time"] = timeStr;
  doc["wifi"] = (WiFi.status() == WL_CONNECTED);

  char buf[512];
  serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_STATUS, buf);
}

// ==================== 配置持久化 ====================
void saveConfig() {
  prefs.begin("config", false);

  prefs.putBool("mEnabled", motorCfg.enabled);
  prefs.putInt("mRunSec", motorCfg.runSeconds);
  prefs.putInt("mIntDays", motorCfg.intervalDays);
  prefs.putInt("mTimeCnt", motorCfg.timeCount);
  for (int i = 0; i < motorCfg.timeCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "mT%d", i);
    prefs.putString(key, motorCfg.times[i]);
  }

  prefs.putBool("piEnabled", pumpInCfg.enabled);

  prefs.putBool("poEnabled", pumpOutCfg.enabled);
  prefs.putInt("poOnSec", pumpOutCfg.onSeconds);
  prefs.putInt("poOffSec", pumpOutCfg.offSeconds);

  prefs.end();
}

void loadConfig() {
  prefs.begin("config", true);

  motorCfg.enabled = prefs.getBool("mEnabled", false);
  motorCfg.runSeconds = prefs.getInt("mRunSec", 30);
  motorCfg.intervalDays = prefs.getInt("mIntDays", 1);
  motorCfg.timeCount = prefs.getInt("mTimeCnt", 0);
  for (int i = 0; i < motorCfg.timeCount && i < 10; i++) {
    char key[8];
    snprintf(key, sizeof(key), "mT%d", i);
    motorCfg.times[i] = prefs.getString(key, "");
  }

  pumpInCfg.enabled = prefs.getBool("piEnabled", false);

  pumpOutCfg.enabled = prefs.getBool("poEnabled", false);
  pumpOutCfg.onSeconds = prefs.getInt("poOnSec", 30);
  pumpOutCfg.offSeconds = prefs.getInt("poOffSec", 30);

  prefs.end();
  Serial.println("配置已从 Flash 加载");
}

/*
 * IoT датчик температуры/влажности на ESP32 + DHT22
 * Отправляет данные POST-запросом на Flask-сервер.
 *
 * Требуемые библиотеки (Менеджер библиотек Arduino IDE):
 *   - DHT sensor library (Adafruit)
 *   - Adafruit Unified Sensor
 *   - ArduinoJson (6.x)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ================== НАСТРОЙКИ ==================

// Wi-Fi
const char* WIFI_SSID     = "ВАШ_WIFI";
const char* WIFI_PASSWORD = "ВАШ_ПАРОЛЬ";

// Сервер: IP компьютера с Flask в локальной сети + порт 5000
// Узнать IP: Linux/macOS — `ip a` или `ifconfig`, Windows — `ipconfig`
const char* SERVER_URL = "http://192.168.1.100:5000/api/data";

// Идентификатор устройства (уникальный для каждого ESP32)
const char* DEVICE_ID = "esp32-01";

// Пин датчика и его тип
#define DHTPIN  4
#define DHTTYPE DHT22   // для DHT11 замените на DHT11

// Интервал отправки данных (мс)
const unsigned long SEND_INTERVAL = 10000;   // 10 секунд

// Интервал переподключения к Wi-Fi (мс)
const unsigned long WIFI_RETRY_INTERVAL = 5000;

// ================================================

DHT dht(DHTPIN, DHTTYPE);

unsigned long lastSendTime = 0;
unsigned long lastWifiCheck = 0;

void connectWiFi() {
  Serial.print("Подключение к Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Подключено. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("Не удалось подключиться. Повтор позже.");
  }
}

// Отправка JSON на сервер. Возвращает true при HTTP 2xx.
bool sendToServer(const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Нет Wi-Fi, отправка пропущена");
    return false;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);

  int code = http.POST(payload);
  String response = http.getString();
  http.end();

  Serial.print("HTTP ");
  Serial.print(code);
  Serial.print(" -> ");
  Serial.println(response);

  return code >= 200 && code < 300;
}

void readAndSend() {
  // Чтение датчика. DHT22 требует паузу ~2 сек между чтениями,
  // поэтому опрашиваем не чаще раза в 2 секунды (у нас — раз в 10 сек).
  float temperature = dht.readTemperature();   // °C
  float humidity    = dht.readHumidity();      // %

  StaticJsonDocument<200> doc;
  doc["device_id"] = DEVICE_ID;

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Ошибка чтения DHT22");
    doc["error"] = "sensor_timeout";
  } else {
    Serial.printf("T=%.1f°C  H=%.1f%%\n", temperature, humidity);
    doc["temperature"] = round(temperature * 10) / 10.0;
    doc["humidity"]    = round(humidity * 10) / 10.0;
  }

  String payload;
  serializeJson(doc, payload);
  Serial.print("Отправка: ");
  Serial.println(payload);

  sendToServer(payload);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== IoT Sensor starting ===");

  dht.begin();
  connectWiFi();

  // Первый замер сразу после старта
  lastSendTime = millis() - SEND_INTERVAL;
}

void loop() {
  unsigned long now = millis();

  // Проверка Wi-Fi и переподключение
  if (now - lastWifiCheck > WIFI_RETRY_INTERVAL) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi потерян, переподключение...");
      connectWiFi();
    }
  }

  // Отправка показаний по расписанию
  if (now - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = now;
    readAndSend();
  }

  delay(100);
}
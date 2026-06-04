#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <GxEPD2_BW.h>
#include <Adafruit_SHTC3.h>
#include <esp_sleep.h>
#include <time.h>
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ----------- EPD pins (ESP32-S3) -----------
#define EPD_DC      10
#define EPD_CS      11
#define EPD_SCK     12
#define EPD_MOSI    13
#define EPD_RST      9
#define EPD_BUSY     8
#define EPD_PWR      6    // ACTIVE-LOW (ON = LOW)
#define VBAT_PWR    17    // rail enable (ON = HIGH)

// ----------- I2C pins -----------
#define I2C_SDA     47
#define I2C_SCL     48

// ----------- Settings -----------
#define SPI_CLOCK_HZ        4000000
#define SLEEP_SECONDS       60
#define WIFI_TIMEOUT_MS     15000
#define FULL_REFRESH_EVERY  5
#define NTP_TIMEOUT_MS      10000
#define NTP_TIMEZONE        "CET-1CEST,M3.5.0,M10.5.0/3"

// MQTT topics
#define MQTT_STATE_TOPIC      "notifier/sensor"
#define MQTT_DISCOVERY_TEMP   "homeassistant/sensor/notifier/temperature/config"
#define MQTT_DISCOVERY_HUM    "homeassistant/sensor/notifier/humidity/config"

// Uptime Kuma feeds (controlla qualsiasi servizio down in ciascun feed)
#define UPTIME_RSS_CASAVO  "https://uptime.casavo.com/status/casavo/rss"
#define UPTIME_RSS_HOME    "https://uptime.alorenzi.eu/status/home/rss"


// ----------- EPD (1.54" D67) -----------
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

Adafruit_SHTC3 shtc3;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

float tempC  = NAN;
float humPct = NAN;
uint8_t partialRefreshCount = 0;
bool mqttDiscoverySent = false;
bool ntpSynced = false;
char timeStr[6] = "--:--";
String uptimeDownName = "";

bool connectToWifi(uint32_t timeoutMs);
bool connectToMqtt();
void syncNtp();
void updateTimeStr();
void publishMqttDiscovery();
void publishSensorData();
bool readSensor(float& outTempC, float& outHumPct);
String findDownInFeed(const char* url);
void epdDraw();

void setup() {
  Serial.begin(115200);
  // Aspetta il monitor seriale (max 5s), poi continua comunque
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 5000) delay(10);
  Serial.println("\n--- Notifier boot ---");

  pinMode(VBAT_PWR, OUTPUT);
  digitalWrite(VBAT_PWR, HIGH);
  Serial.println("Power rails ON");

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, LOW); // EPD ON

  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);

  delay(10);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!shtc3.begin()) {
    Serial.println("ERROR: SHTC3 not found!");
  } else {
    Serial.println("SHTC3 OK");
  }

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(3);
  Serial.println("EPD OK");

  Serial.print("MQTT broker: ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);

  if (connectToWifi(WIFI_TIMEOUT_MS)) {
    syncNtp();
  }
}

void loop() {
  Serial.println("\n--- Loop ---");

  updateTimeStr();
  Serial.print("Time: "); Serial.println(timeStr);

  if (!readSensor(tempC, humPct)) {
    Serial.println("SHTC3 read failed.");
  } else {
    Serial.print("Temp: "); Serial.print(tempC, 1); Serial.print(" C  ");
    Serial.print("Hum: ");  Serial.print(humPct, 1); Serial.println(" %");
  }

  if (connectToWifi(WIFI_TIMEOUT_MS)) {
    if (connectToMqtt()) {
      if (!mqttDiscoverySent) {
        publishMqttDiscovery();
        mqttDiscoverySent = true;
      }
      publishSensorData();
    }
    uptimeDownName = findDownInFeed(UPTIME_RSS_CASAVO);
    if (uptimeDownName.isEmpty()) {
      uptimeDownName = findDownInFeed(UPTIME_RSS_HOME);
    }
  } else {
    Serial.println("Skipping MQTT (no WiFi).");
  }

  Serial.println("Updating display...");
  epdDraw();

  Serial.print("Sleeping ");
  Serial.print(SLEEP_SECONDS);
  Serial.println("s...");
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_light_sleep_start();
}

bool connectToWifi(uint32_t timeoutMs)
{
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi already connected.");
    return true;
  }

  Serial.print("Connecting to WiFi \"" WIFI_SSID "\"...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK. IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("WiFi timeout.");
  return false;
}

bool connectToMqtt()
{
  if (mqtt.connected()) {
    Serial.println("MQTT already connected.");
    return true;
  }

  Serial.print("Connecting to MQTT...");
  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
  } else {
    ok = mqtt.connect(MQTT_CLIENT_ID);
  }

  if (ok) {
    Serial.println(" OK");
  } else {
    Serial.print(" FAILED, rc=");
    Serial.println(mqtt.state());
    // rc codes: -4=timeout, -3=conn lost, -2=conn failed, -1=disconnected, 1=bad proto, 2=bad client id, 3=unavailable, 4=bad credentials, 5=unauthorized
  }
  return ok;
}

void syncNtp()
{
  Serial.print("Syncing NTP...");
  configTzTime(NTP_TIMEZONE, "pool.ntp.org", "time.cloudflare.com");

  struct tm timeinfo;
  const uint32_t start = millis();
  while (!getLocalTime(&timeinfo) && (millis() - start) < NTP_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (getLocalTime(&timeinfo)) {
    ntpSynced = true;
    char buf[32];
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
    Serial.print("NTP OK: "); Serial.println(buf);
  } else {
    Serial.println("NTP sync failed.");
  }
}

void updateTimeStr()
{
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    strncpy(timeStr, "--:--", sizeof(timeStr));
  }
}

void publishMqttDiscovery()
{
  Serial.println("Publishing MQTT discovery...");

#define DEVICE_BLOCK \
    "\"device\":{\"identifiers\":[\"notifier\"]," \
    "\"name\":\"Notifier\"," \
    "\"model\":\"ESP32-S3-ePaper-1.54\"," \
    "\"manufacturer\":\"Waveshare\"}"

  bool okTemp = mqtt.publish(MQTT_DISCOVERY_TEMP,
    "{\"name\":\"Notifier Temperature\","
    "\"state_topic\":\"" MQTT_STATE_TOPIC "\","
    "\"value_template\":\"{{ value_json.temperature }}\","
    "\"unit_of_measurement\":\"°C\","
    "\"device_class\":\"temperature\","
    "\"unique_id\":\"notifier_temperature\","
    DEVICE_BLOCK "}",
    true
  );
  Serial.print("  temp discovery: "); Serial.println(okTemp ? "OK" : "FAILED");
  mqtt.loop();
  delay(100);

  bool okHum = mqtt.publish(MQTT_DISCOVERY_HUM,
    "{\"name\":\"Notifier Humidity\","
    "\"state_topic\":\"" MQTT_STATE_TOPIC "\","
    "\"value_template\":\"{{ value_json.humidity }}\","
    "\"unit_of_measurement\":\"%\","
    "\"device_class\":\"humidity\","
    "\"unique_id\":\"notifier_humidity\","
    DEVICE_BLOCK "}",
    true
  );
  Serial.print("  hum discovery:  "); Serial.println(okHum ? "OK" : "FAILED");
}

void publishSensorData()
{
  char payload[48];
  if (isnan(tempC) || isnan(humPct)) {
    snprintf(payload, sizeof(payload), "{\"temperature\":null,\"humidity\":null}");
  } else {
    snprintf(payload, sizeof(payload), "{\"temperature\":%.1f,\"humidity\":%.1f}", tempC, humPct);
  }
  mqtt.publish(MQTT_STATE_TOPIC, payload);
  Serial.println(payload);
}

bool readSensor(float& outTempC, float& outHumPct)
{
  sensors_event_t humEvent, tempEvent;
  bool ok = shtc3.getEvent(&humEvent, &tempEvent);
  if (!ok) {
    delay(5);
    ok = shtc3.getEvent(&humEvent, &tempEvent);
  }
  if (!ok) return false;

  outTempC  = tempEvent.temperature + TEMP_OFFSET_C;
  outHumPct = humEvent.relative_humidity;
  return true;
}

void epdDraw()
{
  if (partialRefreshCount == 0) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
  }
  partialRefreshCount = (partialRefreshCount + 1) % FULL_REFRESH_EVERY;

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // Time — centered, textSize 4 (char: 24x32 px), "HH:MM" = 5 chars = 120px wide
    display.setTextSize(4);
    display.setCursor((display.width() - 5 * 24) / 2, 3);
    display.print(timeStr);

    display.drawFastHLine(0, 42, display.width(), GxEPD_BLACK);

    if (!uptimeDownName.isEmpty()) {
      display.setTextSize(4);
      display.setCursor((display.width() - 7 * 24) / 2, 58);
      display.print("ALLARME");

      int nameW = uptimeDownName.length() * 24;
      display.setCursor(max(0, (display.width() - nameW) / 2), 96);
      display.print(uptimeDownName);

      display.setCursor((display.width() - 4 * 24) / 2, 134);
      display.print("DOWN");
    } else {
      // Label: TEMPERATURA
      display.setTextSize(2);
      display.setCursor(10, 48);
      display.print("TEMPERATURA");

      // Temperature value — textSize 4 (32px tall), suffix in textSize 2
      display.setTextSize(4);
      display.setCursor(10, 68);
      if (isnan(tempC)) {
        display.print("--.-");
      } else {
        display.print(tempC, 1);
      }
      display.setTextSize(2);
      display.print(" C");

      display.drawFastHLine(0, 108, display.width(), GxEPD_BLACK);

      // Label: UMIDITA'
      display.setTextSize(2);
      display.setCursor(10, 114);
      display.print("UMIDITA'");

      // Humidity value
      display.setTextSize(4);
      display.setCursor(10, 134);
      if (isnan(humPct)) {
        display.print("--.-");
      } else {
        display.print(humPct, 1);
      }
      display.setTextSize(2);
      display.print(" %");
    }

  } while (display.nextPage());
}

// "Degraded Service" is in the channel <description>, not in individual items.
// Items contain "X is down" / "X is up" titles, newest-first.
// Returns the name of the first currently-down service, or "" if all OK.
String findDownInFeed(const char* url)
{
  WiFiClientSecure sc;
  sc.setInsecure();
  HTTPClient http;
  http.begin(sc, url);
  http.setTimeout(8000);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Uptime HTTP error %d\n", code);
    http.end();
    return "";
  }

  String body = http.getString();
  http.end();

  // Fast path: "Degraded Service" lives in the channel section (before first <item>).
  // If it's absent there, everything is operational.
  int firstItem = body.indexOf("<item>");
  String channelPart = (firstItem >= 0) ? body.substring(0, firstItem) : body;
  if (channelPart.indexOf("Degraded Service") < 0) return "";

  // Scan items newest-first. Track seen service names so that a later "is up"
  // event correctly shadows an older "is down" for the same service.
  String seen = ",";
  int pos = 0;
  while (true) {
    int is = body.indexOf("<item>", pos);
    if (is < 0) break;
    int ie = body.indexOf("</item>", is);
    if (ie < 0) break;

    String item = body.substring(is, ie);

    int ts = item.indexOf("<title>");
    int te = item.indexOf("</title>");
    if (ts >= 0 && te > ts) {
      String title = item.substring(ts + 7, te);
      if (title.startsWith("<![CDATA[") && title.endsWith("]]>")) {
        title = title.substring(9, title.length() - 3);
      }

      int downIdx = title.indexOf(" is down");
      int upIdx   = title.indexOf(" is up");

      if (downIdx > 0) {
        String name = title.substring(0, downIdx);
        String key  = "," + name + ",";
        if (seen.indexOf(key) < 0) {
          seen += name + ",";
          Serial.printf("Uptime down: %s\n", name.c_str());
          return name;
        }
      } else if (upIdx > 0) {
        String name = title.substring(0, upIdx);
        String key  = "," + name + ",";
        if (seen.indexOf(key) < 0) seen += name + ","; // mark as recovered
      }
    }

    pos = ie + 7;
  }

  return "";
}

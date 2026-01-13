#include <WiFi.h>
#include <PubSubClient.h>

// ---------- User config ----------
static const char* WIFI_SSID = "YOUR_WIFI";
static const char* WIFI_PASS = "YOUR_PASS";

// test-server
static const char* TOP_PREFIX = "azbi/3c71bf52d1e0";

static const char* MQTT_HOST = "mqtt.example.com";
static const uint16_t MQTT_PORT = 1883;         // später ggf. 8883
static const char* MQTT_USER = "mqttuser";
static const char* MQTT_PASS = "mqttpass";

static const char* CLIENT_ID = "house1-sensors";

// Pins (Beispiele)
static const int PIN_LDR = 34; // ADC1 pins sind weniger WiFi-zickig (z.B. 32-39)

// Schwellen / Logik
static const int   LDR_BRIGHT_THRESHOLD = 2000;   // ADC 0..4095, anpassen!
static const float RH_WET_THRESHOLD     = 65.0;   // falls du echte RH hast

// Publish timing
static const uint32_t PUBLISH_HEARTBEAT_MS = 15000; // periodischer "1" refresh optional
static const uint32_t PUBLISH_NUMERIC_MS = 5000;  // RH/ADC alle X ms

// ---------- MQTT topics ----------
// ---------- Topic scheme (README) ----------
static const char* HOUSE_ID   = "haus1";
static const char* TOP_STATUS = "h2h/haus1/sys/status";   // 1=online, 0=offline (retain)

// metrics:
// h2h/haus1/wc/humid
// h2h/haus1/stube/light_adc

static void buildTopic(char* out, size_t outLen, const char* room, const char* metric) {
  // h2h/<house_id>/<room>/<metric>
  snprintf(out, outLen, "h2h/%s/%s/%s", HOUSE_ID, room, metric);
}

static void publishNumber(const char* room, const char* metric, float value, bool retain=false) {
  char topic[128];
  buildTopic(topic, sizeof(topic), room, metric);

  char payload[32];
  dtostrf(value, 0, 2, payload); // numeric only
  mqtt.publish(topic, payload, retain);
}

// ---------- Globals ----------
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

static uint32_t lastHeartbeatMs = 0;
static uint32_t lastNumericMs = 0;

// Zustände (nur publish bei Änderung)


// Dummy: ersetze das durch deinen echten Feuchtesensor (DHT/SHT/whatever)
float readRelativeHumidityDummy() {
  // TODO: echten Sensor einbauen
  // Für jetzt: immer -1 => "unavailable"
  return -1.0f;
}

int readLdrAdc() {
  // ESP32 ADC read 0..4095
  return analogRead(PIN_LDR);
}

void wifi_init() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(200);
  }
}

bool mqtt_connect() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  // LWT: wenn Haus1 wegstirbt -> offline (retain=true)
  const bool willRetain = true;
  const uint8_t willQos = 1;
  const char* willMsg = "0";

  bool ok = mqtt.connect(
    CLIENT_ID,
    MQTT_USER,
    MQTT_PASS,
    TOP_STATUS,
    willQos,
    willRetain,
    willMsg
  );

  if (ok) {
    // Online setzen (retain=true), damit Haus2 sofort weiß was Sache ist
    mqtt.publish(TOP_STATUS, "1", true);

    // Optional: beim Connect gleich die letzten States nochmal raushauen (retain)
    // (machen wir sowieso in sensor_loop wenn haveLast* noch false ist)
  }
  return ok;
}

void mqtt_ensure_connected() {
  if (mqtt.connected()) return;

  // Nicht zu aggressiv reconnecten
  static uint32_t lastTry = 0;
  if (millis() - lastTry < 2000) return;
  lastTry = millis();

  if (WiFi.status() != WL_CONNECTED) {
    wifi_init();
  }
  mqtt_connect();
}

void sensors_loop() {
  // 1) LDR -> "bright/dark"
  int adc = readLdrAdc();
  bool isBright = (adc >= LDR_BRIGHT_THRESHOLD);

  // 2) Feuchte -> "wet/dry" (Dummy oder echter RH)
  float rh = readRelativeHumidityDummy();
  bool isWet;
  bool wetAvailable = true;

  if (rh < 0.0f) {
    // Kein RH verfügbar -> hier könntest du z.B. nur LDR machen
    wetAvailable = false;
    isWet = false;
  } else {
    isWet = (rh >= RH_WET_THRESHOLD);
  }

  // Publish bei Änderung (oder beim allerersten Mal)
  const uint32_t now = millis();

  if (mqtt.connected()) {
  const uint32_t now = millis();

  // Numeric values every X seconds (no sender-side heuristics)
  if (now - lastNumericMs >= PUBLISH_NUMERIC_MS) {
    lastNumericMs = now;

    publishNumber("stube", "light_adc", (float)adc, false);

    if (rh >= 0.0f) {
      publishNumber("wc", "humid", rh, false);
    }
  }

  // Optional: status refresh (retain)
  if (now - lastHeartbeatMs >= PUBLISH_HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    mqtt.publish(TOP_STATUS, "1", true);
  }
}


void setup() {
  // analogRead default ok; evtl analogSetPinAttenuation(PIN_LDR, ADC_11db);
  pinMode(PIN_LDR, INPUT);

  wifi_init();
  mqtt.setBufferSize(256);
  mqtt_connect();
}

void loop() {
  mqtt_ensure_connected();
  mqtt.loop();
  sensors_loop();
  delay(20);
}

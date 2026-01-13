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
static const uint32_t PUBLISH_MIN_MS = 1000;      // min Zeit zwischen Publishes bei Änderungen
static const uint32_t PUBLISH_HEARTBEAT_MS = 15000; // periodischer "online" refresh optional
static const uint32_t PUBLISH_NUMERIC_MS = 5000;  // RH/ADC alle X ms

// ---------- MQTT topics ----------
static const char* TOP_STATUS          = "h/1/status";
static const char* TOP_WC_STATE        = "h/1/wc/humidity/state";
static const char* TOP_WC_RH           = "h/1/wc/humidity/rh";
static const char* TOP_STUBE_STATE     = "h/1/stube/light/state";
static const char* TOP_STUBE_ADC       = "h/1/stube/light/adc";

// ---------- Globals ----------
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

static uint32_t lastPublishMs = 0;
static uint32_t lastHeartbeatMs = 0;
static uint32_t lastNumericMs = 0;

// Zustände (nur publish bei Änderung)
static bool lastIsWet = false;
static bool lastIsBright = false;
static bool haveLastWet = false;
static bool haveLastBright = false;

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
  const char* willMsg = "offline";

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
    mqtt.publish(TOP_STATUS, "online", true);

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

void publish_state_if_needed(const char* topic, const char* payload) {
  // QoS in PubSubClient ist begrenzt; retain ist hier der Hauptgewinn
  mqtt.publish(topic, payload, true);
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
    if (wetAvailable) {
      if (!haveLastWet || (isWet != lastIsWet)) {
        if (now - lastPublishMs >= PUBLISH_MIN_MS) {
          publish_state_if_needed(TOP_WC_STATE, isWet ? "wet" : "dry");
          lastIsWet = isWet;
          haveLastWet = true;
          lastPublishMs = now;
        }
      }
    }

    if (!haveLastBright || (isBright != lastIsBright)) {
      if (now - lastPublishMs >= PUBLISH_MIN_MS) {
        publish_state_if_needed(TOP_STUBE_STATE, isBright ? "bright" : "dark");
        lastIsBright = isBright;
        haveLastBright = true;
        lastPublishMs = now;
      }
    }

    // Optional: numerische Werte alle X Sekunden (nicht retain nötig, aber ok)
    if (now - lastNumericMs >= PUBLISH_NUMERIC_MS) {
      lastNumericMs = now;

      char buf[16];
      snprintf(buf, sizeof(buf), "%d", adc);
      mqtt.publish(TOP_STUBE_ADC, buf, false);

      if (wetAvailable) {
        dtostrf(rh, 0, 1, buf); // 1 Nachkommastelle
        mqtt.publish(TOP_WC_RH, buf, false);
      }
    }

    // Optional: heartbeat online refresh (retain)
    if (now - lastHeartbeatMs >= PUBLISH_HEARTBEAT_MS) {
      lastHeartbeatMs = now;
      mqtt.publish(TOP_STATUS, "online", true);
    }
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

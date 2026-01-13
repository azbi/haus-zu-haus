#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>

// ---------- User config ----------
static const char* WIFI_SSID = "YOUR_WIFI";
static const char* WIFI_PASS = "YOUR_PASS";

// test-server
static const char* TOP_PREFIX = "azbi/3c71bf52d1e0";

static const char* MQTT_HOST = "mqtt.example.com";
static const uint16_t MQTT_PORT = 1883;
static const char* MQTT_USER = "mqttuser";
static const char* MQTT_PASS = "mqttpass";

static const char* CLIENT_ID = "house2-dollhouse";

// LED config
static const int PIN_LED = 5;            // anpassen!
static const int NUM_LEDS = 60;          // anpassen!
static const int WC_LED_START = 0;
static const int WC_LED_COUNT = 10;
static const int STUBE_LED_START = 10;
static const int STUBE_LED_COUNT = 10;

// Topics (numeric-only scheme)
static const char* TOP_STATUS    = "h2h/haus1/sys/status";       // 1/0 retained
static const char* TOP_WC_HUMID  = "h2h/haus1/wc/humid";         // float (%)
static const char* TOP_STUBE_ADC = "h2h/haus1/stube/light_adc";  // int (0..4095)

// ---------- Globals ----------
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

CRGB leds[NUM_LEDS];

static bool sourceOnline = false;

// kleine Helfer
void fillRange(int start, int count, const CRGB& c) {
  for (int i = 0; i < count; i++) {
    int idx = start + i;
    if (idx >= 0 && idx < NUM_LEDS) leds[idx] = c;
  }
}

void showAll() {
  FastLED.show();
}

void setOfflineVisual() {
  // z.B. alles dunkelgrau (oder rot blinken, wenn du magst)
  fill_solid(leds, NUM_LEDS, CRGB(10,10,10));
  showAll();
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  // Payload in null-terminated string kopieren
  static char msg[64];
  unsigned int n = (length < sizeof(msg)-1) ? length : (sizeof(msg)-1);
  memcpy(msg, payload, n);
  msg[n] = 0;

  if (strcmp(topic, TOP_STATUS) == 0) {
    sourceOnline = (atoi(msg) == 1);
    if (!sourceOnline) {
      setOfflineVisual();
    }
    return;
  }

  // Wenn Quelle offline ist, ignorieren wir States (optional)
  if (!sourceOnline) return;

  if (strcmp(topic, TOP_WC_HUMID) == 0) {
  float rh = atof(msg);
  if (rh >= 65.0f) {
    fillRange(WC_LED_START, WC_LED_COUNT, CRGB(0,0,255));   // wet-ish -> blau
  } else {
    fillRange(WC_LED_START, WC_LED_COUNT, CRGB(255,80,0));  // dry-ish -> orange
  }
  showAll();
  return;
}

if (strcmp(topic, TOP_STUBE_ADC) == 0) {
  int adc = atoi(msg);
  if (adc >= 2000) {
    fillRange(STUBE_LED_START, STUBE_LED_COUNT, CRGB(255,255,0)); // bright -> gelb
  } else {
    fillRange(STUBE_LED_START, STUBE_LED_COUNT, CRGB::Black);     // dark -> aus
  }
  showAll();
  return;
}

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
  mqtt.setCallback(mqtt_callback);

  bool ok = mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS);
  if (ok) {
    // Alles von Haus1 holen (inkl retained)
    mqtt.subscribe(TOP_STATUS, 1);
    mqtt.subscribe(TOP_WC_HUMID, 1);
    mqtt.subscribe(TOP_STUBE_ADC, 1);
  }
  return ok;
}

void mqtt_ensure_connected() {
  if (mqtt.connected()) return;

  static uint32_t lastTry = 0;
  if (millis() - lastTry < 2000) return;
  lastTry = millis();

  if (WiFi.status() != WL_CONNECTED) wifi_init();
  mqtt_connect();
}

void leds_init() {
  FastLED.addLeds<WS2812B, PIN_LED, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(120);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  showAll();
}

void setup() {
  leds_init();
  setOfflineVisual();

  wifi_init();
  mqtt.setBufferSize(256);
  mqtt_connect();
}

void loop() {
  mqtt_ensure_connected();
  mqtt.loop();
  delay(10);
}

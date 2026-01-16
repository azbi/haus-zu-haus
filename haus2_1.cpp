// ============================================================
// haus2_1.ino  —  H2H Receiver/Visualizer (ESP32)
// - WiFi via WiFiManager (no credentials in code)
// - Config portal ONLY on GPIO4 long-press (3s) at boot
// - Separate 1-pixel WS2812 "WiFi Ampel" on GPIO5
// - House LEDs (rooms/tree) on GPIO16
// - MQTT subscribes numeric-only topics from haus1
// ============================================================


// ============================================================
//  INCLUDES
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>

#include <FastLED.h>
#include <WiFiManager.h>   // tzapu


// ============================================================
//  DEBUG (optional)
// ============================================================

#define DEBUG_SERIAL 1
#if DEBUG_SERIAL
  #define DPRINTLN(x) Serial.println(x)
  #define DPRINT(x)   Serial.print(x)
#else
  #define DPRINTLN(x) do{}while(0)
  #define DPRINT(x)   do{}while(0)
#endif


// ============================================================
//  PINS / BUTTON
// ============================================================

#define WIFI_RESET_PIN       4
#define WIFI_RESET_HOLD_MS   3000

#define WIFI_LED_PIN         5       // private 1-pixel status LED
#define WIFI_LED_COUNT       1

#define HOUSE_LED_PIN        16      // official LED strip (rooms/tree)


// ============================================================
//  MQTT CONFIG (no secrets in repo)
// ============================================================

// For initial testing you can use test.mosquitto.org (no auth, public).
// For real use: set to your broker and configure auth accordingly.
static const char* MQTT_HOST = "test.mosquitto.org";
static const uint16_t MQTT_PORT = 1883;

// If your broker needs auth, fill these (or leave empty for none).
static const char* MQTT_USER = "";    // e.g. "mqttuser"
static const char* MQTT_PASS = "";    // e.g. "mqttpass"

// Make this unique per device
static const char* CLIENT_ID = "haus2-esp32";

// Topics (numeric-only)
static const char* TOP_STATUS    = "h2h/haus1/sys/status";       // "1"/"0" retained
static const char* TOP_WC_HUMID  = "h2h/haus1/wc/humid";         // float (%)
static const char* TOP_STUBE_ADC = "h2h/haus1/stube/light_adc";  // int (0..4095)


// ============================================================
//  LED CONFIG (house strip layout)
// ============================================================

static const int NUM_LEDS = 60;     // adjust to your strip length

static const int WC_LED_START    = 0;
static const int WC_LED_COUNT    = 10;

static const int STUBE_LED_START = 10;
static const int STUBE_LED_COUNT = 10;


// ============================================================
//  GLOBALS
// ============================================================

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

CRGB wifiLed[WIFI_LED_COUNT];     // private status pixel
CRGB leds[NUM_LEDS];              // house strip

static bool sourceOnline = false;


// ============================================================
//  WIFI STATUS LED (private pixel)
// ============================================================

static inline void wifi_led_show() {
  FastLED.show();
}

void wifi_led_init() {
  FastLED.addLeds<NEOPIXEL, WIFI_LED_PIN>(wifiLed, WIFI_LED_COUNT);
  wifiLed[0] = CRGB::Black;
  wifi_led_show();
}

void wifi_led_set(const CRGB& c) {
  wifiLed[0] = c;
  wifi_led_show();
}


// ============================================================
//  HOUSE LED HELPERS
// ============================================================

void fillRange(int start, int count, const CRGB& c) {
  for (int i = 0; i < count; i++) {
    int idx = start + i;
    if (idx >= 0 && idx < NUM_LEDS) leds[idx] = c;
  }
}

void house_show() {
  FastLED.show();
}

void setOfflineVisual() {
  // everything dim gray
  fill_solid(leds, NUM_LEDS, CRGB(10,10,10));
  house_show();
}


// ============================================================
//  LED INIT / LOOP
// ============================================================

void leds_init() {
  // Add official strip (rooms/tree)
  FastLED.addLeds<WS2812B, HOUSE_LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(120);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  house_show();
}

void leds_loop() {
  // currently nothing time-based here
}


// ============================================================
//  WIFI (WiFiManager) INIT / LOOP
// ============================================================

static bool wifi_reset_button_held() {
  pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
  uint32_t t0 = millis();

  while (digitalRead(WIFI_RESET_PIN) == LOW) {
    if (millis() - t0 > WIFI_RESET_HOLD_MS) return true;
    delay(50);
  }
  return false;
}

void wifi_init() {
  // Boot state: not connected yet
  wifi_led_set(CRGB::Red);

  const bool forcePortal = wifi_reset_button_held();
  if (forcePortal) {
    DPRINTLN("WIFI: reset button held -> force config portal + clear settings");
    wifi_led_set(CRGB::Blue);
  } else {
    DPRINTLN("WIFI: normal boot (no portal)");
  }

  // Clean WiFi state (prevents "sta is connecting, cannot set config")
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_STA);
  delay(200);

  WiFiManager wm;
  wm.setDebugOutput(DEBUG_SERIAL ? true : false);

  // do NOT try forever; we do NOT wipe on failures
  wm.setConnectTimeout(15);

  // Portal timeout only matters when we actually start portal
  wm.setConfigPortalTimeout(180);

  if (forcePortal) {
    wm.resetSettings(); // ONLY here (manual wipe)
    wm.startConfigPortal("h2h-haus2-setup");
  } else {
    // autoConnect tries saved credentials; if none, it will start portal.
    // If you *really* want portal only on button, we can switch to:
    //   wm.setEnableConfigPortal(false);
    // But for now, autoConnect is convenient for first-time setup.
    bool ok = wm.autoConnect("h2h-haus2-setup");
    if (!ok) {
      DPRINTLN("WIFI: autoConnect failed; staying offline (no wipe, no reboot)");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_led_set(CRGB::Green);
    DPRINT("WIFI: connected, IP=");
    DPRINTLN(WiFi.localIP());
  } else {
    wifi_led_set(CRGB::Red);
    DPRINTLN("WIFI: NOT connected");
  }
}

void wifi_loop() {
  // no forced reconnect here (we do not want to mess with credentials)
  // (Optional later: detect WL_DISCONNECTED and set wifi_led red)
  if (WiFi.status() == WL_CONNECTED) {
    // keep green unless MQTT changes it later (optional)
  }
}


// ============================================================
//  MQTT CALLBACK
// ============================================================

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  static char msg[64];
  unsigned int n = (length < sizeof(msg)-1) ? length : (sizeof(msg)-1);
  memcpy(msg, payload, n);
  msg[n] = 0;

  if (strcmp(topic, TOP_STATUS) == 0) {
    sourceOnline = (atoi(msg) == 1);
    if (!sourceOnline) setOfflineVisual();
    return;
  }

  if (!sourceOnline) return;

  if (strcmp(topic, TOP_WC_HUMID) == 0) {
    float rh = atof(msg);
    if (rh >= 65.0f) {
      fillRange(WC_LED_START, WC_LED_COUNT, CRGB(0,0,255));     // blue
    } else {
      fillRange(WC_LED_START, WC_LED_COUNT, CRGB(255,80,0));    // orange
    }
    house_show();
    return;
  }

  if (strcmp(topic, TOP_STUBE_ADC) == 0) {
    int adc = atoi(msg);
    if (adc >= 2000) {
      fillRange(STUBE_LED_START, STUBE_LED_COUNT, CRGB(255,255,0)); // yellow
    } else {
      fillRange(STUBE_LED_START, STUBE_LED_COUNT, CRGB::Black);     // off
    }
    house_show();
    return;
  }
}


// ============================================================
//  MQTT INIT / LOOP
// ============================================================

bool mqtt_connect() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);

  if (WiFi.status() != WL_CONNECTED) return false;

  bool ok;
  if (MQTT_USER[0] != 0) ok = mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS);
  else                  ok = mqtt.connect(CLIENT_ID);

  if (ok) {
    mqtt.subscribe(TOP_STATUS, 1);
    mqtt.subscribe(TOP_WC_HUMID, 1);
    mqtt.subscribe(TOP_STUBE_ADC, 1);
  }
  return ok;
}

void mqtt_init() {
  // Optional: set WiFi LED to purple when MQTT is connected later.
  // For now, keep WiFi green as "WiFi OK".
  mqtt.setBufferSize(256);
  mqtt_connect();
}

void mqtt_loop() {
  static uint32_t lastTry = 0;

  if (!mqtt.connected()) {
    if (millis() - lastTry > 2000) {
      lastTry = millis();
      mqtt_connect();
    }
  }

  mqtt.loop();
}


// ============================================================
//  SETUP / LOOP (always at end)
// ============================================================

void setup() {
#if DEBUG_SERIAL
  Serial.begin(115200);
  delay(300);
  DPRINTLN("\nBOOT: haus2 starting");
#endif

  wifi_led_init();     // must be early
  leds_init();         // OK even if no strip attached (just no effect)

  setOfflineVisual();

  wifi_init();
  mqtt_init();
}

void loop() {
  wifi_loop();
  mqtt_loop();
  leds_loop();
  delay(10);
}

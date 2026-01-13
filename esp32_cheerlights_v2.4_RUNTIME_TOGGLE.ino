/*
 * ESP32 CheerLights + Custom Color LED Strip v2.4
 * 
 * Features:
 * - WiFi Config via Captive Portal (Button 1 held 3s)
 * - n-1 LEDs show CheerLights color (with 3 display modes)
 * - nth LED shows custom color from web (always, in all modes)
 * - LED count configurable via web interface
 * - Colors normalized to same perceived brightness
 * 
 * Display Modes (Button 2 = short press to cycle):
 * - Mode 0: All LEDs show current CheerLights color (default)
 * - Mode 1: Shift-along - new colors push old colors to the right
 * - Mode 2: Like Mode 1 + auto-duplicate if no change for 15 minutes
 * 
 * Hardware:
 * - ESP32
 * - WS2812(B) LED Strip on GPIO 5
 * - Button 1 on GPIO 4 (WiFi config, active low)
 * - Button 2 on GPIO 2 (Mode switch, active low)
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WebServer.h>

// ==================== KONFIGURATION ====================
#define VERSION "v2.3"             // Version String für HTML
#define SHOW_DEBUG false           // true = Debug-Sektion anzeigen, false = verstecken

// Phase 2: Zwei Custom LEDs mit eigenen URLs
#define CUSTOM_LED_0_ENABLED true   // true = LED 0 ist Custom
#define CUSTOM_LED_N_ENABLED true   // true = LED n ist Custom
#define LED_PIN 5              // GPIO für WS2812 Data
#define BUTTON_PIN 4           // GPIO für Config-Button (3s hold = WiFi reset)
#define MODE_BUTTON_PIN 2      // GPIO für Mode-Button (kurzer Druck = Mode wechseln)
#define LDR_PIN 34             // GPIO für LDR (ADC1_CH6, analog input)
#define BUTTON_HOLD_TIME 3000  // 3 Sekunden halten für Config-Mode
#define MODE_AUTO_DUPLICATE_TIME 900000  // 15 Minuten für Auto-Duplicate
#define STARTUP_ANIMATION_DURATION 5000  // 5 Sekunden Startup-Animation
#define STARTUP_ROTATION_INTERVAL 250    // 1/4 Sekunde zwischen Rotationen
#define LDR_SAMPLE_INTERVAL 5000         // LDR alle 5 Sekunden lesen
#define BRIGHTNESS_MIN 10                // Minimale Helligkeit (dunkel)
#define BRIGHTNESS_MAX 80                // Maximale Helligkeit (hell) - reduziert für Nachts
#define LDR_DARK_THRESHOLD 500           // ADC-Wert: dunkel (0-4095 Skala)
#define LDR_BRIGHT_THRESHOLD 3000        // ADC-Wert: hell

// ==================== GLOBALE VARIABLEN ====================
Preferences preferences;
WiFiManager wifiManager;
Adafruit_NeoPixel *strip = nullptr;
WebServer server(80);

int numLEDs = 10;              // Default: 10 LEDs
String customColorURL_LED0 = "https://example.com/led0.txt";  // URL für LED 0
String customColorURL_LEDN = "https://example.com/ledn.txt";  // URL für LED n
bool customLED0Enabled = true;  // Runtime: LED 0 Custom aktiviert?
bool customLEDNEnabled = true;  // Runtime: LED n Custom aktiviert?
uint32_t cheerLightsColor = 0;
uint32_t customColorLED0 = 0;    // Custom Color für LED 0
uint32_t customColorLEDN = 0;    // Custom Color für LED n
unsigned long lastCheerLightsUpdate = 0;
unsigned long lastCustomColorUpdate0 = 0;  // Für LED 0
unsigned long lastCustomColorUpdateN = 0;  // Für LED n
unsigned long lastColorChange = 0;  // Für Mode 2: Timestamp der letzten Farbänderung
const unsigned long updateInterval = 30000;  // Update alle 30 Sekunden

// Display Mode: 0 = alle gleich, 1 = shift-along, 2 = shift + auto-duplicate
int displayMode = 0;
uint32_t colorHistory[50] = {0};  // History für shift-along (max 50 CheerLights LEDs)

// LDR und Auto-Brightness
unsigned long lastLDRRead = 0;
int currentBrightness = BRIGHTNESS_MAX;
int targetBrightness = BRIGHTNESS_MAX;  // Ziel-Helligkeit für smooth transition
int currentLDRValue = 0;  // Aktueller LDR-Wert für Anzeige
bool ldrEnabled = true;  // LDR an/aus (kann im Web getoggelt werden)

// LDR-Konfiguration (aus Preferences, mit Defaults)
int brightnessMin = BRIGHTNESS_MIN;
int brightnessMax = BRIGHTNESS_MAX;
int ldrDarkThreshold = LDR_DARK_THRESHOLD;
int ldrBrightThreshold = LDR_BRIGHT_THRESHOLD;

// Function declarations
bool isHexColor(String str);
uint32_t normalizeColorBrightness(uint32_t color, uint8_t targetBrightness);
void checkModeButton();
void updateBrightnessFromLDR();
void smoothBrightnessTransition();
String getColorName(uint32_t color);
bool isLightColor(uint32_t color);
String colorToHex(uint32_t color);
String colorNameToHex(String colorName);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n========================================");
  Serial.print("ESP32 CheerLights Strip ");
  Serial.println(VERSION);
  Serial.println("========================================\n");

  // Button konfigurieren
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
  
  // LDR konfigurieren (Analog Input)
  pinMode(LDR_PIN, INPUT);
  
  // Preferences laden
  preferences.begin("ledstrip", false);
  numLEDs = preferences.getInt("numLEDs", 10);
  customColorURL_LED0 = preferences.getString("colorURL0", "https://example.com/led0.txt");
  customColorURL_LEDN = preferences.getString("colorURLN", "https://example.com/ledn.txt");
  customLED0Enabled = preferences.getBool("customLED0En", true);  // Runtime toggle
  customLEDNEnabled = preferences.getBool("customLEDNEn", true);  // Runtime toggle
  displayMode = preferences.getInt("displayMode", 0);
  ldrEnabled = preferences.getBool("ldrEnabled", true);
  
  // LDR-Konfiguration laden (mit Defaults falls nicht gesetzt)
  brightnessMin = preferences.getInt("brightMin", BRIGHTNESS_MIN);
  brightnessMax = preferences.getInt("brightMax", BRIGHTNESS_MAX);
  ldrDarkThreshold = preferences.getInt("ldrDark", LDR_DARK_THRESHOLD);
  ldrBrightThreshold = preferences.getInt("ldrBright", LDR_BRIGHT_THRESHOLD);
  
  preferences.end();
  
  Serial.printf("LED Count: %d\n", numLEDs);
  Serial.printf("Custom URL LED0: %s\n", customColorURL_LED0.c_str());
  Serial.printf("Custom URL LEDN: %s\n", customColorURL_LEDN.c_str());
  Serial.printf("Display Mode: %d\n", displayMode);
  Serial.printf("LDR Enabled: %s\n", ldrEnabled ? "Yes" : "No");
  Serial.printf("LDR Config: Min=%d, Max=%d, Dark=%d, Bright=%d\n", 
                brightnessMin, brightnessMax, ldrDarkThreshold, ldrBrightThreshold);

  // LED Strip initialisieren
  strip = new Adafruit_NeoPixel(numLEDs, LED_PIN, NEO_GRB + NEO_KHZ800);
  strip->begin();
  strip->setBrightness(currentBrightness);
  targetBrightness = currentBrightness; // Initial gleich setzen
  strip->show();
  
  // Initial LDR-Wert lesen (auch wenn deaktiviert, für Anzeige)
  currentLDRValue = analogRead(LDR_PIN);
  Serial.printf("Initial LDR: %d\n", currentLDRValue);

  // Startup-Animation
  startupAnimation();

  // Check ob Button gedrückt wird
  if (checkButtonHold()) {
    enterConfigMode();
  }

  // WiFi verbinden
  connectWiFi();

  // Web Server starten
  setupWebServer();
  server.begin();
  Serial.println("Web Server gestartet auf Port 80");

  // Erste Updates
  updateCheerLights();
  updateCustomColors();  // Beide Custom LEDs updaten
  
  // Initialisiere Color History für Modi 1 und 2
  if (displayMode == 1 || displayMode == 2) {
    // Anzahl CheerLights LEDs berechnen
    int cheerLightsCount = numLEDs;
    #if CUSTOM_LED_0_ENABLED
    if (customLED0Enabled) cheerLightsCount--;
    #endif
    #if CUSTOM_LED_N_ENABLED
    if (customLEDNEnabled) cheerLightsCount--;
    #endif
    
    for(int i = 0; i < cheerLightsCount && i < 50; i++) {
      colorHistory[i] = cheerLightsColor;
    }
    lastColorChange = millis();
    Serial.println("Color history initialized");
  }
  
  // Initialisiere Custom LEDs wenn noch nicht gesetzt
  #if CUSTOM_LED_0_ENABLED
  if (customColorLED0 == 0) {
    customColorLED0 = cheerLightsColor;
  }
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  if (customColorLEDN == 0) {
    customColorLEDN = cheerLightsColor;
  }
  #endif
  
  Serial.println("Setup complete!");
}

// ==================== MAIN LOOP ====================
void loop() {
  server.handleClient();
  
  // Check Button für Config-Mode
  if (checkButtonHold()) {
    enterConfigMode();
  }
  
  // Check Mode Button für Mode-Wechsel
  checkModeButton();
  
  // LDR-basierte Helligkeitsanpassung
  updateBrightnessFromLDR();
  
  // Smooth brightness transition (jeden Loop)
  smoothBrightnessTransition();

  // Regelmäßige Updates
  unsigned long now = millis();
  
  if (now - lastCheerLightsUpdate > updateInterval) {
    updateCheerLights();
  }
  
  #if CUSTOM_LED_0_ENABLED
  if (customLED0Enabled && now - lastCustomColorUpdate0 > updateInterval) {
    updateCustomColorLED0();
  }
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  if (customLEDNEnabled && now - lastCustomColorUpdateN > updateInterval) {
    updateCustomColorLEDN();
  }
  #endif

  // LEDs aktualisieren
  updateLEDs();
  
  delay(100);
}

// ==================== FUNKTIONEN ====================

void startupAnimation() {
  // Array mit allen CheerLights-Farben (distinkt und unterscheidbar!)
  const int numColors = 11;
  uint32_t cheerColors[numColors] = {
    strip->Color(200, 0, 0),      // Red
    strip->Color(0, 180, 0),      // Green
    strip->Color(0, 0, 200),      // Blue
    strip->Color(0, 180, 180),    // Cyan
    strip->Color(120, 120, 120),  // White
    strip->Color(150, 100, 50),   // Warmwhite
    strip->Color(120, 0, 180),    // Purple
    strip->Color(180, 0, 120),    // Magenta
    strip->Color(150, 150, 0),    // Yellow
    strip->Color(200, 80, 0),     // Orange
    strip->Color(200, 50, 100)    // Pink
  };
  
  // Initial: Verteile Farben auf LEDs (LED 0 = Farbe 0, LED 1 = Farbe 1, etc.)
  uint32_t ledColors[numLEDs];
  for(int i = 0; i < numLEDs; i++) {
    ledColors[i] = cheerColors[i % numColors];
    strip->setPixelColor(i, ledColors[i]);
  }
  strip->show();
  
  // Rotiere für STARTUP_ANIMATION_DURATION (5 Sekunden)
  unsigned long startTime = millis();
  unsigned long lastRotation = startTime;
  
  while(millis() - startTime < STARTUP_ANIMATION_DURATION) {
    if(millis() - lastRotation >= STARTUP_ROTATION_INTERVAL) {
      // Rotiere: alle LEDs um 1 nach rechts shiften
      uint32_t temp = ledColors[numLEDs - 1];
      for(int i = numLEDs - 1; i > 0; i--) {
        ledColors[i] = ledColors[i - 1];
      }
      ledColors[0] = temp;
      
      // LEDs aktualisieren
      for(int i = 0; i < numLEDs; i++) {
        strip->setPixelColor(i, ledColors[i]);
      }
      strip->show();
      
      lastRotation = millis();
    }
  }
  
  // Am Ende alles löschen
  strip->clear();
  strip->show();
  delay(300);
}

bool checkButtonHold() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    unsigned long pressStart = millis();
    
    // Visuelles Feedback
    for(int i = 0; i < numLEDs; i++) {
      strip->setPixelColor(i, strip->Color(100, 100, 0));
    }
    strip->show();
    
    while (digitalRead(BUTTON_PIN) == LOW) {
      if (millis() - pressStart > BUTTON_HOLD_TIME) {
        // Button lange genug gedrückt
        for(int i = 0; i < numLEDs; i++) {
          strip->setPixelColor(i, strip->Color(0, 100, 0));
        }
        strip->show();
        delay(500);
        return true;
      }
      delay(10);
    }
    
    // Button zu kurz gedrückt
    strip->clear();
    strip->show();
  }
  return false;
}


void checkModeButton() {
  static bool lastButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 50;
  
  bool reading = digitalRead(MODE_BUTTON_PIN);
  
  // Debouncing
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Button wurde gedrückt (falling edge)
    if (reading == LOW && lastButtonState == HIGH) {
      // Mode wechseln
      displayMode = (displayMode + 1) % 3;
      
      Serial.printf("\n=== Mode Changed ===\n");
      Serial.printf("New Display Mode: %d\n", displayMode);
      
      // Speichern
      preferences.begin("ledstrip", false);
      preferences.putInt("displayMode", displayMode);
      preferences.end();
      
      // Bei Wechsel zu Mode 1 oder 2: History initialisieren
      if (displayMode == 1 || displayMode == 2) {
        int cheerLightsCount = numLEDs;
        #if CUSTOM_LED_0_ENABLED
        if (customLED0Enabled) cheerLightsCount--;
        #endif
        #if CUSTOM_LED_N_ENABLED
        if (customLEDNEnabled) cheerLightsCount--;
        #endif
        
        for(int i = 0; i < cheerLightsCount && i < 50; i++) {
          colorHistory[i] = cheerLightsColor;
        }
        lastColorChange = millis();
      }
      
      // Visuelles Feedback: Anzahl Blinks = Mode
      for(int i = 0; i < displayMode + 1; i++) {
        for(int j = 0; j < numLEDs; j++) {
          // Alle LEDs außer aktive Custom blinken
          bool isCustom = false;
          #if CUSTOM_LED_0_ENABLED
          if (j == 0 && customLED0Enabled) isCustom = true;
          #endif
          #if CUSTOM_LED_N_ENABLED
          if (j == numLEDs - 1 && customLEDNEnabled) isCustom = true;
          #endif
          
          if (!isCustom) {
            strip->setPixelColor(j, strip->Color(50, 0, 50));
          }
        }
        strip->show();
        delay(200);
        updateLEDs();  // Zurück zu normal
        delay(200);
      }
    }
  }
  
  lastButtonState = reading;
}

void enterConfigMode() {
  Serial.println("Config-Mode aktiviert!");
  
  // LEDs orange blinken lassen
  for(int i = 0; i < 5; i++) {
    for(int j = 0; j < numLEDs; j++) {
      strip->setPixelColor(j, strip->Color(100, 30, 0));
    }
    strip->show();
    delay(200);
    strip->clear();
    strip->show();
    delay(200);
  }

  // WiFiManager starten
  wifiManager.resetSettings();  // Alte Settings löschen
  
  if (!wifiManager.startConfigPortal("ESP32-LED-Setup")) {
    Serial.println("Failed to connect or hit timeout");
    ESP.restart();
  }
  
  Serial.println("WiFi konfiguriert!");
  ESP.restart();
}

void connectWiFi() {
  Serial.println("Verbinde mit WiFi...");
  
  // AutoConnect mit gespeicherten Credentials
  wifiManager.setConfigPortalTimeout(180);  // 3 Minuten Timeout
  
  if (!wifiManager.autoConnect("ESP32-LED-Setup")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }
  
  Serial.println("WiFi verbunden!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  
  // Erfolgs-Animation
  for(int i = 0; i < 3; i++) {
    for(int j = 0; j < numLEDs; j++) {
      strip->setPixelColor(j, strip->Color(0, 100, 0));
    }
    strip->show();
    delay(200);
    strip->clear();
    strip->show();
    delay(200);
  }
}

void updateCheerLights() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - skipping CheerLights update");
    lastCheerLightsUpdate = millis();
    return;
  }
  
  Serial.println("\n=== Updating CheerLights ===");
  HTTPClient http;
  
  // CheerLights API
  http.begin("https://api.thingspeak.com/channels/1417/field/2/last.json");
  http.setTimeout(10000);
  int httpCode = http.GET();
  
  Serial.printf("HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.printf("Raw JSON: %s\n", payload.c_str());
    
    // JSON parsen
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.printf("JSON Parse Error: %s\n", error.c_str());
    } else {
      String colorName = doc["field2"].as<String>();
      colorName.trim();
      colorName.toLowerCase();
      
      Serial.printf("Extracted Color Name: '%s'\n", colorName.c_str());
      
      uint32_t newColor = parseColorName(colorName);
      
      // Zeige RGB-Werte
      uint8_t r = (newColor >> 16) & 0xFF;
      uint8_t g = (newColor >> 8) & 0xFF;
      uint8_t b = newColor & 0xFF;
      Serial.printf("RGB Values: R=%d, G=%d, B=%d\n", r, g, b);
      
      // Shift history für Modi 1 und 2 (wenn Farbe sich geändert hat)
      if (newColor != cheerLightsColor && (displayMode == 1 || displayMode == 2)) {
        // Anzahl CheerLights LEDs berechnen
        int cheerLightsCount = numLEDs;
        #if CUSTOM_LED_0_ENABLED
        if (customLED0Enabled) cheerLightsCount--;
        #endif
        #if CUSTOM_LED_N_ENABLED
        if (customLEDNEnabled) cheerLightsCount--;
        #endif
        
        // Shift history: alle Farben nach rechts (max 50)
        int maxShift = cheerLightsCount < 50 ? cheerLightsCount : 50;
        for(int i = maxShift - 1; i > 0; i--) {
          colorHistory[i] = colorHistory[i - 1];
        }
        colorHistory[0] = newColor;
        Serial.println("Color changed - history shifted");
        lastColorChange = millis();
      }
      
      cheerLightsColor = newColor;
      lastCheerLightsUpdate = millis();
      Serial.println("CheerLights update successful!");
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  
  http.end();
  
  // Mode 2: Auto-duplicate wenn 15 Minuten keine Änderung
  if (displayMode == 2) {
    unsigned long timeSinceChange = millis() - lastColorChange;
    if (timeSinceChange > MODE_AUTO_DUPLICATE_TIME) {
      Serial.println("Mode 2: 15 minutes passed - auto-duplicating color");
      
      // Anzahl CheerLights LEDs berechnen
      int cheerLightsCount = numLEDs;
      #if CUSTOM_LED_0_ENABLED
      if (customLED0Enabled) cheerLightsCount--;
      #endif
      #if CUSTOM_LED_N_ENABLED
      if (customLEDNEnabled) cheerLightsCount--;
      #endif
      
      // Shift history
      int maxShift = cheerLightsCount < 50 ? cheerLightsCount : 50;
      for(int i = maxShift - 1; i > 0; i--) {
        colorHistory[i] = colorHistory[i - 1];
      }
      colorHistory[0] = cheerLightsColor;
      lastColorChange = millis();
    }
  }
  
  Serial.println("=== CheerLights Update Complete ===\n");
}

void updateCustomColorLED0() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - skipping LED0 Custom Color");
    lastCustomColorUpdate0 = millis();
    return;
  }
  if (customColorURL_LED0.length() < 10) {
    Serial.println("LED0 URL too short - skipping");
    lastCustomColorUpdate0 = millis();
    return;
  }
  
  Serial.println("\n=== Updating Custom Color LED0 ===");
  Serial.printf("URL: %s\n", customColorURL_LED0.c_str());
  
  HTTPClient http;
  http.begin(customColorURL_LED0);
  http.setTimeout(5000);
  int httpCode = http.GET();
  
  Serial.printf("HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    
    if (payload.startsWith("#")) {
      payload = payload.substring(1);
    }
    
    if (payload.length() == 6) {
      long color = strtol(payload.c_str(), NULL, 16);
      uint32_t rawColor = strip->Color(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
      );
      
      customColorLED0 = normalizeColorBrightness(rawColor, 100);
      
      uint8_t r = (customColorLED0 >> 16) & 0xFF;
      uint8_t g = (customColorLED0 >> 8) & 0xFF;
      uint8_t b = customColorLED0 & 0xFF;
      Serial.printf("LED0 RGB: R=%d, G=%d, B=%d\n", r, g, b);
      
      Serial.println("LED0 Custom Color update successful!");
    } else {
      Serial.printf("ERROR: Invalid hex length: %d\n", payload.length());
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  
  lastCustomColorUpdate0 = millis();
  http.end();
  Serial.println("=== LED0 Update Complete ===\n");
}

void updateCustomColorLEDN() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - skipping LEDN Custom Color");
    lastCustomColorUpdateN = millis();
    return;
  }
  if (customColorURL_LEDN.length() < 10) {
    Serial.println("LEDN URL too short - skipping");
    lastCustomColorUpdateN = millis();
    return;
  }
  
  Serial.println("\n=== Updating Custom Color LEDN ===");
  Serial.printf("URL: %s\n", customColorURL_LEDN.c_str());
  
  HTTPClient http;
  http.begin(customColorURL_LEDN);
  http.setTimeout(5000);
  int httpCode = http.GET();
  
  Serial.printf("HTTP Code: %d\n", httpCode);
  
  if (httpCode == 200) {
    String payload = http.getString();
    payload.trim();
    
    if (payload.startsWith("#")) {
      payload = payload.substring(1);
    }
    
    if (payload.length() == 6) {
      long color = strtol(payload.c_str(), NULL, 16);
      uint32_t rawColor = strip->Color(
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF
      );
      
      customColorLEDN = normalizeColorBrightness(rawColor, 100);
      
      uint8_t r = (customColorLEDN >> 16) & 0xFF;
      uint8_t g = (customColorLEDN >> 8) & 0xFF;
      uint8_t b = customColorLEDN & 0xFF;
      Serial.printf("LEDN RGB: R=%d, G=%d, B=%d\n", r, g, b);
      
      Serial.println("LEDN Custom Color update successful!");
    } else {
      Serial.printf("ERROR: Invalid hex length: %d\n", payload.length());
    }
  } else {
    Serial.printf("HTTP Error: %d\n", httpCode);
  }
  
  lastCustomColorUpdateN = millis();
  http.end();
  Serial.println("=== LEDN Update Complete ===\n");
}

// Helper function to update both custom colors (for setup)
void updateCustomColors() {
  #if CUSTOM_LED_0_ENABLED
  updateCustomColorLED0();
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  updateCustomColorLEDN();
  #endif
}

uint32_t parseColorName(String colorName) {
  colorName.toLowerCase();
  colorName.trim();
  
  Serial.printf("Parsing color: '%s'\n", colorName.c_str());
  
  uint32_t color = 0;
  
  // Check if it's a hex code (starts with # or is 6 characters)
  if (colorName.startsWith("#") || (colorName.length() == 6 && isHexColor(colorName))) {
    // Remove # if present
    if (colorName.startsWith("#")) {
      colorName = colorName.substring(1);
    }
    
    if (colorName.length() == 6) {
      long colorValue = strtol(colorName.c_str(), NULL, 16);
      uint8_t r = (colorValue >> 16) & 0xFF;
      uint8_t g = (colorValue >> 8) & 0xFF;
      uint8_t b = colorValue & 0xFF;
      
      Serial.printf("Parsed as HEX - RGB: R=%d, G=%d, B=%d\n", r, g, b);
      color = strip->Color(r, g, b);
      
      // Normalisiere Hex-Codes auf gleiche Helligkeit
      color = normalizeColorBrightness(color, 100);
    }
  }
  // Parse as color name - DISTINKTE Farben, gut unterscheidbar!
  else if (colorName == "red") color = strip->Color(200, 0, 0);        // Kräftiges Rot
  else if (colorName == "green") color = strip->Color(0, 180, 0);      // Sattes Grün
  else if (colorName == "blue") color = strip->Color(0, 0, 200);       // Tiefblau
  else if (colorName == "cyan") color = strip->Color(0, 180, 180);     // Türkis (deutlich von Weiß)
  else if (colorName == "white") color = strip->Color(120, 120, 120);  // Neutrales Weiß
  else if (colorName == "warmwhite" || colorName == "oldlace") color = strip->Color(150, 100, 50); // Warmweiß (orange Stich!)
  else if (colorName == "purple") color = strip->Color(120, 0, 180);   // Lila (mehr Blau als Rot)
  else if (colorName == "magenta") color = strip->Color(180, 0, 120);  // Magenta (mehr Rot als Blau)
  else if (colorName == "yellow") color = strip->Color(150, 150, 0);   // Gelb
  else if (colorName == "orange") color = strip->Color(200, 80, 0);    // Orange (weniger Grün)
  else if (colorName == "pink") color = strip->Color(200, 50, 100);    // Pink (deutlich pink!)
  else {
    Serial.printf("WARNING: Unknown color '%s', defaulting to white\n", colorName.c_str());
    color = strip->Color(120, 120, 120);  // Gedimmtes Weiß
  }
  
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  Serial.printf("Final RGB: R=%d, G=%d, B=%d (Luminance: %.1f)\n", r, g, b, 0.299*r + 0.587*g + 0.114*b);
  
  return color;
}

bool isHexColor(String str) {
  if (str.length() != 6) return false;
  for (int i = 0; i < 6; i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

// Normalisiert Farbe auf gleiche perzeptuelle Helligkeit
// Nur DIMMEN, nicht aufhellen (um Clipping zu vermeiden)
uint32_t normalizeColorBrightness(uint32_t color, uint8_t targetBrightness) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  
  // Berechne perzeptuelle Helligkeit (Luminanz)
  // Human eye sensitivity: ~30% red, 59% green, 11% blue
  float currentBrightness = (0.299 * r + 0.587 * g + 0.114 * b);
  
  if (currentBrightness < 1) currentBrightness = 1; // Verhindere Division durch 0
  
  // Nur dimmen, nicht aufhellen (verhindert Clipping-Probleme)
  if (currentBrightness > targetBrightness) {
    float scale = targetBrightness / currentBrightness;
    
    r = (uint8_t)(r * scale);
    g = (uint8_t)(g * scale);
    b = (uint8_t)(b * scale);
  }
  // Wenn Farbe dunkler ist als Ziel, lasse sie so (keine Aufhellung)
  
  return strip->Color(r, g, b);
}

// LDR auslesen und Helligkeit anpassen
void updateBrightnessFromLDR() {
  if (!ldrEnabled) return;
  
  unsigned long now = millis();
  if (now - lastLDRRead < LDR_SAMPLE_INTERVAL) return;
  
  lastLDRRead = now;
  
  // LDR auslesen (ADC 0-4095)
  currentLDRValue = analogRead(LDR_PIN);
  
  // Map zu Helligkeit mit konfigurierbaren Werten
  int newBrightness = map(currentLDRValue, ldrDarkThreshold, ldrBrightThreshold, 
                          brightnessMin, brightnessMax);
  newBrightness = constrain(newBrightness, brightnessMin, brightnessMax);
  
  // Nur ändern wenn Unterschied > 5 (verhindert flackern)
  if (abs(newBrightness - targetBrightness) > 5) {
    targetBrightness = newBrightness;
    Serial.printf("LDR: %d → Target Brightness: %d (%d%%)\n", currentLDRValue, targetBrightness, 
                  (targetBrightness * 100) / 255);
  }
}

// Smooth brightness transition (called every loop)
void smoothBrightnessTransition() {
  if (currentBrightness != targetBrightness) {
    // Schrittweise anpassen (2 pro Loop = smooth aber nicht zu langsam)
    if (currentBrightness < targetBrightness) {
      currentBrightness = min(currentBrightness + 2, targetBrightness);
    } else {
      currentBrightness = max(currentBrightness - 2, targetBrightness);
    }
    strip->setBrightness(currentBrightness);
  }
}

// Gibt Farbnamen für uint32_t zurück (für HTML-Anzeige)
String getColorName(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  
  // Prüfe auf bekannte CheerLights Farben (genauer)
  if (r > 180 && g < 50 && b < 50) return "Red";
  if (r < 50 && g > 150 && b < 50) return "Green";
  if (r < 50 && g < 50 && b > 180) return "Blue";
  if (r < 50 && g > 130 && b > 130) return "Cyan";
  if (r > 100 && g > 100 && b > 100 && abs(r-g) < 30 && abs(g-b) < 30) return "White";
  if (r > 130 && g > 80 && b < 70) return "Warmwhite";
  if (r > 100 && g < 50 && b > 100) return "Purple";
  if (r > 150 && g < 50 && b > 50) return "Magenta";
  if (r > 130 && g > 130 && b < 50) return "Yellow";
  if (r > 180 && g < 120 && b < 50) return "Orange";
  if (r > 180 && g < 120 && b > 50 && b < 150) return "Pink";
  
  // Bessere Fallbacks - gib immer Hex zurück statt Namen
  // Das ist klarer und weniger verwirrend
  char hex[8];
  sprintf(hex, "#%02x%02x%02x", r, g, b);
  return String(hex);
}

// Prüft ob Farbe hell ist (braucht dunkle Schrift)
bool isLightColor(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  
  // Berechne Luminanz (perceived brightness)
  // Formula: 0.299*R + 0.587*G + 0.114*B
  float luminance = (0.299 * r + 0.587 * g + 0.114 * b);
  
  return luminance > 186;  // Threshold für helle Farben
}

// Konvertiert uint32_t Farbe zu Hex-String
String colorToHex(uint32_t color) {
  uint8_t r = (color >> 16) & 0xFF;
  uint8_t g = (color >> 8) & 0xFF;
  uint8_t b = color & 0xFF;
  char hex[8];
  sprintf(hex, "#%02x%02x%02x", r, g, b);
  return String(hex);
}

// Konvertiert Farbnamen zu Hex-Code für HTML (nur für bekannte Namen)
String colorNameToHex(String colorName) {
  colorName.toLowerCase();
  if (colorName == "red") return "#c80000";
  if (colorName == "green") return "#00b400";
  if (colorName == "blue") return "#0000c8";
  if (colorName == "cyan") return "#00b4b4";
  if (colorName == "white") return "#787878";
  if (colorName == "warmwhite" || colorName == "oldlace") return "#966432";
  if (colorName == "purple") return "#7800b4";
  if (colorName == "magenta") return "#b40078";
  if (colorName == "yellow") return "#969600";
  if (colorName == "orange") return "#c85000";
  if (colorName == "pink") return "#c83264";
  
  // Falls colorName schon ein Hex ist (#XXXXXX), gib ihn direkt zurück
  if (colorName.startsWith("#") && colorName.length() == 7) {
    return colorName;
  }
  
  return "#666666"; // Default grau
}

void updateLEDs() {
  // Setze alle LEDs basierend auf Mode
  for(int i = 0; i < numLEDs; i++) {
    uint32_t color;
    
    // Prüfe ob LED Custom ist (compile-time UND runtime)
    bool isCustomLED0 = false;
    bool isCustomLEDN = false;
    
    #if CUSTOM_LED_0_ENABLED
    isCustomLED0 = (i == 0 && customLED0Enabled);
    #endif
    
    #if CUSTOM_LED_N_ENABLED
    isCustomLEDN = (i == numLEDs - 1 && customLEDNEnabled);
    #endif
    
    // LED 0: Custom oder CheerLights?
    if (isCustomLED0) {
      color = customColorLED0;
    }
    // LED n-1: Custom oder CheerLights?
    else if (isCustomLEDN) {
      color = customColorLEDN;
    }
    // Alle CheerLights LEDs
    else {
      if (displayMode == 0) {
        color = cheerLightsColor;  // Mode 0: Alle gleich
      } else {
        // Mode 1/2: History
        int historyIndex = i;
        
        // Offset für Custom LED 0 wenn aktiviert
        #if CUSTOM_LED_0_ENABLED
        if (customLED0Enabled && i > 0) {
          historyIndex--;
        }
        #endif
        
        if (historyIndex >= 0 && historyIndex < 50) {
          color = colorHistory[historyIndex];
        } else {
          color = cheerLightsColor;
        }
      }
    }
    
    strip->setPixelColor(i, color);
  }
  
  strip->show();
}

// ==================== WEB SERVER ====================

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/status", handleStatus);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='Cache-Control' content='no-cache, no-store, must-revalidate'>";
  html += "<meta http-equiv='Pragma' content='no-cache'>";
  html += "<meta http-equiv='Expires' content='0'>";
  html += "<title>ESP32 LED Strip " + String(VERSION) + "</title>";
  html += "<style>";
  html += "body{font-family:Arial;max-width:600px;margin:50px auto;padding:20px;background:#f0f0f0;}";
  html += "h1{color:#333;}";
  html += "input,button{width:100%;padding:10px;margin:10px 0;font-size:16px;}";
  html += "button{background:#4CAF50;color:white;border:none;cursor:pointer;border-radius:5px;}";
  html += "button:hover{background:#45a049;}";
  html += ".status{background:white;padding:15px;border-radius:5px;margin:20px 0;}";
  html += ".radio-group{background:white;padding:15px;border-radius:5px;margin:10px 0;}";
  html += ".radio-option{margin:10px 0;}";
  html += ".radio-option input{width:auto;margin-right:10px;}";
  html += ".radio-option label{cursor:pointer;font-size:16px;}";
  #if SHOW_COLOR_HISTORY
  html += ".color-history{background:white;padding:10px;border-radius:5px;margin:10px 0;font-size:12px;}";
  html += ".color-chip{display:inline-block;padding:3px 8px;margin:2px;border-radius:3px;color:white;background-color:#666;cursor:pointer;min-width:65px;text-align:center;}";
  #endif
  html += ".checkbox-option{margin:10px 0;} .checkbox-option input{width:auto;margin-right:10px;}";
  html += ".advanced{background:#f9f9f9;padding:15px;border-radius:5px;margin:10px 0;border:1px solid #ddd;}";
  html += ".advanced summary{cursor:pointer;font-weight:bold;margin-bottom:10px;}";
  html += ".input-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
  html += ".input-small{padding:8px;font-size:14px;}";
  html += "label.small{font-size:14px;margin:5px 0;}";
  html += "</style></head><body>";
  html += "<h1>🎨 ESP32 LED Strip " + String(VERSION) + "</h1>";
  html += "<p style='font-size:10px;color:#999;'>Build: " + String(millis()/1000) + "s | Refresh: CTRL+F5</p>";
  html += "<form action='/save' method='POST'>";
  html += "<label>Anzahl LEDs:</label>";
  html += "<input type='number' name='numLEDs' value='" + String(numLEDs) + "' min='2' max='300'>";
  
  #if CUSTOM_LED_0_ENABLED
  html += "<div class='checkbox-option'>";
  html += "<input type='checkbox' id='customLED0En' name='customLED0En' value='1'";
  if (customLED0Enabled) html += " checked";
  html += "><label for='customLED0En'>🎨 Custom LED 0 aktivieren</label>";
  html += "</div>";
  html += "<label>Custom LED 0 URL:</label>";
  html += "<input type='text' name='colorURL0' value='" + customColorURL_LED0 + "' placeholder='https://example.com/led0.txt'>";
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  html += "<div class='checkbox-option'>";
  html += "<input type='checkbox' id='customLEDNEn' name='customLEDNEn' value='1'";
  if (customLEDNEnabled) html += " checked";
  html += "><label for='customLEDNEn'>🎨 Custom LED " + String(numLEDs-1) + " aktivieren</label>";
  html += "</div>";
  html += "<label>Custom LED " + String(numLEDs-1) + " URL:</label>";
  html += "<input type='text' name='colorURLN' value='" + customColorURL_LEDN + "' placeholder='https://example.com/ledn.txt'>";
  #endif
  
  // LDR An/Aus mit aktuellem Wert
  html += "<div class='checkbox-option'>";
  html += "<input type='checkbox' id='ldrEnabled' name='ldrEnabled' value='1'";
  if (ldrEnabled) html += " checked";
  html += "><label for='ldrEnabled'>Auto-Helligkeit <span id='livePercent'>";
  if (ldrEnabled) {
    int brightnessPercent = (currentBrightness * 100) / 255;
    html += String(brightnessPercent) + "%";
  }
  html += "</span> <span id='liveLDR'>(LDR " + String(currentLDRValue) + ")</span>";
  html += "</label>";
  html += "</div>";
  
  // Advanced LDR Settings (collapsible)
  html += "<details class='advanced'>";
  html += "<summary>⚙️ LDR-Einstellungen (Erweitert)</summary>";
  html += "<label class='small'>Helligkeit Min (Nachts, 0-255):</label>";
  html += "<input class='input-small' type='number' name='brightMin' value='" + String(brightnessMin) + "' min='1' max='255'>";
  html += "<label class='small'>Helligkeit Max (Tags, 0-255):</label>";
  html += "<input class='input-small' type='number' name='brightMax' value='" + String(brightnessMax) + "' min='1' max='255'>";
  html += "<div class='input-row'>";
  html += "<div><label class='small'>LDR Dunkel-Schwelle:</label>";
  html += "<input class='input-small' type='number' name='ldrDark' value='" + String(ldrDarkThreshold) + "' min='0' max='4095'></div>";
  html += "<div><label class='small'>LDR Hell-Schwelle:</label>";
  html += "<input class='input-small' type='number' name='ldrBright' value='" + String(ldrBrightThreshold) + "' min='0' max='4095'></div>";
  html += "</div>";
  html += "<p style='font-size:12px;color:#666;margin:10px 0;'>💡 Aktueller LDR-Wert: <strong>" + String(currentLDRValue) + "</strong> - Nutze diesen Wert zur Kalibrierung!<br>";
  html += "ℹ️ Helligkeit in % = (Wert × 100) ÷ 255</p>";
  html += "</details>";
  
  // Radio Buttons für Display Mode
  html += "<div class='radio-group'>";
  html += "<label><strong>Display Mode:</strong></label>";
  html += "<div class='radio-option'>";
  html += "<input type='radio' id='mode0' name='displayMode' value='0'";
  if (displayMode == 0) html += " checked";
  html += "><label for='mode0'>Mode 0: Alle LEDs gleiche Farbe</label>";
  html += "</div>";
  html += "<div class='radio-option'>";
  html += "<input type='radio' id='mode1' name='displayMode' value='1'";
  if (displayMode == 1) html += " checked";
  html += "><label for='mode1'>Mode 1: Shift-along (Historie)</label>";
  html += "</div>";
  html += "<div class='radio-option'>";
  html += "<input type='radio' id='mode2' name='displayMode' value='2'";
  if (displayMode == 2) html += " checked";
  html += "><label for='mode2'>Mode 2: Shift + Auto-Duplicate (15 Min)</label>";
  html += "</div>";
  html += "</div>";
  
  html += "<button type='submit'>💾 Speichern & Neustarten</button>";
  html += "</form>";
  
  html += "<div class='status'>";
  html += "<h3>Status</h3>";
  html += "<p>WiFi: " + WiFi.SSID() + "</p>";
  html += "<p>IP: " + WiFi.localIP().toString() + "</p>";
  html += "<p>LEDs: " + String(numLEDs) + "</p>";
  
  // Custom LEDs Info
  #if CUSTOM_LED_0_ENABLED
  html += "<p>🎨 LED 0: Custom ";
  if (customLED0Enabled) {
    html += "(✓ aktiv)";
  } else {
    html += "(○ CheerLights)";
  }
  html += "</p>";
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  html += "<p>🎨 LED " + String(numLEDs-1) + ": Custom ";
  if (customLEDNEnabled) {
    html += "(✓ aktiv)";
  } else {
    html += "(○ CheerLights)";
  }
  html += "</p>";
  #endif
  
  #if !CUSTOM_LED_0_ENABLED && !CUSTOM_LED_N_ENABLED
  html += "<p>🌈 Alle LEDs: CheerLights</p>";
  #endif
  
  html += "<p>Display Mode: " + String(displayMode);
  if (displayMode == 0) html += " (All same)";
  else if (displayMode == 1) html += " (Shift-along)";
  else if (displayMode == 2) html += " (Shift + auto-dup)";
  html += "</p>";
  
  // LDR und Helligkeit
  if (ldrEnabled) {
    int brightnessPercent = (currentBrightness * 100) / 255; // % von 255 (absolute max)
    html += "<p>💡 Helligkeit: <span id='statusPercent'>" + String(brightnessPercent) + "%</span> (<span id='statusBright'>" + String(currentBrightness) + "</span>/255)</p>";
    html += "<p>📊 LDR-Wert: <span id='statusLDR'>" + String(currentLDRValue) + "</span> (0-4095)</p>";
  } else {
    html += "<p>💡 Helligkeit: Fest " + String((currentBrightness * 100) / 255) + "% (" + String(currentBrightness) + "/255) - LDR aus</p>";
  }
  
  html += "</div>";
  html += "<p><small>Tipp: Button 1 (GPIO 4) 3s drücken für WiFi-Reset<br>";
  html += "Button 2 (GPIO 2) kurz drücken für Mode-Wechsel<br>";
  html += "🌈 CheerLights Farben: <a href='https://cheerlights.com' target='_blank' style='color:#4CAF50;'>cheerlights.com</a></small></p>";
  
  // JavaScript für Live-Updates
  html += "<script>";
  html += "function updateLiveValues(){";
  html += "fetch('/status').then(r=>r.json()).then(d=>{";
  html += "const ldr=d.ldrValue||0;";
  html += "const bright=d.currentBrightness||0;";
  html += "const pct=Math.round((bright*100)/255);";
  html += "const lp=document.getElementById('livePercent');";
  html += "const ll=document.getElementById('liveLDR');";
  html += "const sp=document.getElementById('statusPercent');";
  html += "const sb=document.getElementById('statusBright');";
  html += "const sl=document.getElementById('statusLDR');";
  html += "if(lp)lp.textContent=pct+'%';";
  html += "if(ll)ll.textContent='(LDR '+ldr+')';";
  html += "if(sp)sp.textContent=pct+'%';";
  html += "if(sb)sb.textContent=bright;";
  html += "if(sl)sl.textContent=ldr;";
  html += "}).catch(e=>console.log('Update failed',e));";
  html += "}";
  html += "setInterval(updateLiveValues,2000);"; // Alle 2 Sekunden
  html += "</script>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("numLEDs")) {
    numLEDs = server.arg("numLEDs").toInt();
    if (numLEDs < 2) numLEDs = 2;
    if (numLEDs > 300) numLEDs = 300;
  }
  
  #if CUSTOM_LED_0_ENABLED
  customLED0Enabled = server.hasArg("customLED0En");  // Checkbox
  if (server.hasArg("colorURL0")) {
    customColorURL_LED0 = server.arg("colorURL0");  // URL (bleibt gespeichert!)
  }
  #endif
  
  #if CUSTOM_LED_N_ENABLED
  customLEDNEnabled = server.hasArg("customLEDNEn");  // Checkbox
  if (server.hasArg("colorURLN")) {
    customColorURL_LEDN = server.arg("colorURLN");  // URL (bleibt gespeichert!)
  }
  #endif
  
  if (server.hasArg("displayMode")) {
    displayMode = server.arg("displayMode").toInt();
    if (displayMode < 0) displayMode = 0;
    if (displayMode > 2) displayMode = 2;
    Serial.printf("Display Mode changed via Web: %d\n", displayMode);
  }
  
  // LDR an/aus (Checkbox)
  ldrEnabled = server.hasArg("ldrEnabled");
  Serial.printf("LDR Enabled: %s\n", ldrEnabled ? "Yes" : "No");
  
  // LDR-Konfiguration
  if (server.hasArg("brightMin")) {
    brightnessMin = server.arg("brightMin").toInt();
    if (brightnessMin < 1) brightnessMin = 1;
    if (brightnessMin > 255) brightnessMin = 255;
  }
  if (server.hasArg("brightMax")) {
    brightnessMax = server.arg("brightMax").toInt();
    if (brightnessMax < 1) brightnessMax = 1;
    if (brightnessMax > 255) brightnessMax = 255;
  }
  if (server.hasArg("ldrDark")) {
    ldrDarkThreshold = server.arg("ldrDark").toInt();
    if (ldrDarkThreshold < 0) ldrDarkThreshold = 0;
    if (ldrDarkThreshold > 4095) ldrDarkThreshold = 4095;
  }
  if (server.hasArg("ldrBright")) {
    ldrBrightThreshold = server.arg("ldrBright").toInt();
    if (ldrBrightThreshold < 0) ldrBrightThreshold = 0;
    if (ldrBrightThreshold > 4095) ldrBrightThreshold = 4095;
  }
  
  Serial.printf("LDR Config: Min=%d, Max=%d, Dark=%d, Bright=%d\n", 
                brightnessMin, brightnessMax, ldrDarkThreshold, ldrBrightThreshold);
  
  // Speichern
  preferences.begin("ledstrip", false);
  preferences.putInt("numLEDs", numLEDs);
  #if CUSTOM_LED_0_ENABLED
  preferences.putString("colorURL0", customColorURL_LED0);
  preferences.putBool("customLED0En", customLED0Enabled);
  #endif
  #if CUSTOM_LED_N_ENABLED
  preferences.putString("colorURLN", customColorURL_LEDN);
  preferences.putBool("customLEDNEn", customLEDNEnabled);
  #endif
  preferences.putInt("displayMode", displayMode);
  preferences.putBool("ldrEnabled", ldrEnabled);
  preferences.putInt("brightMin", brightnessMin);
  preferences.putInt("brightMax", brightnessMax);
  preferences.putInt("ldrDark", ldrDarkThreshold);
  preferences.putInt("ldrBright", ldrBrightThreshold);
  preferences.end();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='3;url=/'>";
  html += "<style>body{font-family:Arial;text-align:center;padding:50px;}</style>";
  html += "</head><body>";
  html += "<h1>✅ Gespeichert!</h1>";
  html += "<p>ESP32 wird neu gestartet...</p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  
  delay(1000);
  ESP.restart();
}

void handleStatus() {
  String json = "{";
  json += "\"numLEDs\":" + String(numLEDs) + ",";
  json += "\"displayMode\":" + String(displayMode) + ",";
  json += "\"cheerLightsColor\":\"" + String(cheerLightsColor, HEX) + "\",";
  #if CUSTOM_LED_0_ENABLED
  json += "\"customColorLED0\":\"" + String(customColorLED0, HEX) + "\",";
  #endif
  #if CUSTOM_LED_N_ENABLED
  json += "\"customColorLEDN\":\"" + String(customColorLEDN, HEX) + "\",";
  #endif
  json += "\"ldrValue\":" + String(currentLDRValue) + ",";
  json += "\"currentBrightness\":" + String(currentBrightness) + ",";
  json += "\"ldrEnabled\":" + String(ldrEnabled ? "true" : "false") + ",";
  json += "\"ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

// ==========================================
// EyeSwap Firmware — ESP32-S3
// Personal Media & Navigation Controller
// Copyright © 2026 Shawn Baird
// ==========================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <BH1750.h>

// ==== Pin Defines (Sanwa OBSF-24, 3x) ====
#define BTN_1_PIN     3
#define BTN_2_PIN     4
#define BTN_3_PIN     5

// ==== Debounce & Timing ====
#define DEBOUNCE_MS        20
#define LONG_PRESS_MS      600
#define DOUBLE_CLICK_MS    250
#define BTN_IDLE_MS        50

// ==== WiFi AP Config ====
const char* AP_SSID     = "EyeSwap-Setup";
const char* AP_PASS     = "eyeswap2026";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

// ==== UDP Config ====
WiFiUDP Udp;
const int UDP_PORT             = 4210;
const char* PHONE_IP_DEFAULT   = "192.168.4.2";   // Android usually .2
const int PHONE_UDP_PORT       = 4211;             // Listen on app side if needed

// ==== Light Sensor ====
BH1750 lightMeter(0x23);  // ADDR to GND

// ==== Display ====
TFT_eSPI tft = TFT_eSPI();

// ==== Brightness / Color State ====
int brightnessOffset = 0;   // -50 to +50 from app
int baseBrightness   = 128; // 0-255, set by BH1750
uint16_t bgColor565  = 0x0000; // Black default
bool flashRequested  = false;

// ==== Button State ====
struct Button {
  uint8_t pin;
  bool pressed;
  bool longPressSent;
  unsigned long pressTime;
  unsigned long lastRelease;
  int clickCount;
};
Button btns[3] = {
  {BTN_1_PIN, false, false, 0, 0, 0},
  {BTN_2_PIN, false, false, 0, 0, 0},
  {BTN_3_PIN, false, false, 0, 0, 0}
};

// ==== Forward Declarations ====
void setupWiFiAP();
void setupDisplay();
void setupButtons();
void setupLightSensor();
void handleUDP();
void handleBrightness();
void handleButtons();
void sendButtonEvent(int btnNum, const char* action);
void drawIdle();
void applyBrightness();

// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== EyeSwap v1.0 ===");

  setupDisplay();
  setupButtons();
  setupLightSensor();
  setupWiFiAP();

  Udp.begin(UDP_PORT);
  Serial.printf("UDP listening on %s:%d\n", AP_IP.toString().c_str(), UDP_PORT);

  drawIdle();
}

// ==========================================
void loop() {
  handleUDP();
  handleBrightness();
  handleButtons();
  delay(2);  // ~500Hz loop, debounce friendly
}

// ==========================================
void setupWiFiAP() {
  Serial.println("Starting WiFi AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  WiFi.softAP(AP_SSID, AP_PASS, 6, 0, 4);  // ch6, no hide, max4 clients
  Serial.printf("AP: %s / IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

// ==========================================
void setupDisplay() {
  // Setup LEDC for backlight on pin 7 before TFT init
  ledcAttachPin(7, 0);
  ledcSetup(0, 5000, 8);
  ledcWrite(0, 128);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  Serial.println("GC9A01 init OK");
}

// ==========================================
void setupButtons() {
  for (int i=0; i<3; i++) {
    pinMode(btns[i].pin, INPUT_PULLUP);
  }
  Serial.println("Buttons init OK");
}

// ==========================================
void setupLightSensor() {
  Wire.begin();
  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 init OK");
  } else {
    Serial.println("BH1750 NOT FOUND");
  }
}

// ==========================================
void handleUDP() {
  int packetSize = Udp.parsePacket();
  if (!packetSize) return;

  char buf[512];
  int len = Udp.read(buf, sizeof(buf)-1);
  if (len <= 0) return;
  buf[len] = '\0';

  Serial.printf("[UDP] %s\n", buf);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    Serial.printf("JSON err: %s\n", err.c_str());
    return;
  }

  const char* type = doc["type"] | "unknown";

  if (strcmp(type, "config") == 0) {
    // Brightness offset
    if (doc.containsKey("brightness_offset")) {
      brightnessOffset = constrain(doc["brightness_offset"].as<int>(), -50, 50);
      Serial.printf("Brightness offset: %d\n", brightnessOffset);
    }
    // Background color
    if (doc.containsKey("bg_color")) {
      const char* hexC = doc["bg_color"];
      if (hexC && strlen(hexC) >= 7) {
        long rgb = strtol(hexC+1, NULL, 16);
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8)  & 0xFF;
        uint8_t b = rgb & 0xFF;
        bgColor565 = tft.color565(r, g, b);
        Serial.printf("BG color: %s -> 0x%04X\n", hexC, bgColor565);
      }
    }
    flashRequested = true;
    drawIdle();
  }
}

// ==========================================
void handleBrightness() {
  static unsigned long lastLuxCheck = 0;
  if (millis() - lastLuxCheck < 500) return;  // 2Hz max
  lastLuxCheck = millis();

  float lux = lightMeter.readLightLevel();
  // Map lux roughly to 20-255
  int target = map(constrain((int)lux, 0, 500), 0, 500, 20, 255);
  baseBrightness = target;

  applyBrightness();
}

// ==========================================
void applyBrightness() {
  int finalBright = constrain(baseBrightness + brightnessOffset, 5, 255);
  ledcWrite(0, finalBright);
}

// ==========================================
void handleButtons() {
  unsigned long now = millis();

  for (int i=0; i<3; i++) {
    bool raw = (digitalRead(btns[i].pin) == LOW);  // Active low with pullup
    Button &b = btns[i];

    if (!b.pressed && raw) {
      // Press start
      b.pressed = true;
      b.longPressSent = false;
      b.pressTime = now;
    }
    else if (b.pressed && !raw) {
      // Release
      if (!b.longPressSent) {
        // Short press
        sendButtonEvent(i+1, "tap");
      }
      b.pressed = false;
      b.lastRelease = now;
    }
    else if (b.pressed && raw && !b.longPressSent) {
      if (now - b.pressTime >= LONG_PRESS_MS) {
        b.longPressSent = true;
        sendButtonEvent(i+1, "long");
      }
    }
  }
}

// ==========================================
void sendButtonEvent(int btnNum, const char* action) {
  JsonDocument doc;
  doc["type"] = "button";
  doc["button"] = btnNum;
  doc["action"] = action;

  char out[256];
  size_t len = serializeJson(doc, out);

  Udp.beginPacket(PHONE_IP_DEFAULT, PHONE_UDP_PORT);
  Udp.write((const uint8_t*)out, len);
  Udp.endPacket();

  Serial.printf("[BTN] %d %s\n", btnNum, action);
}

// ==========================================
void drawIdle() {
  tft.fillScreen(bgColor565);
  tft.setTextColor(TFT_WHITE, bgColor565);
  tft.setTextSize(2);
  tft.drawString("EYESWAP", 120, 100, 2);
  tft.setTextSize(1);
  tft.drawString("READY", 120, 130, 2);

  if (flashRequested) {
    // Brief flash feedback
    tft.fillCircle(120, 170, 10, TFT_RED);
    delay(100);
    tft.fillCircle(120, 170, 10, bgColor565);
    flashRequested = false;
  }
}

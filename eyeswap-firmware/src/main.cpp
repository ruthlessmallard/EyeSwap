// ==========================================
// EyeSwap Firmware — ESP32-S3
// BLE Peripheral for Personal Media Controller
// Copyright © 2026 Shawn Baird
// ==========================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

// ==== BLE UUIDs (must match Flutter app) ====
#define EYESWAP_SERVICE_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHAR_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BUTTON_CHAR_UUID        "a1e60244-960d-4d06-aca2-a2fc604f09be"

// ==== Pin Defines (Sanwa OBSF-24, 3x) ====
#define BTN_1_PIN     3
#define BTN_2_PIN     4
#define BTN_3_PIN     5

// ==== Debounce & Timing ====
#define DEBOUNCE_MS        20
#define LONG_PRESS_MS      600
#define BTN_IDLE_MS        50

// ==== Display ====
TFT_eSPI tft = TFT_eSPI();

// ==== Brightness / Color State ====
int brightnessOffset = 0;   // -50 to +50 from app
uint16_t bgColor565  = 0x0000; // Black default
bool flashRequested  = false;

// ==== Button State ====
struct Button {
  uint8_t pin;
  bool pressed;
  bool longPressSent;
  unsigned long pressTime;
};
Button btns[3] = {
  {BTN_1_PIN, false, false, 0},
  {BTN_2_PIN, false, false, 0},
  {BTN_3_PIN, false, false, 0}
};

// ==== BLE Globals ====
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pConfigChar = nullptr;
NimBLECharacteristic* pButtonChar = nullptr;
bool deviceConnected = false;

// ==== Forward Declarations ====
void setupDisplay();
void setupButtons();
void setupBLE();
void handleButtons();
void sendButtonEvent(int btnNum, const char* action);
void drawIdle();

// ==========================================
// BLE Server Callbacks
// ==========================================
class EyeSwapServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] Client connected");
    drawIdle();
  }
  void onDisconnect(NimBLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Client disconnected");
    NimBLEDevice::startAdvertising();
    drawIdle();
  }
};

// ==========================================
// Config Characteristic Callbacks
// ==========================================
class ConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (value.empty()) return;

    Serial.printf("[BLE] Config recv: %s\n", value.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err) {
      Serial.printf("JSON err: %s\n", err.c_str());
      return;
    }

    const char* type = doc["type"] | "unknown";
    if (strcmp(type, "config") != 0) return;

    if (doc.containsKey("brightness_offset")) {
      brightnessOffset = constrain(doc["brightness_offset"].as<int>(), -50, 50);
      Serial.printf("Brightness offset: %d\n", brightnessOffset);
    }
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
};

// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== EyeSwap BLE v1.0 ===");

  setupDisplay();
  setupButtons();
  setupBLE();

  drawIdle();
}

// ==========================================
void loop() {
  handleButtons();
  delay(2);  // ~500Hz loop, debounce friendly
}

// ==========================================
void setupDisplay() {
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
void setupBLE() {
  NimBLEDevice::init("EyeSwap");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // +9dBm max range

  pServer = NimBLEServer::createServer();
  pServer->setCallbacks(new EyeSwapServerCallbacks());

  NimBLEService* pService = pServer->createService(EYESWAP_SERVICE_UUID);

  // Config characteristic: app WRITES to this
  pConfigChar = pService->createCharacteristic(
    CONFIG_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pConfigChar->setCallbacks(new ConfigCallbacks());

  // Button characteristic: ESP32 NOTIFIES app
  pButtonChar = pService->createCharacteristic(
    BUTTON_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(EYESWAP_SERVICE_UUID);
  pAdvertising->setName("EyeSwap");
  pAdvertising->start();

  Serial.println("BLE advertising as 'EyeSwap'");
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
        sendButtonEvent(i+1, "tap");
      }
      b.pressed = false;
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

  if (deviceConnected && pButtonChar) {
    pButtonChar->setValue((uint8_t*)out, len);
    pButtonChar->notify();
    Serial.printf("[BLE] BTN %d %s\n", btnNum, action);
  } else {
    Serial.printf("[BTN] %d %s (no client)\n", btnNum, action);
  }
}

// ==========================================
void drawIdle() {
  tft.fillScreen(bgColor565);
  tft.setTextColor(TFT_WHITE, bgColor565);

  tft.setTextSize(2);
  tft.drawString("EYESWAP", 120, 100, 2);

  tft.setTextSize(1);
  if (deviceConnected) {
    tft.setTextColor(TFT_GREEN, bgColor565);
    tft.drawString("CONNECTED", 120, 130, 2);
  } else {
    tft.setTextColor(TFT_RED, bgColor565);
    tft.drawString("WAITING", 120, 130, 2);
  }

  if (flashRequested) {
    tft.fillCircle(120, 170, 10, TFT_RED);
    delay(100);
    tft.fillCircle(120, 170, 10, bgColor565);
    flashRequested = false;
  }
}

// ==========================================
// EyeSwap Firmware — ESP32-S3 (MINIMAL - NO DISPLAY)
// BLE Peripheral for Personal Media Controller
// Copyright © 2026 Shawn Baird
// ==========================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ==== BLE UUIDs (MUST match Flutter app exactly) ====
#define EYESWAP_SERVICE_UUID    "12345678-1234-1234-1234-123456789abc"
#define COMMAND_CHAR_UUID       "87654321-4321-4321-4321-cba987654321"

// ==== Pin Defines (Sanwa OBSF-24, 3x) ====
#define BTN_1_PIN     3
#define BTN_2_PIN     4
#define BTN_3_PIN     5

// ==== Timing ====
#define LONG_PRESS_MS      600

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
BLEServer* pServer = nullptr;
BLECharacteristic* pCommandChar = nullptr;
bool deviceConnected = false;

// ==========================================
class EyeSwapServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("[BLE] Client connected");
  }
  
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("[BLE] Client disconnected - restarting advertising");
    BLEDevice::startAdvertising();
  }
};

// ==========================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== EyeSwap BLE Firmware v1.4 (NO DISPLAY) ===");

  setupButtons();
  setupBLE();

  Serial.println("Setup complete - waiting for BLE connection...");
}

// ==========================================
void loop() {
  handleButtons();
  delay(2);
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
  BLEDevice::init("EyeSwap-ESP32");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new EyeSwapServerCallbacks());

  BLEService* pService = pServer->createService(EYESWAP_SERVICE_UUID);

  pCommandChar = pService->createCharacteristic(
    COMMAND_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  pCommandChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(EYESWAP_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as 'EyeSwap-ESP32'");
}

// ==========================================
void handleButtons() {
  unsigned long now = millis();

  for (int i=0; i<3; i++) {
    bool raw = (digitalRead(btns[i].pin) == LOW);
    Button &b = btns[i];

    if (!b.pressed && raw) {
      b.pressed = true;
      b.longPressSent = false;
      b.pressTime = now;
    }
    else if (b.pressed && !raw) {
      if (!b.longPressSent) {
        sendButtonCommand(i+1, false);
      }
      b.pressed = false;
    }
    else if (b.pressed && raw && !b.longPressSent) {
      if (now - b.pressTime >= LONG_PRESS_MS) {
        b.longPressSent = true;
        sendButtonCommand(i+1, true);
      }
    }
  }
}

// ==========================================
void sendButtonCommand(int btnNum, bool longPress) {
  if (!deviceConnected || !pCommandChar) {
    Serial.printf("[BTN] %d %s (no client)\n", btnNum, longPress ? "LONG" : "TAP");
    return;
  }

  String command;
  if (longPress) {
    command = "BTN" + String(btnNum) + "_LONG";
  } else {
    command = "BTN" + String(btnNum);
  }

  pCommandChar->setValue(command.c_str());
  pCommandChar->notify();
  
  Serial.printf("[BLE] Sent: %s\n", command.c_str());
}
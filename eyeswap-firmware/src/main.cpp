// ==========================================
// EyeSwap Firmware — ESP32-S3 (TFT BASIC)
// BLE Peripheral for Personal Media Controller
// Copyright © 2026 Shawn Baird
// ==========================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <TFT_eSPI.h>
#include "esp_system.h"
#include <Preferences.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <string.h>

// ==== BLE UUIDs (MUST match Flutter app exactly) ====
#define EYESWAP_SERVICE_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CONFIG_CHAR_UUID        "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BUTTON_CHAR_UUID        "a1e60244-960d-4d06-aca2-a2fc604f09be"

// ==== Pin Defines (Sanwa OBSF-24, 3x) ====
// FIXED: Avoided pin 3 (reset/boot pin)
#define BTN_1_PIN     4   // YouTube Music
#define BTN_2_PIN     5   // Audible
#define BTN_3_PIN     6   // Call/Gemini

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
BLECharacteristic* pConfigChar = nullptr;
BLECharacteristic* pButtonChar = nullptr;
bool deviceConnected = false;

// ==== TFT Globals ====
TFT_eSPI tft = TFT_eSPI();
String marqueeText = "EYESWAP";
int marqueePos = 0;
unsigned long lastMarqueeUpdate = 0;
#define MARQUEE_MS 150
#define TICKER_ROWS 1
#define TICKER_ROW_SPACING 30
// Amber text (~#FFBF00) in RGB565 for the round display.
#define TICKER_COLOR 0xFDE0

// ==== Display mode + config (set over BLE) ====
enum DisplayMode { MODE_MARQUEE, MODE_STATIC, MODE_NOW_PLAYING };
DisplayMode displayMode = MODE_MARQUEE;
int brightnessOffset = 0;   // -50..+50, applied as a text brightness scale
uint8_t baseR = 0xFF, baseG = 0xBF, baseB = 0x00;  // Amber default
uint16_t drawColor = TICKER_COLOR;

// Now-playing metadata (pushed from the app, slice 2)
String npTitle = "";
String npArtist = "";
bool npPlaying = false;

// Now-playing scroll state: long titles/artists marquee inside their band.
bool npTitleScroll = false;
bool npArtistScroll = false;
int npTitleX = 0;
int npArtistX = 0;
int npTitleWidth = 0;
int npArtistWidth = 0;

// Game of Life (MODE_STATIC): amber cells on black, torus wrap, reseeds when
// the soup dies out or freezes into a still life.
#define LIFE_CELL 2                     // px per cell: 2 = 50% zoom-in, 120x120 field
#define LIFE_W (240 / LIFE_CELL)
#define LIFE_H (240 / LIFE_CELL)
#define LIFE_BYTES ((LIFE_W * LIFE_H + 7) / 8)
#define LIFE_MIN_POP_PCT 2              // reseed when population < this % of the field
static uint8_t lifeBufA[LIFE_BYTES];
static uint8_t lifeBufB[LIFE_BYTES];
static uint8_t* lifeCur = lifeBufA;
static uint8_t* lifeNext = lifeBufB;
static TFT_eSprite lifeSpr = TFT_eSprite(&tft);
static bool lifeSpriteReady = false;
static unsigned long lastLifeUpdate = 0;

// BLE-callback handoff: onWrite only records state; the loop task does ALL
// TFT/SPI work. Two tasks driving one SPI bus corrupts the transaction
// mutex and panics (assert in xQueueGenericSend / spiEndTransaction).
volatile DisplayMode pendingMode = MODE_MARQUEE;
volatile bool modeDirty = false;
volatile bool configDirty = false;
volatile bool npDirty = false;

// Display settings persist across power cycles (NVS).
Preferences prefs;

// ==== Forward Declarations ====
void setupTFT();
void setupButtons();
void setupBLE();
void handleButtons();
void updateMarquee();
void lifeSeed();
void lifeRepaint();
void updateLife();
void renderDisplay();
void setDisplayMode(DisplayMode mode);
void applyDisplayColor();
void saveDisplaySettings();
void drawNowPlaying();
void updateNowPlaying();
void sendButtonCommand(int btnNum, bool longPress);

// ==== Config Callbacks (handle incoming JSON from app) ====
class ConfigCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.empty()) return;

    Serial.printf("[BLE] Config received: %s\n", value.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value.c_str());
    if (err) {
      Serial.printf("[BLE] JSON parse failed: %s\n", err.c_str());
      return;
    }

    const char* type = doc["type"] | "";
    if (strcmp(type, "mode") == 0) {
      const char* anim = doc["animation"] | "";
      // No TFT work here -- just record it; the loop task applies it.
      if (strcmp(anim, "marquee") == 0)          { pendingMode = MODE_MARQUEE;     modeDirty = true; }
      else if (strcmp(anim, "static") == 0)      { pendingMode = MODE_STATIC;      modeDirty = true; }
      else if (strcmp(anim, "now_playing") == 0) { pendingMode = MODE_NOW_PLAYING; modeDirty = true; }
      else Serial.printf("[BLE] Unknown animation '%s'\n", anim);
    }
    else if (strcmp(type, "config") == 0) {
      brightnessOffset = doc["brightness_offset"] | 0;
      if (brightnessOffset < -50) brightnessOffset = -50;
      if (brightnessOffset > 50) brightnessOffset = 50;

      const char* hex = doc["bg_color"] | "";
      if (hex[0] == '#' && strlen(hex) == 7) {
        long rgb = strtol(hex + 1, nullptr, 16);
        baseR = (rgb >> 16) & 0xFF;
        baseG = (rgb >> 8) & 0xFF;
        baseB = rgb & 0xFF;
      }
      configDirty = true;  // deferred: loop task calls applyDisplayColor()
      Serial.printf("[BLE] Config applied: brightness %d, color #%02X%02X%02X\n",
                    brightnessOffset, baseR, baseG, baseB);
    }
    else if (strcmp(type, "now_playing") == 0) {
      npTitle = (const char*)(doc["title"] | "");
      npArtist = (const char*)(doc["artist"] | "");
      const char* state = doc["state"] | "";
      npPlaying = (strcmp(state, "playing") == 0);
      Serial.printf("[BLE] Now playing: '%s' / '%s' (%s)\n",
                    npTitle.c_str(), npArtist.c_str(), state);
      npDirty = true;  // deferred: loop task repaints
    }
    else {
      Serial.printf("[BLE] Unknown config type '%s'\n", type);
    }
  }
};

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
  // N16R8: Native USB-CDC, wait for serial connection
  Serial.begin(115200);
  while (!Serial) { ; }
  delay(100);
  Serial.println("\n\n=== EyeSwap BLE Firmware v1.7 (TFT Basic) ===");

  setupTFT();
  setupButtons();

  // Restore last display settings from NVS so the pod survives power cycles.
  prefs.begin("eyeswap", false);
  {
    DisplayMode savedMode =
        (DisplayMode)prefs.getUChar("mode", (uint8_t)MODE_MARQUEE);
    if (savedMode > MODE_NOW_PLAYING) savedMode = MODE_MARQUEE;
    displayMode = savedMode;
    brightnessOffset = prefs.getInt("bright", 0);
    baseR = prefs.getUChar("cr", 0xFF);
    baseG = prefs.getUChar("cg", 0xBF);
    baseB = prefs.getUChar("cb", 0x00);
  }
  applyDisplayColor();
  setDisplayMode(displayMode);

  setupBLE();

  Serial.println("Setup complete - waiting for BLE connection...");
}

// ==========================================
void loop() {
  handleButtons();
  if (modeDirty) { modeDirty = false; setDisplayMode(pendingMode); }
  if (configDirty) {
    configDirty = false;
    applyDisplayColor();
    saveDisplaySettings();
    if (displayMode == MODE_NOW_PLAYING) drawNowPlaying();
    else if (displayMode == MODE_STATIC) lifeRepaint();
  }
  if (npDirty) {
    npDirty = false;
    if (displayMode == MODE_NOW_PLAYING) drawNowPlaying();
  }
  renderDisplay();
  delay(2);
}

// ==========================================
void setupTFT() {
  Serial.println("TFT init starting...");
  
  // Explicit SPI bus initialization for ESP32-S3
  Serial.println("Initializing SPI bus...");
  SPI.begin(14, -1, 13, 10);  // SCK=14, MISO=-1, MOSI=13, SS=10
  SPI.setFrequency(40000000);  // 40MHz
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  
  Serial.println("SPI bus initialized, starting TFT...");
  
  // Initialize TFT
  tft.init();
  tft.setRotation(0);  // Round display, adjust if needed
  tft.fillScreen(TFT_BLACK);
  
  // Original small built-in font/scale from the first working display build.
  tft.setTextColor(TICKER_COLOR);
  tft.setTextFont(1);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  Serial.printf("EYESWAP ticker label width: %d px\n",
                tft.textWidth(marqueeText));

  // Game of Life sprite: 1bpp full-frame push keeps animation flicker-free.
  lifeSpr.setColorDepth(1);
  lifeSpr.createSprite(LIFE_W * LIFE_CELL, LIFE_H * LIFE_CELL);
  lifeSpr.setBitmapColor(drawColor, TFT_BLACK);
  lifeSpriteReady = true;
  
  // First ticker frame is rendered by updateMarquee().
  
  Serial.println("TFT init OK (240x240 GC9A01)");
}

// ==========================================
void updateMarquee() {
  unsigned long now = millis();
  if (now - lastMarqueeUpdate < MARQUEE_MS) return;
  lastMarqueeUpdate = now;

  // Repeating one-way ticker in several stacked rows. Every row marches right
  // and wraps seamlessly, and each row is phase-shifted by half a label
  // period so the letters brick-offset instead of forming columns.
  const int labelWidth = tft.textWidth(marqueeText);
  const int spacing = 30;
  const int repeatPeriod = labelWidth + spacing;
  const int charHeight = tft.fontHeight();
  const int totalHeight = (TICKER_ROWS - 1) * TICKER_ROW_SPACING + charHeight;
  const int topY = (tft.height() - totalHeight) / 2;
  tft.fillRect(0, topY, tft.width(), totalHeight, TFT_BLACK);

  for (int row = 0; row < TICKER_ROWS; row++) {
    const int y = topY + row * TICKER_ROW_SPACING;
    const int phase = (marqueePos + (row * repeatPeriod) / 2) % repeatPeriod;
    for (int x = phase - repeatPeriod; x < tft.width(); x += repeatPeriod) {
      tft.drawString(marqueeText, x, y);
    }
  }

  marqueePos += 2;
  if (marqueePos >= repeatPeriod) marqueePos -= repeatPeriod;
}

// ==========================================
void applyDisplayColor() {
  // No backlight pin on this module, so approximate the brightness offset by
  // scaling the text color (offset -50..+50 -> 50%..150%, clamped to 255).
  const float scale = 1.0f + (brightnessOffset / 100.0f);
  int r = (int)(baseR * scale); if (r > 255) r = 255; if (r < 0) r = 0;
  int g = (int)(baseG * scale); if (g > 255) g = 255; if (g < 0) g = 0;
  int b = (int)(baseB * scale); if (b > 255) b = 255; if (b < 0) b = 0;
  drawColor = tft.color565(r, g, b);
  tft.setTextColor(drawColor);
  if (lifeSpriteReady) lifeSpr.setBitmapColor(drawColor, TFT_BLACK);
  Serial.printf("[TFT] draw color 0x%04X\n", drawColor);
}

void drawNowPlaying() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(drawColor);
  tft.setTextFont(1);
  tft.setTextDatum(MC_DATUM);

  // Playback state line
  tft.setTextSize(2);
  tft.drawString(npPlaying ? "PLAYING" : "PAUSED", tft.width() / 2, 66);

  // Title: use the largest size that fits; scroll at size 2 if none does.
  String title = (npTitle.length() > 0) ? npTitle : "No media";
  int titleSize = 3;
  while (titleSize > 2) {
    tft.setTextSize(titleSize);
    if (tft.textWidth(title) <= tft.width() - 28) break;
    titleSize--;
  }
  tft.setTextSize(2);
  npTitleWidth = tft.textWidth(title);
  npTitleScroll = (npTitleWidth > tft.width() - 28);
  if (npTitleScroll) {
    npTitleX = tft.width();  // enter from the right edge
  } else {
    tft.setTextSize(titleSize);
    npTitleWidth = tft.textWidth(title);
    tft.drawString(title, tft.width() / 2, 112);
  }

  // Artist line: centered if it fits, otherwise it scrolls too.
  String artist = (npArtist.length() > 0) ? npArtist : " ";
  tft.setTextSize(2);
  npArtistWidth = tft.textWidth(artist);
  npArtistScroll = (npArtistWidth > tft.width() - 28);
  npArtistX = npArtistScroll ? tft.width() : 0;
  if (!npArtistScroll) {
    tft.drawString(artist, tft.width() / 2, 146);
  }

  tft.setTextDatum(TL_DATUM);
}

void updateNowPlaying() {
  if (!npTitleScroll && !npArtistScroll) return;
  unsigned long now = millis();
  if (now - lastMarqueeUpdate < MARQUEE_MS) return;
  lastMarqueeUpdate = now;

  tft.setTextFont(1);
  tft.setTextColor(drawColor);
  tft.setTextDatum(TL_DATUM);

  if (npTitleScroll) {
    tft.setTextSize(2);
    tft.fillRect(0, 102, tft.width(), 20, TFT_BLACK);
    npTitleX -= 2;
    if (npTitleX < -npTitleWidth) npTitleX = tft.width();
    tft.drawString(npTitle, npTitleX, 104);
  }
  if (npArtistScroll) {
    tft.setTextSize(2);
    tft.fillRect(0, 136, tft.width(), 20, TFT_BLACK);
    npArtistX -= 2;
    if (npArtistX < -npArtistWidth) npArtistX = tft.width();
    tft.drawString(npArtist, npArtistX, 138);
  }
}

void saveDisplaySettings() {
  prefs.putUChar("mode", (uint8_t)displayMode);
  prefs.putInt("bright", brightnessOffset);
  prefs.putUChar("cr", baseR);
  prefs.putUChar("cg", baseG);
  prefs.putUChar("cb", baseB);
}

void setDisplayMode(DisplayMode mode) {
  displayMode = mode;
  saveDisplaySettings();
  marqueePos = 0;
  lastMarqueeUpdate = 0;
  tft.fillScreen(TFT_BLACK);
  if (mode == MODE_NOW_PLAYING) {
    drawNowPlaying();
  } else if (mode == MODE_MARQUEE) {
    // drawNowPlaying() changes font/size/datum; restore ticker settings.
    tft.setTextFont(1);
    tft.setTextSize(4);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(drawColor);
  } else if (mode == MODE_STATIC) {
    lifeSeed();
  }
  Serial.printf("[TFT] Display mode -> %d\n", (int)mode);
}

static inline bool lifeGet(const uint8_t* buf, int x, int y) {
  uint32_t idx = (uint32_t)y * LIFE_W + x;
  return (buf[idx >> 3] >> (idx & 7)) & 1;
}

static inline void lifeSet(uint8_t* buf, int x, int y, bool v) {
  uint32_t idx = (uint32_t)y * LIFE_W + x;
  uint8_t mask = 1u << (idx & 7);
  if (v) buf[idx >> 3] |= mask; else buf[idx >> 3] &= (uint8_t)~mask;
}

void lifeSeed() {
  randomSeed(esp_random());
  memset(lifeCur, 0, LIFE_BYTES);
  lifeSpr.fillSprite(0);
  for (int y = 0; y < LIFE_H; y++) {
    for (int x = 0; x < LIFE_W; x++) {
      if (random(100) < 22) {
        lifeSet(lifeCur, x, y, true);
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
      }
    }
  }
  lifeSpr.pushSprite(0, 0);
  lastLifeUpdate = millis();
}

void updateLife() {
  unsigned long now = millis();
  if (now - lastLifeUpdate < MARQUEE_MS) return;
  lastLifeUpdate = now;

  uint32_t changed = 0;
  uint32_t pop = 0;
  for (int y = 0; y < LIFE_H; y++) {
    const int ym = (y == 0) ? LIFE_H - 1 : y - 1;
    const int yp = (y == LIFE_H - 1) ? 0 : y + 1;
    for (int x = 0; x < LIFE_W; x++) {
      const int xm = (x == 0) ? LIFE_W - 1 : x - 1;
      const int xp = (x == LIFE_W - 1) ? 0 : x + 1;
      const int n = lifeGet(lifeCur, xm, ym) + lifeGet(lifeCur, x, ym) +
                    lifeGet(lifeCur, xp, ym) + lifeGet(lifeCur, xm, y) +
                    lifeGet(lifeCur, xp, y) + lifeGet(lifeCur, xm, yp) +
                    lifeGet(lifeCur, x, yp) + lifeGet(lifeCur, xp, yp);
      const bool alive = lifeGet(lifeCur, x, y);
      const bool next = alive ? (n == 2 || n == 3) : (n == 3);
      lifeSet(lifeNext, x, y, next);
      if (next) pop++;
      if (next != alive) {
        changed++;
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, next ? 1 : 0);
      }
    }
  }
  uint8_t* tmp = lifeCur; lifeCur = lifeNext; lifeNext = tmp;
  lifeSpr.pushSprite(0, 0);

  // Soup died, froze into a still life, or collapsed to a boring blinker-and-
  // block residue (blinkers keep changing, so they'd dodge a freeze check).
  // Reseed while there's still something worth watching.
  const uint32_t minPop = ((uint32_t)LIFE_W * LIFE_H * LIFE_MIN_POP_PCT) / 100;
  if (changed == 0 || pop < minPop) lifeSeed();
}

void lifeRepaint() {
  // Repaint the sprite from the current field with the current color.
  if (!lifeSpriteReady) return;
  lifeSpr.fillSprite(0);
  for (int y = 0; y < LIFE_H; y++) {
    for (int x = 0; x < LIFE_W; x++) {
      if (lifeGet(lifeCur, x, y)) {
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
      }
    }
  }
  lifeSpr.pushSprite(0, 0);
}

void renderDisplay() {
  switch (displayMode) {
    case MODE_MARQUEE:     updateMarquee(); break;
    case MODE_STATIC:      updateLife(); break;
    case MODE_NOW_PLAYING: updateNowPlaying(); break;
  }
}

// ==========================================
void setupButtons() {
  for (int i=0; i<3; i++) {
    pinMode(btns[i].pin, INPUT_PULLUP);
  }
  Serial.println("Buttons init OK (pins 4,5,6)");
}

// ==========================================
void setupBLE() {
  BLEDevice::init("EyeSwap");

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new EyeSwapServerCallbacks());

  BLEService* pService = pServer->createService(EYESWAP_SERVICE_UUID);

  // Config characteristic (app -> ESP32: JSON commands)
  pConfigChar = pService->createCharacteristic(
    CONFIG_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pConfigChar->addDescriptor(new BLE2902());
  pConfigChar->setCallbacks(new ConfigCallbacks());

  // Button characteristic (ESP32 -> app: JSON button events)
  pButtonChar = pService->createCharacteristic(
    BUTTON_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pButtonChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(EYESWAP_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising as 'EyeSwap'");
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
  if (!deviceConnected || !pButtonChar) {
    Serial.printf("[BTN] %d %s (no client)\n", btnNum, longPress ? "LONG" : "TAP");
    return;
  }

  // Send JSON: {"type":"button","button":1,"action":"tap|long"}
  String command = "{\"type\":\"button\",\"button\":" + String(btnNum) + ",\"action\":\"" + (longPress ? "long" : "tap") + "\"}";

  pButtonChar->setValue(command.c_str());
  pButtonChar->notify();
  
  Serial.printf("[BLE] Sent: %s\n", command.c_str());
}
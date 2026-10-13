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
enum DisplayMode { MODE_MARQUEE, MODE_STATIC, MODE_NOW_PLAYING, MODE_BOIDS, MODE_ANT, MODE_FIRE, MODE_PIPES, MODE_RADAR, MODE_STARFIELD, MODE_OSC, MODE_PONG, MODE_EQ };

// Random Cycle: rotate the pod through the visual roster on a timer.
#define CYCLE_MS 600000UL   // 10 minutes per animation
static const DisplayMode CYCLE_MODES[] = {
  MODE_STATIC, MODE_BOIDS, MODE_ANT, MODE_FIRE, MODE_PIPES,
  MODE_RADAR, MODE_STARFIELD, MODE_OSC, MODE_PONG, MODE_EQ, MODE_MARQUEE
};
#define CYCLE_N ((int)(sizeof(CYCLE_MODES) / sizeof(CYCLE_MODES[0])))
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
volatile bool cycleStart = false;      // BLE asked for Random Cycle
static bool cycleEnabled = false;      // loop-owned
static int cycleIndex = 0;             // current slot in CYCLE_MODES
static unsigned long cycleNextAt = 0;  // millis() of next hop
volatile bool configDirty = false;
volatile bool npDirty = false;

// Display settings persist across power cycles (NVS).
Preferences prefs;

// ==== Boids (MODE_BOIDS): flocking agents on the round face ====
#define BOID_COUNT 24
struct Boid { float x, y, px, py, vx, vy; };
static Boid boids[BOID_COUNT];
static unsigned long lastBoidUpdate = 0;

// ==== Langton's ant (MODE_ANT): highway-builder on the torus ====
static uint8_t antGrid[LIFE_BYTES];
static int antX, antY, antDir;   // dir: 0=N 1=E 2=S 3=W
static unsigned long lastAntUpdate = 0;

// ==== Fire (MODE_FIRE): doom-style heat field, half-res, dithered ====
#define FIRE_W LIFE_W
#define FIRE_H LIFE_H
static uint8_t fireHeat[FIRE_W * FIRE_H];   // 120x120 heat, 0..255
static unsigned long lastFireUpdate = 0;
static float firePhase = 0.0f;

// ==== Pipes (MODE_PIPES): phosphor 3D tube maze on a lattice ====
#define PIPE_COUNT 5
#define PIPE_N 7                     // lattice nodes per axis
struct Pipe { int x, y, z, dx, dy, dz, steps, maxSteps; bool alive; };
static Pipe pipes[PIPE_COUNT];
static int pipesAlive = 0;
static unsigned long lastPipeUpdate = 0;
static unsigned long pipesRespawnAt = 0;
static float pipeCosY, pipeSinY, pipeCosP, pipeSinP;
#define PIPE_SCALE 19.0f

// ==== Radar (MODE_RADAR): phosphor scope sweep with fading blips ====
#define RADAR_W LIFE_W
#define RADAR_H LIFE_H
static uint8_t radarBuf[RADAR_W * RADAR_H];    // 120x120 intensity, 0..255
static float radarAngle = 0.0f;
static unsigned long lastRadarUpdate = 0;
#define RADAR_BLIPS 10
struct RadarBlip { float ang, rad; uint8_t lit; bool used; };
static RadarBlip radarBlips[RADAR_BLIPS];

// ==== Starfield (MODE_STARFIELD): 3D fly-through, perspective streaks ====
#define STAR_COUNT 70
struct Star { float x, y, z, sp; int px, py; bool hadPos; };
static Star stars[STAR_COUNT];
static unsigned long lastStarUpdate = 0;

// ==== Oscilloscope (MODE_OSC): Lissajous art with phosphor persistence ====
// Reuses radarBuf (120x120 intensity); exclusive modes, so no extra RAM.
static float oscT = 0.0f;      // beam parameter
static float oscDelta = 0.0f;  // slowly drifting phase -> morphing figure
static int oscPX = 0, oscPY = 0;
static bool oscHad = false;
static unsigned long lastOscUpdate = 0;

// ==== Pong (MODE_PONG): autonomous rally, blocky digits ====
#define PONG_TOP 54
#define PONG_BOT 186
#define PONG_PAD_H 30
#define PONG_PAD_X_L 25
#define PONG_PAD_X_R 215
struct PongPaddle { float y, err; };
static PongPaddle pongL, pongR;
static float pongBallX, pongBallY, pongBallVX, pongBallVY;
static int pongScoreL = 0, pongScoreR = 0, pongServeDelay = 0, pongFrame = 0;
static unsigned long lastPongUpdate = 0;

// ==== EQ fake (MODE_EQ): spectrum bars driven by a synthetic beat ====
#define EQ_BARS 14
static float eqLevel[EQ_BARS], eqTarget[EQ_BARS], eqPeak[EQ_BARS];
static int eqX[EQ_BARS], eqBaseY[EQ_BARS], eqMaxH[EQ_BARS];
static float eqT = 0.0f;
static int eqBeatCount = -1;
static unsigned long lastEqUpdate = 0;

// ==== Forward Declarations ====
void setupTFT();
void setupButtons();
void setupBLE();
void handleButtons();
void updateMarquee();
void lifeSeed();
void lifeRepaint();
void updateLife();
void boidInit();
void updateBoids();
void antInit();
void updateAnt();
void fireInit();
void updateFire();
void pipesInit();
void updatePipes();
void radarInit();
void updateRadar();
void starInit();
void updateStarfield();
void oscInit();
void updateOsc();
void pongInit();
void updatePong();
void eqInit();
void updateEq();
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
      else if (strcmp(anim, "boids") == 0)       { pendingMode = MODE_BOIDS;       modeDirty = true; }
      else if (strcmp(anim, "ant") == 0)         { pendingMode = MODE_ANT;         modeDirty = true; }
      else if (strcmp(anim, "fire") == 0)        { pendingMode = MODE_FIRE;        modeDirty = true; }
      else if (strcmp(anim, "pipes") == 0)       { pendingMode = MODE_PIPES;       modeDirty = true; }
      else if (strcmp(anim, "radar") == 0)       { pendingMode = MODE_RADAR;       modeDirty = true; }
      else if (strcmp(anim, "starfield") == 0)   { pendingMode = MODE_STARFIELD;   modeDirty = true; }
      else if (strcmp(anim, "oscilloscope") == 0 || strcmp(anim, "osc") == 0) { pendingMode = MODE_OSC; modeDirty = true; }
      else if (strcmp(anim, "pong") == 0)        { pendingMode = MODE_PONG;        modeDirty = true; }
      else if (strcmp(anim, "eq") == 0)          { pendingMode = MODE_EQ;          modeDirty = true; }
      else if (strcmp(anim, "cycle") == 0 || strcmp(anim, "random") == 0) { cycleStart = true; }
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
    if (savedMode > MODE_EQ) savedMode = MODE_MARQUEE;
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
  if (cycleStart) {                          // Random Cycle: start now
    cycleStart = false;
    cycleEnabled = true;
    cycleIndex = (int)random(CYCLE_N);
    setDisplayMode(CYCLE_MODES[cycleIndex]);
    cycleNextAt = millis() + CYCLE_MS;
  }
  if (modeDirty) { modeDirty = false; cycleEnabled = false; setDisplayMode(pendingMode); }
  if (cycleEnabled && millis() >= cycleNextAt) {   // hop to a fresh animation
    int k = cycleIndex;
    while (CYCLE_N > 1 && k == cycleIndex) k = (int)random(CYCLE_N);
    cycleIndex = k;
    setDisplayMode(CYCLE_MODES[cycleIndex]);
    cycleNextAt = millis() + CYCLE_MS;
  }
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
  } else if (mode == MODE_BOIDS) {
    boidInit();
  } else if (mode == MODE_ANT) {
    antInit();
  } else if (mode == MODE_FIRE) {
    fireInit();
  } else if (mode == MODE_PIPES) {
    pipesInit();
  } else if (mode == MODE_RADAR) {
    radarInit();
  } else if (mode == MODE_STARFIELD) {
    starInit();
  } else if (mode == MODE_OSC) {
    oscInit();
  } else if (mode == MODE_PONG) {
    pongInit();
  } else if (mode == MODE_EQ) {
    eqInit();
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

// ==== Boids (MODE_BOIDS) ====
void boidInit() {
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];
    b->x = 120.0f + (random(81) - 40);
    b->y = 120.0f + (random(81) - 40);
    b->px = b->x;
    b->py = b->y;
    float a = random(0, 628) / 100.0f;
    float s = 1.2f + random(100) / 100.0f;
    b->vx = cosf(a) * s;
    b->vy = sinf(a) * s;
  }
}

void updateBoids() {
  unsigned long now = millis();
  if (now - lastBoidUpdate < 50) return;
  lastBoidUpdate = now;

  const float cx = 120.0f, cy = 120.0f, maxR = 105.0f;
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];
    float ax = 0, ay = 0, cxs = 0, cys = 0, vxs = 0, vys = 0;
    int neighbors = 0;
    for (int j = 0; j < BOID_COUNT; j++) {
      if (j == i) continue;
      Boid* o = &boids[j];
      float dx = o->x - b->x, dy = o->y - b->y;
      float d2 = dx * dx + dy * dy;
      if (d2 > 48.0f * 48.0f) continue;
      neighbors++;
      cxs += o->x; cys += o->y;      // cohesion
      vxs += o->vx; vys += o->vy;    // alignment
      if (d2 < 14.0f * 14.0f) { ax -= dx; ay -= dy; }  // separation
    }
    if (neighbors > 0) {
      ax += (cxs / neighbors - b->x) * 0.006f;
      ay += (cys / neighbors - b->y) * 0.006f;
      ax += (vxs / neighbors - b->vx) * 0.05f;
      ay += (vys / neighbors - b->vy) * 0.05f;
    }
    // Steer back inside the round face.
    float dx = b->x - cx, dy = b->y - cy;
    float r = sqrtf(dx * dx + dy * dy);
    if (r > maxR) { ax -= (dx / r) * 0.15f; ay -= (dy / r) * 0.15f; }

    b->vx += ax; b->vy += ay;
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
    if (sp > 2.2f) {
      b->vx = b->vx / sp * 2.2f; b->vy = b->vy / sp * 2.2f;
    } else if (sp < 1.0f && sp > 0.001f) {
      b->vx = b->vx / sp; b->vy = b->vy / sp;
    }

    b->px = b->x; b->py = b->y;
    b->x += b->vx; b->y += b->vy;
    if (b->x < 0) b->x = 0; if (b->x > 239) b->x = 239;
    if (b->y < 0) b->y = 0; if (b->y > 239) b->y = 239;
  }

  lifeSpr.fillSprite(0);
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];
    // Arrowhead pointing along heading: tip forward, two swept-back base corners.
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
    float fx = 1.0f, fy = 0.0f;
    if (sp > 0.001f) { fx = b->vx / sp; fy = b->vy / sp; }
    float px = -fy, py = fx;               // unit perpendicular
    const float L = 5.0f, W = 2.8f;        // arrow length / base half-width
    int tipx = (int)lroundf(b->x + fx * L);
    int tipy = (int)lroundf(b->y + fy * L);
    int lx = (int)lroundf(b->x - fx * 1.5f + px * W);
    int ly = (int)lroundf(b->y - fy * 1.5f + py * W);
    int rx = (int)lroundf(b->x - fx * 1.5f - px * W);
    int ry = (int)lroundf(b->y - fy * 1.5f - py * W);
    lifeSpr.fillTriangle(tipx, tipy, lx, ly, rx, ry, 1);
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Langton's ant (MODE_ANT) ====
static inline bool antGet(int x, int y) {
  uint32_t idx = (uint32_t)y * LIFE_W + x;
  return (antGrid[idx >> 3] >> (idx & 7)) & 1;
}

static inline void antFlip(int x, int y) {
  uint32_t idx = (uint32_t)y * LIFE_W + x;
  antGrid[idx >> 3] ^= (uint8_t)(1u << (idx & 7));
}

void antInit() {
  memset(antGrid, 0, sizeof(antGrid));
  lifeSpr.fillSprite(0);
  antX = LIFE_W / 2;
  antY = LIFE_H / 2;
  antDir = 0;
}

void updateAnt() {
  unsigned long now = millis();
  if (now - lastAntUpdate < 50) return;
  lastAntUpdate = now;

  for (int i = 0; i < 5; i++) {            // 5 steps/tick ~= 100 steps/s
    bool on = antGet(antX, antY);
    antDir = (antDir + (on ? 1 : 3)) & 3;  // right on filled, left on empty
    antFlip(antX, antY);
    lifeSpr.fillRect(antX * LIFE_CELL, antY * LIFE_CELL, LIFE_CELL, LIFE_CELL,
                     on ? 0 : 1);
    switch (antDir) {
      case 0: antY = (antY == 0) ? LIFE_H - 1 : antY - 1; break;
      case 1: antX = (antX == LIFE_W - 1) ? 0 : antX + 1; break;
      case 2: antY = (antY == LIFE_H - 1) ? 0 : antY + 1; break;
      default: antX = (antX == 0) ? LIFE_W - 1 : antX - 1; break;
    }
  }
  // Show the ant itself at the head of its trail (self-corrects next flip).
  lifeSpr.fillRect(antX * LIFE_CELL, antY * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
  lifeSpr.pushSprite(0, 0);
}

// ==== Fire (MODE_FIRE) ====
// 4x4 ordered-dither threshold: turns the 8-bit heat field into 1bpp shading,
// so hot regions read solid/bright and cooler regions sparse/dim.
static const uint8_t BAYER4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

void fireInit() {
  randomSeed(esp_random());
  memset(fireHeat, 0, sizeof(fireHeat));
  firePhase = 0.0f;
  lifeSpr.fillSprite(0);
}

void updateFire() {
  unsigned long now = millis();
  if (now - lastFireUpdate < 35) return;
  lastFireUpdate = now;
  firePhase += 0.045f;

  // Fuel bed = wandering hot spots (two drifting emitters) plus dim coals
  // everywhere, so tongues lick up from points instead of a uniform wall.
  float c1 = FIRE_W * (0.5f + 0.33f * sinf(firePhase * 0.90f));
  float c2 = FIRE_W * (0.5f + 0.35f * sinf(firePhase * 0.47f + 2.1f));
  for (int x = 0; x < FIRE_W; x++) {
    float d1 = fabsf(x - c1), d2 = fabsf(x - c2);
    float bump = 130.0f * expf(-d1 * d1 / 128.0f)   // sigma ~8 cells
               +  95.0f * expf(-d2 * d2 / 242.0f);  // sigma ~11 cells
    int v = (int)(55.0f + bump + random(50));
    if (v > 255) v = 255;
    fireHeat[(FIRE_H - 1) * FIRE_W + x] = (uint8_t)v;
  }

  // Rise: each cell pulls from ONE random cell below (doom-style), which
  // keeps high-frequency noise alive -> ragged tongues. Cooling grows with
  // height so flames die out before the top of the face.
  for (int y = 0; y < FIRE_H - 1; y++) {
    for (int x = 0; x < FIRE_W; x++) {
      int sx = x + (int)random(3) - 1;             // jagged column drift
      if (sx < 0) sx = FIRE_W - 1; else if (sx >= FIRE_W) sx = 0;
      int v = fireHeat[(y + 1) * FIRE_W + sx];
      v -= 1 + random(3) + (y >> 6);               // 1..3, taller flame
      if (v < 0) v = 0;
      fireHeat[y * FIRE_W + x] = (uint8_t)v;
    }
  }

  // Sparks: a few fresh 255 embers near the hot spots each frame.
  for (int s = 0; s < 4; s++) {
    float c = (s & 1) ? c1 : c2;
    int x = (int)c + (int)random(13) - 6;
    if (x < 0) x = 0; else if (x >= FIRE_W) x = FIRE_W - 1;
    int y = FIRE_H - 1 - random(6);
    fireHeat[y * FIRE_W + x] = 255;
  }

  // Dither into the shared 1bpp sprite at 2x2 blocks.
  lifeSpr.fillSprite(0);
  for (int y = 0; y < FIRE_H; y++) {
    for (int x = 0; x < FIRE_W; x++) {
      uint8_t h = fireHeat[y * FIRE_W + x];
      uint8_t t = (uint8_t)(BAYER4[y & 3][x & 3] * 16 + 8);
      if (h > t) lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
    }
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Pipes (MODE_PIPES) ====
static const int PIPE_DIRS[6][3] = {
  { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
};

// Fixed 3/4 view: yaw -30 deg, pitch ~25 deg, orthographic.
static void pipeProject(float xi, float yi, float zi, int* sx, int* sy, float* depth) {
  const float c = (PIPE_N - 1) * 0.5f;
  float x = xi - c, y = yi - c, z = c - zi;          // z up
  float xr = x * pipeCosY - y * pipeSinY;
  float yr = x * pipeSinY + y * pipeCosY;
  float yy = yr * pipeCosP - z * pipeSinP;           // screen up
  float dd = yr * pipeSinP + z * pipeCosP;           // toward viewer
  *sx = 120 + (int)lroundf(xr * PIPE_SCALE);
  *sy = 120 - (int)lroundf(yy * PIPE_SCALE);
  *depth = dd;
}

static int pipeRadius(float d) {
  if (d > 1.2f) return 3;           // near: fat
  if (d < -1.2f) return 1;          // far: thin
  return 2;
}

// mode 0: any valid direction; mode 1: elbow turn (no straight/reverse).
static void pipePickDir(Pipe* p, bool turn) {
  for (int tries = 0; tries < 12; tries++) {
    int k = random(6);
    const int* d = PIPE_DIRS[k];
    bool straight = (d[0] == p->dx && d[1] == p->dy && d[2] == p->dz);
    bool reverse  = (d[0] == -p->dx && d[1] == -p->dy && d[2] == -p->dz);
    if (turn && (straight || reverse)) continue;
    if (!turn && reverse) continue;
    int nx = p->x + d[0], ny = p->y + d[1], nz = p->z + d[2];
    if (nx < 0 || nx >= PIPE_N || ny < 0 || ny >= PIPE_N || nz < 0 || nz >= PIPE_N) continue;
    p->dx = d[0]; p->dy = d[1]; p->dz = d[2];
    return;
  }
  p->alive = false;   // boxed in
}

void pipesInit() {
  randomSeed(esp_random());
  pipeCosY = cosf(-0.52f); pipeSinY = sinf(-0.52f);
  pipeCosP = cosf(0.44f);  pipeSinP = sinf(0.44f);
  lifeSpr.fillSprite(0);
  pipesAlive = 0;
  for (int i = 0; i < PIPE_COUNT; i++) {
    Pipe* p = &pipes[i];
    p->x = random(PIPE_N); p->y = random(PIPE_N); p->z = random(PIPE_N);
    p->steps = 0; p->maxSteps = 28 + random(36);
    p->dx = p->dy = p->dz = 0;
    p->alive = true;
    pipePickDir(p, false);
    if (!p->alive) continue;
    pipesAlive++;
    int sx, sy; float d;
    pipeProject(p->x, p->y, p->z, &sx, &sy, &d);
    lifeSpr.fillCircle(sx, sy, pipeRadius(d), 1);
  }
  lifeSpr.pushSprite(0, 0);
}

void updatePipes() {
  unsigned long now = millis();
  if (pipesAlive == 0) {
    if (pipesRespawnAt == 0 || now >= pipesRespawnAt) pipesInit();
    return;
  }
  if (now - lastPipeUpdate < 70) return;
  lastPipeUpdate = now;

  for (int i = 0; i < PIPE_COUNT; i++) {
    Pipe* p = &pipes[i];
    if (!p->alive) continue;

    bool turned = false;
    if (random(100) < 26) {
      pipePickDir(p, true);            // elbow
      turned = true;
    } else {
      int nx = p->x + p->dx, ny = p->y + p->dy, nz = p->z + p->dz;
      if (nx < 0 || nx >= PIPE_N || ny < 0 || ny >= PIPE_N || nz < 0 || nz >= PIPE_N)
        pipePickDir(p, false);         // wall ahead: try any way out
    }
    if (!p->alive) { pipesAlive--; continue; }

    int nx = p->x + p->dx, ny = p->y + p->dy, nz = p->z + p->dz;
    int x0, y0, x1, y1; float d0, d1;
    pipeProject(p->x, p->y, p->z, &x0, &y0, &d0);
    pipeProject(nx, ny, nz, &x1, &y1, &d1);

    // Depth shading: near runs fat, far runs thin (1bpp phosphor look).
    float dmid = d0 + d1;
    int thick = (dmid > 1.0f) ? 3 : ((dmid < -1.0f) ? 1 : 2);
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy) + 0.001f;
    float px = -dy / len, py = dx / len;
    for (int k = -(thick / 2); k <= thick / 2; k++)
      lifeSpr.drawLine(x0 + (int)lroundf(px * k), y0 + (int)lroundf(py * k),
                       x1 + (int)lroundf(px * k), y1 + (int)lroundf(py * k), 1);
    if (turned) lifeSpr.fillCircle(x0, y0, thick + 1, 1);   // joint sphere

    p->x = nx; p->y = ny; p->z = nz; p->steps++;
    if (p->steps >= p->maxSteps) {
      lifeSpr.fillCircle(x1, y1, thick, 1);                  // end cap
      p->alive = false;
      pipesAlive--;
    }
  }
  lifeSpr.pushSprite(0, 0);
  if (pipesAlive == 0) pipesRespawnAt = millis() + 1600;
}

// ==== Radar (MODE_RADAR) ====
static inline void radarPlot(int x, int y, uint8_t v) {
  if ((unsigned)x >= RADAR_W || (unsigned)y >= RADAR_H) return;
  uint8_t* p = &radarBuf[y * RADAR_W + x];
  if (v > *p) *p = v;
}

static void radarLine(int x0, int y0, int x1, int y1, uint8_t v) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    radarPlot(x0, y0, v);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void radarRing(float r, uint8_t v) {
  for (int a = 0; a < 360; a += 3) {
    float t = a * 0.0174533f;
    radarPlot((int)lroundf(60.0f + r * cosf(t)),
              (int)lroundf(60.0f + r * sinf(t)), v);
  }
}

static void radarSpawnBlip() {
  for (int i = 0; i < RADAR_BLIPS; i++) {
    if (!radarBlips[i].used) {
      radarBlips[i].used = true;
      radarBlips[i].ang = random(628) / 100.0f;
      radarBlips[i].rad = 12 + random(46);
      radarBlips[i].lit = 0;
      return;
    }
  }
  int i = random(RADAR_BLIPS);
  radarBlips[i].ang = random(628) / 100.0f;
  radarBlips[i].rad = 12 + random(46);
  radarBlips[i].lit = 0;
}

void radarInit() {
  randomSeed(esp_random());
  memset(radarBuf, 0, sizeof(radarBuf));
  radarAngle = 0.0f;
  for (int i = 0; i < RADAR_BLIPS; i++) { radarBlips[i].used = false; radarBlips[i].lit = 0; }
  for (int i = 0; i < 5; i++) radarSpawnBlip();
  lifeSpr.fillSprite(0);
}

void updateRadar() {
  unsigned long now = millis();
  if (now - lastRadarUpdate < 30) return;
  lastRadarUpdate = now;

  // Phosphor decay: everything fades a step each frame.
  for (int i = 0; i < RADAR_W * RADAR_H; i++) {
    uint8_t v = radarBuf[i];
    radarBuf[i] = (v > 9) ? (uint8_t)(v - 9) : 0;
  }

  // Static chrome: range rings + crosshair (redrawn each frame -> steady dim).
  radarRing(20.0f, 78); radarRing(40.0f, 78); radarRing(58.0f, 78);
  radarLine(60, 4, 60, 116, 55);
  radarLine(4, 60, 116, 60, 55);

  // Sweep: bright leading edge + a half-step ghost behind it.
  radarAngle += 0.06f;
  if (radarAngle > 6.28318f) radarAngle -= 6.28318f;
  radarLine(60, 60, 60 + (int)lroundf(58.0f * cosf(radarAngle)),
                     60 + (int)lroundf(58.0f * sinf(radarAngle)), 255);
  radarLine(60, 60, 60 + (int)lroundf(58.0f * cosf(radarAngle - 0.06f)),
                     60 + (int)lroundf(58.0f * sinf(radarAngle - 0.06f)), 170);

  // Blips: light when the sweep passes over, then fade until next rotation.
  for (int i = 0; i < RADAR_BLIPS; i++) {
    RadarBlip &b = radarBlips[i];
    if (!b.used) continue;
    float d = radarAngle - b.ang;
    while (d < 0) d += 6.28318f;
    while (d >= 6.28318f) d -= 6.28318f;
    if (d < 0.12f) b.lit = 255;
    if (b.lit) b.lit = (b.lit > 7) ? (uint8_t)(b.lit - 7) : 0;
    if (b.lit > 40) {
      int bx = 60 + (int)lroundf(b.rad * cosf(b.ang));
      int by = 60 + (int)lroundf(b.rad * sinf(b.ang));
      radarPlot(bx, by, b.lit);
      radarPlot(bx + 1, by, b.lit); radarPlot(bx - 1, by, b.lit);
      radarPlot(bx, by + 1, b.lit); radarPlot(bx, by - 1, b.lit);
    }
  }
  if (random(100) < 1) radarSpawnBlip();   // occasional new contact

  // Dither the intensity field into the shared 1bpp sprite at 2x2 blocks.
  lifeSpr.fillSprite(0);
  for (int y = 0; y < RADAR_H; y++) {
    for (int x = 0; x < RADAR_W; x++) {
      uint8_t v = radarBuf[y * RADAR_W + x];
      uint8_t t = (uint8_t)(BAYER4[y & 3][x & 3] * 16 + 8);
      if (v > t) lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
    }
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Starfield (MODE_STARFIELD) ====
void starInit() {
  randomSeed(esp_random());
  for (int i = 0; i < STAR_COUNT; i++) {
    Star* s = &stars[i];
    s->x = (random(2001) - 1000) / 1000.0f;
    s->y = (random(2001) - 1000) / 1000.0f;
    s->z = 0.05f + random(96) / 100.0f;          // 0.05..1.0
    s->sp = 0.010f + random(21) / 1000.0f;       // 0.010..0.030
    s->px = s->py = 0;
    s->hadPos = false;
  }
  lastStarUpdate = 0;
  lifeSpr.fillSprite(0);
}

void updateStarfield() {
  unsigned long now = millis();
  if (now - lastStarUpdate < 30) return;
  lastStarUpdate = now;

  lifeSpr.fillSprite(0);
  for (int i = 0; i < STAR_COUNT; i++) {
    Star* s = &stars[i];
    s->z -= s->sp;
    if (s->z <= 0.03f) {                         // flew past: respawn far
      s->x = (random(2001) - 1000) / 1000.0f;
      s->y = (random(2001) - 1000) / 1000.0f;
      s->z = 1.0f;
      s->hadPos = false;
    }
    float inv = 1.0f / s->z;
    int sx = 120 + (int)lroundf(s->x * inv * 42.0f);
    int sy = 120 + (int)lroundf(s->y * inv * 42.0f);
    int ddx = sx - 120, ddy = sy - 120;
    if (ddx * ddx + ddy * ddy > 118 * 118) {     // outside the round face
      s->hadPos = false;
      continue;
    }
    if (s->hadPos) lifeSpr.drawLine(s->px, s->py, sx, sy, 1);   // streak
    else           lifeSpr.drawPixel(sx, sy, 1);                // far: dot
    s->px = sx; s->py = sy; s->hadPos = true;
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Oscilloscope (MODE_OSC) ====
// Beam stroke: a 3-cell-wide bright line so the trace clearly outranks the grid.
static void oscStroke(int x0, int y0, int x1, int y1) {
  radarLine(x0, y0, x1, y1, 255);
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy) + 0.001f;
  float px = -dy / len, py = dx / len;
  radarLine(x0 + (int)lroundf(px), y0 + (int)lroundf(py),
            x1 + (int)lroundf(px), y1 + (int)lroundf(py), 255);
  radarLine(x0 - (int)lroundf(px), y0 - (int)lroundf(py),
            x1 - (int)lroundf(px), y1 - (int)lroundf(py), 255);
}

void oscInit() {
  randomSeed(esp_random());
  memset(radarBuf, 0, sizeof(radarBuf));
  oscT = 0.0f;
  oscDelta = 0.0f;
  oscHad = false;
  lifeSpr.fillSprite(0);
}

void updateOsc() {
  unsigned long now = millis();
  if (now - lastOscUpdate < 25) return;
  lastOscUpdate = now;

  // Phosphor decay (same persistence trick as the radar).
  for (int i = 0; i < RADAR_W * RADAR_H; i++) {
    uint8_t v = radarBuf[i];
    radarBuf[i] = (v > 9) ? (uint8_t)(v - 9) : 0;
  }

  // Graticule: faint dashed grid + modest center axes (kept well under the beam).
  for (int x = 20; x < 120; x += 20) radarLine(x, 6, x, 114, 24);
  for (int y = 20; y < 120; y += 20) radarLine(6, y, 114, y, 24);
  radarLine(60, 4, 60, 116, 48);
  radarLine(4, 60, 116, 60, 48);

  // Lissajous beam: x = A sin(3t + d), y = A sin(2t). Fixed 3:2 ratio with a
  // slowly drifting phase d morphs the figure (line -> ellipse -> 8 -> ...).
  oscT += 0.10f;
  if (oscT > 6.28318f) oscT -= 6.28318f;
  oscDelta += 0.008f;
  if (oscDelta > 6.28318f) oscDelta -= 6.28318f;
  int xi = 60 + (int)lroundf(40.0f * sinf(3.0f * oscT + oscDelta));
  int yi = 60 + (int)lroundf(40.0f * sinf(2.0f * oscT));
  if (oscHad) oscStroke(oscPX, oscPY, xi, yi);
  else        radarPlot(xi, yi, 255);
  oscPX = xi; oscPY = yi; oscHad = true;

  // Dither the intensity field into the shared 1bpp sprite at 2x2 blocks.
  lifeSpr.fillSprite(0);
  for (int y = 0; y < RADAR_H; y++) {
    for (int x = 0; x < RADAR_W; x++) {
      uint8_t v = radarBuf[y * RADAR_W + x];
      uint8_t t = (uint8_t)(BAYER4[y & 3][x & 3] * 16 + 8);
      if (v > t) lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 1);
    }
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Pong (MODE_PONG) ====
// Blocky 3x5 digit font, MSB-left per row — authentic pong scoreboard.
static const uint8_t PONG_DIGITS[10][5] = {
  {0b111,0b101,0b101,0b101,0b111},  // 0
  {0b010,0b110,0b010,0b010,0b111},  // 1
  {0b111,0b001,0b111,0b100,0b111},  // 2
  {0b111,0b001,0b111,0b001,0b111},  // 3
  {0b101,0b101,0b111,0b001,0b001},  // 4
  {0b111,0b100,0b111,0b001,0b111},  // 5
  {0b111,0b100,0b111,0b101,0b111},  // 6
  {0b111,0b001,0b010,0b010,0b010},  // 7
  {0b111,0b101,0b111,0b101,0b111},  // 8
  {0b111,0b101,0b111,0b001,0b111}   // 9
};

static void pongDigit(int d, int x, int y) {
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 3; c++)
      if (PONG_DIGITS[d][r] & (0b100 >> c))
        lifeSpr.fillRect(x + c * 3, y + r * 3, 3, 3, 1);
}

static void pongScoreDraw(int score, int x) {
  if (score >= 10) {
    pongDigit(score / 10, x - 6, 36);
    pongDigit(score % 10, x + 6, 36);
  } else {
    pongDigit(score, x, 36);
  }
}

static void pongServe(int dir) {
  pongBallX = 120; pongBallY = 120;
  pongBallVX = 2.3f * dir;
  pongBallVY = (random(200) - 100) / 50.0f;      // -2..2
  pongServeDelay = 45;                            // freeze ~1.4 s at serve
}

void pongInit() {
  randomSeed(esp_random());
  pongScoreL = pongScoreR = 0;
  pongFrame = 0;
  pongL.y = pongR.y = 120;
  pongServe(random(2) ? 1 : -1);
  lifeSpr.fillSprite(0);
}

static void pongStepPaddle(PongPaddle* pad, bool toward) {
  if (pongFrame % 45 == 0) pad->err = (random(200) - 100) / 4.0f;   // -25..25
  float ty = toward ? (pongBallY + pad->err) : 120.0f;
  float dy = ty - pad->y;
  if (dy > 2.8f) dy = 2.8f; else if (dy < -2.8f) dy = -2.8f;
  pad->y += dy;
  float lo = PONG_TOP + PONG_PAD_H / 2, hi = PONG_BOT - PONG_PAD_H / 2;
  if (pad->y < lo) pad->y = lo;
  if (pad->y > hi) pad->y = hi;
}

void updatePong() {
  unsigned long now = millis();
  if (now - lastPongUpdate < 30) return;
  lastPongUpdate = now;
  pongFrame++;

  // Paddle AI: track when the ball is coming, drift to center otherwise.
  pongStepPaddle(&pongL, pongBallVX < 0);
  pongStepPaddle(&pongR, pongBallVX > 0);

  if (pongServeDelay > 0) {
    pongServeDelay--;
  } else {
    pongBallX += pongBallVX;
    pongBallY += pongBallVY;

    // Walls
    if (pongBallY < PONG_TOP + 2) { pongBallY = PONG_TOP + 2; pongBallVY = fabsf(pongBallVY); }
    if (pongBallY > PONG_BOT - 2) { pongBallY = PONG_BOT - 2; pongBallVY = -fabsf(pongBallVY); }

    // Paddle collisions (ball is a 4px square, +/-2 around center)
    if (pongBallVX < 0 && pongBallX <= PONG_PAD_X_L + 4 &&
        pongBallX >= PONG_PAD_X_L - 8) {
      if (fabsf(pongBallY - pongL.y) <= PONG_PAD_H / 2 + 2) {
        pongBallX = PONG_PAD_X_L + 4;
        pongBallVX = -pongBallVX;
        pongBallVY += (pongBallY - pongL.y) * 0.09f;      // angle from hit spot
        pongBallVX *= 1.04f;                              // rally speeds up
      }
    }
    if (pongBallVX > 0 && pongBallX >= PONG_PAD_X_R - 4 &&
        pongBallX <= PONG_PAD_X_R + 8) {
      if (fabsf(pongBallY - pongR.y) <= PONG_PAD_H / 2 + 2) {
        pongBallX = PONG_PAD_X_R - 4;
        pongBallVX = -pongBallVX;
        pongBallVY += (pongBallY - pongR.y) * 0.09f;
        pongBallVX *= 1.04f;
      }
    }
    float sp = pongBallVY;
    if (sp > 5.0f) pongBallVY = 5.0f; else if (sp < -5.0f) pongBallVY = -5.0f;
    if (pongBallVX > 4.5f) pongBallVX = 4.5f;
    if (pongBallVX < -4.5f) pongBallVX = -4.5f;

    // Score when the ball escapes a side, then serve toward the loser.
    if (pongBallX < -4) {
      pongScoreR++;
      if (pongScoreR >= 11) { pongScoreL = 0; pongScoreR = 0; }
      pongServe(-1);
    } else if (pongBallX > 244) {
      pongScoreL++;
      if (pongScoreL >= 11) { pongScoreL = 0; pongScoreR = 0; }
      pongServe(1);
    }
  }

  // Draw
  lifeSpr.fillSprite(0);
  for (int y = PONG_TOP; y < PONG_BOT; y += 10)          // dashed center line
    lifeSpr.fillRect(119, y, 2, 5, 1);
  lifeSpr.fillRect(PONG_PAD_X_L, (int)pongL.y - PONG_PAD_H / 2, 4, PONG_PAD_H, 1);
  lifeSpr.fillRect(PONG_PAD_X_R, (int)pongR.y - PONG_PAD_H / 2, 4, PONG_PAD_H, 1);
  lifeSpr.fillRect((int)pongBallX - 2, (int)pongBallY - 2, 4, 4, 1);
  pongScoreDraw(pongScoreL, 86);
  pongScoreDraw(pongScoreR, 154);
  lifeSpr.pushSprite(0, 0);
}

// ==== EQ fake (MODE_EQ) ====
// Bars fan out across the face, bottoms sitting on the bezel arc.
void eqInit() {
  randomSeed(esp_random());
  eqT = 0.0f;
  eqBeatCount = -1;
  const float R = 116.0f;
  for (int i = 0; i < EQ_BARS; i++) {
    int x = 16 + i * 16;                       // bar left edge, 8 px wide
    int c = x + 4;
    float dx = fabsf((float)c - 120.0f);
    float dy = sqrtf(R * R - dx * dx);
    eqX[i] = x;
    eqBaseY[i] = (int)(120.0f + dy - 6.0f);    // just inside the bezel
    int h = (int)(2.0f * dy - 12.0f);          // full chord headroom
    if (h > 150) h = 150;
    eqMaxH[i] = h;
    eqLevel[i] = eqTarget[i] = eqPeak[i] = 0.0f;
  }
  lifeSpr.fillSprite(0);
}

void updateEq() {
  unsigned long now = millis();
  if (now - lastEqUpdate < 30) return;
  lastEqUpdate = now;
  eqT += 0.03f;

  // Band targets: bass slow and punchy, highs fast and fluttery.
  for (int i = 0; i < EQ_BARS; i++) {
    float f = 1.2f + i * 0.45f;
    float v = 0.22f + 0.58f * fabsf(sinf(eqT * f + i * 1.7f)) + 0.20f * (random(101) / 100.0f);
    if (v > 1.0f) v = 1.0f;
    eqTarget[i] = v;
  }

  // Synthetic beat: kick every ~0.5 s with a wandering period. Applied after
  // target computation so the thump actually lands.
  float period = 0.52f + 0.12f * sinf(eqT * 0.31f);
  int beat = (int)(eqT / period);
  if (beat != eqBeatCount) {
    eqBeatCount = beat;
    for (int i = 0; i < 5; i++) eqTarget[i] = 1.0f;   // bass thump
    if ((beat & 3) == 2) eqTarget[6] = 1.0f;          // backbeat snare
  }

  // Level dynamics: fast attack, slow decay. Peak hold falls slowly.
  for (int i = 0; i < EQ_BARS; i++) {
    if (eqTarget[i] > eqLevel[i]) eqLevel[i] += (eqTarget[i] - eqLevel[i]) * 0.55f;
    else { eqLevel[i] -= 0.05f; if (eqLevel[i] < 0) eqLevel[i] = 0; }
    if (eqLevel[i] >= eqPeak[i]) eqPeak[i] = eqLevel[i];
    else { eqPeak[i] -= 0.008f; if (eqPeak[i] < 0) eqPeak[i] = 0; }
  }

  // Draw: segmented LED bars (3 on, 1 gap) + peak-hold cap.
  lifeSpr.fillSprite(0);
  for (int i = 0; i < EQ_BARS; i++) {
    int h = (int)(eqLevel[i] * eqMaxH[i]);
    int nseg = h / 4;
    for (int k = 0; k < nseg; k++)
      lifeSpr.fillRect(eqX[i], eqBaseY[i] - 4 * (k + 1) + 1, 8, 3, 1);
    int ph = (int)(eqPeak[i] * eqMaxH[i]);
    int py = eqBaseY[i] - ph - 3;
    int top = eqBaseY[i] - eqMaxH[i];
    if (py < top) py = top;
    lifeSpr.fillRect(eqX[i], py, 8, 2, 1);
  }
  lifeSpr.pushSprite(0, 0);
}

void renderDisplay() {
  switch (displayMode) {
    case MODE_MARQUEE:     updateMarquee(); break;
    case MODE_STATIC:      updateLife(); break;
    case MODE_NOW_PLAYING: updateNowPlaying(); break;
    case MODE_BOIDS:       updateBoids(); break;
    case MODE_ANT:         updateAnt(); break;
    case MODE_FIRE:        updateFire(); break;
    case MODE_PIPES:       updatePipes(); break;
    case MODE_RADAR:       updateRadar(); break;
    case MODE_STARFIELD:   updateStarfield(); break;
    case MODE_OSC:         updateOsc(); break;
    case MODE_PONG:        updatePong(); break;
    case MODE_EQ:          updateEq(); break;
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
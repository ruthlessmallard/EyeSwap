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
enum DisplayMode { MODE_COIN, MODE_STATIC, MODE_NOW_PLAYING, MODE_BOIDS, MODE_ANT, MODE_FIRE, MODE_KNIGHT, MODE_RADAR, MODE_STARFIELD, MODE_OSC, MODE_RALLY, MODE_EQ, MODE_INDICATOR, MODE_PIPES };

// Random Cycle: rotate the pod through the visual roster on a timer.
#define CYCLE_MS 600000UL   // 10 minutes per animation
static const DisplayMode CYCLE_MODES[] = {
  MODE_COIN, MODE_STATIC, MODE_BOIDS, MODE_ANT, MODE_FIRE, MODE_KNIGHT, MODE_PIPES,
  MODE_RADAR, MODE_STARFIELD, MODE_OSC, MODE_RALLY, MODE_EQ
};
#define CYCLE_N ((int)(sizeof(CYCLE_MODES) / sizeof(CYCLE_MODES[0])))
DisplayMode displayMode = MODE_STATIC;   // the user's chosen animation (persisted)
DisplayMode activeMode  = MODE_INDICATOR; // what is actually on screen right now
int brightnessOffset = 0;   // -50..+50, applied as a text brightness scale
uint8_t baseR = 0xFF, baseG = 0xBF, baseB = 0x00;  // primary: amber default
uint8_t accR  = 0xFF, accG  = 0xFF, accB  = 0xFF;  // accent: white default
uint16_t drawColor = TICKER_COLOR;
uint16_t accentColor = 0xFFFF;

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

// Game of Life (MODE_STATIC): two species share the field, each following
// Conway's rules against its own colour. The secondary species also predates:
// a predator with 3+ predator neighbours converts one adjacent prey cell.
// Torus wrap; reseeds when the soup dies out or freezes into a still life.
#define LIFE_CELL 2                     // px per cell: 2 = 50% zoom-in, 120x120 field
#define LIFE_W (240 / LIFE_CELL)
#define LIFE_H (240 / LIFE_CELL)
#define LIFE_BYTES ((LIFE_W * LIFE_H * 2 + 7) / 8)   // 2 bits/cell: 0 empty, 1 primary, 2 secondary
#define LIFE_MIN_POP_PCT 2              // reseed when population < this % of the field
static uint8_t lifeBufA[LIFE_BYTES];
static uint8_t lifeBufB[LIFE_BYTES];
static uint8_t* lifeCur = lifeBufA;
static uint8_t* lifeNext = lifeBufB;
static TFT_eSprite lifeSpr = TFT_eSprite(&tft);
static bool lifeSpriteReady = false;
static unsigned long lastLifeUpdate = 0;

// Stagnation detector: we already count live cells every generation, so a
// rolling fingerprint (live count + cheap FNV hash of the live coordinates)
// costs almost nothing. If 2 of the last 3 generations produce the same
// fingerprint, the field is stuck in a repeating pattern (blinker, traffic
// light, still life) and gets reseeded. A bare pixel count would false-trigger
// on healthy soup and kill gliders (constant count while moving) — the hash
// is what separates "churning" from "stuck".
#define LIFE_SIG_SLOTS 3
static uint16_t lifeSigCount[LIFE_SIG_SLOTS];
static uint32_t lifeSigHash[LIFE_SIG_SLOTS];
static uint32_t lifeSigFill = 0;

// BLE-callback handoff: onWrite only records state; the loop task does ALL
// TFT/SPI work. Two tasks driving one SPI bus corrupts the transaction
// mutex and panics (assert in xQueueGenericSend / spiEndTransaction).
volatile DisplayMode pendingMode = MODE_STATIC;
volatile bool modeDirty = false;
// ==== Marquee: boot / link indicator, NOT an animation ====
// Unlinked pod shows it; a linked pod shows the user's animation. The pod holds
// it for MARQUEE_STARTUP_MS after boot or a disconnect, then hands over to the
// chosen animation and re-runs one full scroll pass every MARQUEE_EVERY_MS.
#define MARQUEE_STARTUP_MS 300000UL   // 5 min
#define MARQUEE_EVERY_MS   600000UL   // one pass per 10 min unlinked
enum MarqueePhase : uint8_t { MP_STARTUP, MP_ANIM, MP_PASS };
static MarqueePhase mpPhase = MP_STARTUP;
static unsigned long mpSince = 0;
static bool mpWasLinked = false;
static volatile bool mpPassDone = false;
static volatile bool mpTestOnePass = false;   // BLE "marquee" = show one pass, do not persist
void marqueeTick();
static void applyChosenMode();
static DisplayMode pickRandomAnim();

volatile bool cycleStart = false;      // BLE asked for Random Cycle
static bool cycleEnabled = false;      // loop-owned
static int cycleIndex = 0;             // current slot in CYCLE_MODES
static unsigned long cycleNextAt = 0;  // millis() of next hop
volatile bool configDirty = false;
volatile bool npDirty = false;

// Display settings persist across power cycles (NVS).
Preferences prefs;

// ==== Boids (MODE_BOIDS): fish on the round face ====
#define BOID_COUNT 24
struct Boid {
  float x, y, px, py, vx, vy;
  float hx, hy;              // heading (unit) that the body is drawn along
  float jx0, jy0, jdx, jh;   // jump arc: origin, horizontal drift, peak height
  int jumpT;                 // frames of airtime left; 0 = swimming
};
static Boid boids[BOID_COUNT];
static unsigned long lastBoidUpdate = 0;

// Fish tank: a water surface across the round face, rare food drops, and the
// occasional leap. Food triggers a frenzy: the school breaks formation and
// races for the nearest pellet, but the speed cap never changes -- none of
// them outswims the others, they only turn better.
#define WATER_Y 38                 // water surface (round face top is ~y=15)
#define JUMP_FRAMES 24             // 24 ticks x 50 ms = 1.2 s of airtime
#define FOOD_MAX 8
#define FOOD_EAT_R 7.0f
#define FOOD_FIRST_MS 5000UL       // first drop shortly after entering the mode
#define FOOD_MIN_MS 90000UL        // production: a pinch every 90-180 s
#define FOOD_JIT_MS 90000UL
#define SPLASH_MAX 4
#define SPLASH_FRAMES 12
static bool foodActive = false;
static int foodCount = 0;
static float foodX[FOOD_MAX], foodY[FOOD_MAX], foodVX[FOOD_MAX];
static unsigned long nextFoodAt = 0;
struct Splash { float x; int t; };
static Splash splashes[SPLASH_MAX];
static float wavePhase = 0.0f;   // scrolling surface phase

// ==== Langton's ant (MODE_ANT): two-colour highway-builder on the torus ====
#define ANT_BYTES ((LIFE_W * LIFE_H + 7) / 8)
static uint8_t antGrid[ANT_BYTES];       // classic binary turn state
static int antX, antY, antDir;   // dir: 0=N 1=E 2=S 3=W
static unsigned long lastAntUpdate = 0;

// ==== Fire (MODE_FIRE): forest-fire percolation (Drossel-Schwabl) ====
// Four states in 2 bits/cell: 0 bare ground, 1 tree, 2 burning, 3 ember.
// Trees sprout, lightning starts a strike, fire spreads to orthogonal
// neighbours, burns to embers, and the forest regrows behind it.
#define FF_W LIFE_W
#define FF_H LIFE_H
#define FF_BYTES ((FF_W * FF_H * 2 + 7) / 8)
#define FF_TICK_MS 120            // ~8 generations/sec
#define FF_IGNITE_PCT 72          // a tree next to fire catches (out of 100)
#define FF_GROW_PERMILLE 12       // an empty cell sprouts (out of 1000)
static uint8_t fireBufA[FF_BYTES], fireBufB[FF_BYTES];
static uint8_t* fireCur = fireBufA;
static uint8_t* fireNext = fireBufB;
static uint32_t ffCalmTicks = 0;      // ticks with no fire -> force a strike
static unsigned long lastFireUpdate = 0;
static void ffBuildPalette();             // fire palette follows the user colours
static void ffRepaint();
static uint32_t ffFlicker = 0;            // per-tick seed for flame flicker

// ==== Pipes (MODE_PIPES): phosphor 3D tube maze on a lattice ====
#define PIPE_COUNT 5
#define PIPE_N 7                     // lattice nodes per axis
#define PIPE_STEP_MS 240             // one segment per tick, round-robin (Win98 pace)
struct Pipe { int x, y, z, dx, dy, dz, steps, maxSteps; bool alive; };
static Pipe pipes[PIPE_COUNT];
static int pipesAlive = 0;
static uint8_t pipeTurn = 0;
static unsigned long lastPipeUpdate = 0;
static unsigned long pipesRespawnAt = 0;
static float pipeCosY, pipeSinY, pipeCosP, pipeSinP;
#define PIPE_SCALE 19.0f

// ==== Pet (MODE_RADAR slot): a small persistent goo creature ====
// Stats live in NVS so the pet keeps ageing across power cycles and modes.
#define TAMA_TICK_MS 110
static Preferences tamaPrefs;
static uint8_t tHunger = 80, tHappy = 70, tEnergy = 90, tPoops = 0;
static uint16_t tAgeMin = 0;
static bool tSleeping = false;
static unsigned long tLastTick = 0, tLastSave = 0;
static unsigned long tEatUntil = 0, tPlayUntil = 0, tSleepUntil = 0;
static unsigned long tBlinkAt = 0, tBlinkUntil = 0, tPoopAt = 0;
static uint32_t tHungerAcc = 0, tHappyAcc = 0, tEnergyAcc = 0, tMinuteAcc = 0;
static int16_t tOffX = 0;              // wander offset from centre
static int8_t tDir = 1;
static unsigned long tWanderAt = 0, tPlayCheckAt = 0, tPoopClearAt = 0;
// Goo rendering state: the pet is chunky pixel art on a 4px grid, not vector shapes.
#define GOO_CELL 4
static float tGooPhase = 0.0f;
static int8_t tDripRow = -1, tDripSide = 1;
static uint8_t tPuddlePulse = 0;
static inline int gooRowOff(int r) {
  return (int)lroundf(1.3f * sinf(r * 0.62f - tGooPhase));
}

// ==== Crystal (MODE_STARFIELD): diffusion-limited aggregation ====
// Kept in the old starfield slot; the app still calls it "starfield" over BLE.
#define DLA_W LIFE_W
#define DLA_H LIFE_H
static uint8_t dlaGrid[DLA_W * DLA_H];   // 0 empty, else crystal palette slot
static int16_t dlaCx = 60, dlaCy = 60;   // seed cell
static int16_t dlaR = 1;                 // deposit radius in cells
static uint16_t dlaCells = 0;
static float dlaWx = 0, dlaWy = 0;       // walker position, cell units
static bool dlaWalker = false;
static unsigned long lastDlaUpdate = 0, dlaResetAt = 0;

// 4x4 ordered-dither threshold, shared by the crystal gradient.
static const uint8_t BAYER4[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

// ==== Snake (MODE_OSC): autonomous snake, the pod tries to survive ====
// 40x40 cells at 4px = a 160px board centred on the round face (half-diagonal
// 113px, inside the ~118px visible radius). Torus wrap at the edges so the
// snake never dies against a bezel it cannot see.
#define SNK_W 40
#define SNK_H 40
#define SNK_CELL 4
#define SNK_X0 40                       // board origin in px
#define SNK_Y0 40
#define SNK_STEP_MS 110                 // ~9 cells/sec
#define SNK_MAX (SNK_W * SNK_H)
#define SNK_DEAD_HOLD 1400              // ms to sit on a dead snake

static uint8_t snkOcc[SNK_W * SNK_H];        // 1 = body
static uint8_t snkSegX[SNK_W * SNK_H], snkSegY[SNK_W * SNK_H];  // [0] = head
static int16_t snkLen = 0;
static int8_t snkDX = 1, snkDY = 0;
static uint8_t snkFX = 20, snkFY = 20;
static uint16_t snkBest = 0;
static unsigned long snkLast = 0, snkDeadAt = 0;

// ==== Rally (MODE_RALLY): autonomous rally, blocky digits ====
#define RALLY_TOP 54
#define RALLY_BOT 186
#define RALLY_PAD_H 30
#define RALLY_PAD_X_L 25
#define RALLY_PAD_X_R 215
struct RallyPaddle { float y, err; };
static RallyPaddle rallyL, rallyR;
static float rallyBallX, rallyBallY, rallyBallVX, rallyBallVY;
static int rallyScoreL = 0, rallyScoreR = 0, rallyServeDelay = 0, rallyFrame = 0;
static unsigned long lastRallyUpdate = 0;

// ==== Eye (MODE_EQ): pixel-art eyeball, bored most of the time ====
static uint8_t eyeSpecies = 0;              // 0 human, 1 cat, 2 goat, 3 robot
static float eyeGX = 0, eyeGY = 0;          // current gaze offset, px
static float eyeTargetX = 0, eyeTargetY = 0;
static float eyeLid = 1.0f;                 // 1 = open, 0 = shut
static bool eyeSnap = false;
static unsigned long lastEyeUpdate = 0;
static unsigned long eyeNextDriftAt = 0, eyeNextSnapAt = 0, eyeSnapEnd = 0;
static unsigned long eyeNextBlinkAt = 0, eyeBlinkEnd = 0, eyeNextSpeciesAt = 0;

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
void knightInit();
void updateKnight();
void coinInit();
void updateCoin();
void pipesInit();
void updatePipes();
void radarInit();
void updateTama();
void dlaInit();
void updateDla();
void snkInit();
void updateSnake();
void rallyInit();
void updateRally();
void eyeInit();
void updateEye();
void renderDisplay();
void setDisplayMode(DisplayMode mode, bool remember = false);
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
      if (strcmp(anim, "marquee") == 0)          { mpTestOnePass = true; }   // indicator test, not a mode
      else if (strcmp(anim, "pipes") == 0)       { pendingMode = MODE_PIPES;       modeDirty = true; }
      else if (strcmp(anim, "coin") == 0 || strcmp(anim, "dozer") == 0 || strcmp(anim, "coins") == 0) { pendingMode = MODE_COIN; modeDirty = true; }
      else if (strcmp(anim, "static") == 0)      { pendingMode = MODE_STATIC;      modeDirty = true; }
      else if (strcmp(anim, "now_playing") == 0) { pendingMode = MODE_NOW_PLAYING; modeDirty = true; }
      else if (strcmp(anim, "boids") == 0)       { pendingMode = MODE_BOIDS;       modeDirty = true; }
      else if (strcmp(anim, "ant") == 0)         { pendingMode = MODE_ANT;         modeDirty = true; }
      else if (strcmp(anim, "fire") == 0)        { pendingMode = MODE_FIRE;        modeDirty = true; }
      else if (strcmp(anim, "knight") == 0 || strcmp(anim, "run") == 0 || strcmp(anim, "jump") == 0) { pendingMode = MODE_KNIGHT; modeDirty = true; }
      else if (strcmp(anim, "radar") == 0 || strcmp(anim, "pet") == 0) { pendingMode = MODE_RADAR; modeDirty = true; }
      else if (strcmp(anim, "starfield") == 0 || strcmp(anim, "dla") == 0 || strcmp(anim, "crystal") == 0) { pendingMode = MODE_STARFIELD; modeDirty = true; }
      else if (strcmp(anim, "oscilloscope") == 0 || strcmp(anim, "osc") == 0 || strcmp(anim, "snake") == 0) { pendingMode = MODE_OSC; modeDirty = true; }
      else if ((strcmp(anim, "rally") == 0 || strcmp(anim, "pong") == 0))        { pendingMode = MODE_RALLY;        modeDirty = true; }
      else if (strcmp(anim, "eq") == 0 || strcmp(anim, "eye") == 0 || strcmp(anim, "eyeball") == 0) { pendingMode = MODE_EQ; modeDirty = true; }
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
      const char* hex2 = doc["accent_color"] | "";
      if (hex2[0] == '#' && strlen(hex2) == 7) {
        long rgb2 = strtol(hex2 + 1, nullptr, 16);
        accR = (rgb2 >> 16) & 0xFF;
        accG = (rgb2 >> 8) & 0xFF;
        accB = rgb2 & 0xFF;
      }
      configDirty = true;  // deferred: loop task calls applyDisplayColor()
      Serial.printf("[BLE] Config applied: brightness %d, primary #%02X%02X%02X accent #%02X%02X%02X\n",
                    brightnessOffset, baseR, baseG, baseB, accR, accG, accB);
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
        (DisplayMode)prefs.getUChar("mode", (uint8_t)MODE_STATIC);
    if (savedMode == MODE_INDICATOR || (uint8_t)savedMode > (uint8_t)MODE_PIPES)
      savedMode = MODE_STATIC;
    displayMode = savedMode;
    if (prefs.getBool("cycle", false)) {      // restore Random Cycle across reboots
      cycleEnabled = true;
      cycleIndex = 0;
      for (int i = 0; i < CYCLE_N; i++)
        if (CYCLE_MODES[i] == savedMode) { cycleIndex = i; break; }
      cycleNextAt = millis() + CYCLE_MS;
      Serial.println("[NVS] restored Random Cycle");
    }
    brightnessOffset = prefs.getInt("bright", 0);
    baseR = prefs.getUChar("cr", 0xFF);
    baseG = prefs.getUChar("cg", 0xBF);
    baseB = prefs.getUChar("cb", 0x00);
    accR  = prefs.getUChar("ar", 0xFF);
    accG  = prefs.getUChar("ag", 0xFF);
    accB  = prefs.getUChar("ab", 0xFF);
  }
  applyDisplayColor();
  // Always boot on the indicator: 5 minutes of marquee, or until the app links.
  mpPhase = MP_STARTUP;
  mpSince = millis();
  setDisplayMode(MODE_INDICATOR);

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
  if (modeDirty) { modeDirty = false; cycleEnabled = false; setDisplayMode(pendingMode, true); }
  marqueeTick();
  if (cycleEnabled && mpPhase == MP_ANIM && millis() >= cycleNextAt) {   // hop to a fresh animation
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
    if (activeMode == MODE_NOW_PLAYING) drawNowPlaying();
    else if (activeMode == MODE_STATIC) lifeRepaint();
  }
  if (npDirty) {
    npDirty = false;
    if (activeMode == MODE_NOW_PLAYING) drawNowPlaying();
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

  // Shared sprite: 4bpp palette (0=black, 1=primary, 2=accent). Full-frame
  // push stays flicker-free, and the palette gives every animation a second
  // color. 240x240 @ 4bpp = ~28 KB.
  lifeSpr.setColorDepth(4);
  lifeSpr.createSprite(LIFE_W * LIFE_CELL, LIFE_H * LIFE_CELL);
  lifeSpr.setPaletteColor(0, TFT_BLACK);
  lifeSpr.setPaletteColor(1, drawColor);
  lifeSpr.setPaletteColor(2, accentColor);
  lifeSpr.setPaletteColor(3, tft.color565(40, 150, 235));  // water blue
  lifeSpr.setPaletteColor(4, tft.color565(10, 55, 105));   // water depth band
  lifeSpr.setPaletteColor(5, tft.color565(40, 170, 70));    // forest tree
  lifeSpr.setPaletteColor(6, tft.color565(255, 235, 150));  // flame core
  lifeSpr.setPaletteColor(7, tft.color565(255, 150, 25));   // flame mid
  lifeSpr.setPaletteColor(8, tft.color565(225, 70, 10));    // flame deep
  lifeSpr.setPaletteColor(9, tft.color565(110, 25, 10));    // ember
  lifeSpr.setPaletteColor(10, tft.color565(45, 40, 38));    // ash
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
  if (marqueePos >= repeatPeriod) {
    marqueePos -= repeatPeriod;
    if (mpPhase == MP_PASS) mpPassDone = true;   // one full pass = one cycle
  }
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
  int ar = (int)(accR * scale); if (ar > 255) ar = 255; if (ar < 0) ar = 0;
  int ag = (int)(accG * scale); if (ag > 255) ag = 255; if (ag < 0) ag = 0;
  int ab = (int)(accB * scale); if (ab > 255) ab = 255; if (ab < 0) ab = 0;
  accentColor = tft.color565(ar, ag, ab);
  tft.setTextColor(drawColor);
  if (lifeSpriteReady) {
    lifeSpr.setPaletteColor(0, TFT_BLACK);
    lifeSpr.setPaletteColor(1, drawColor);
    lifeSpr.setPaletteColor(2, accentColor);
    ffBuildPalette();
    if (activeMode == MODE_FIRE) ffRepaint();
  }
  Serial.printf("[TFT] draw color 0x%04X accent 0x%04X\n", drawColor, accentColor);
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
  prefs.putBool("cycle", cycleEnabled);
  prefs.putInt("bright", brightnessOffset);
  prefs.putUChar("cr", baseR);
  prefs.putUChar("cg", baseG);
  prefs.putUChar("cb", baseB);
  prefs.putUChar("ar", accR);
  prefs.putUChar("ag", accG);
  prefs.putUChar("ab", accB);
}

void setDisplayMode(DisplayMode mode, bool remember) {
  activeMode = mode;
  if (remember) {
    displayMode = mode;
    saveDisplaySettings();
  }
  marqueePos = 0;
  lastMarqueeUpdate = 0;
  tft.fillScreen(TFT_BLACK);
  if (mode == MODE_NOW_PLAYING) {
    drawNowPlaying();
  } else if (mode == MODE_INDICATOR) {
    // drawNowPlaying() changes font/size/datum; restore ticker settings.
    tft.setTextFont(1);
    tft.setTextSize(4);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(drawColor);
  } else if (mode == MODE_PIPES) {
    pipesInit();
  } else if (mode == MODE_COIN) {
    coinInit();
  } else if (mode == MODE_STATIC) {
    lifeSeed();
  } else if (mode == MODE_BOIDS) {
    boidInit();
  } else if (mode == MODE_ANT) {
    antInit();
  } else if (mode == MODE_FIRE) {
    fireInit();
  } else if (mode == MODE_KNIGHT) {
    knightInit();
  } else if (mode == MODE_RADAR) {
    radarInit();
  } else if (mode == MODE_STARFIELD) {
    dlaInit();
  } else if (mode == MODE_OSC) {
    snkInit();
  } else if (mode == MODE_RALLY) {
    rallyInit();
  } else if (mode == MODE_EQ) {
    eyeInit();
  }
  Serial.printf("[TFT] Display mode -> %d\n", (int)mode);
}

// ---- marquee indicator logic ----
static DisplayMode pickRandomAnim() {
  DisplayMode pick = CYCLE_MODES[random(CYCLE_N)];
  // "Now Playing" is meaningless with no phone attached: substitute something.
  for (int i = 0; i < 24 && !deviceConnected && pick == MODE_NOW_PLAYING; i++)
    pick = CYCLE_MODES[random(CYCLE_N)];
  if (!deviceConnected && pick == MODE_NOW_PLAYING) pick = MODE_STATIC;
  return pick;
}

static void applyChosenMode() {
  if (cycleEnabled) {
    setDisplayMode(pickRandomAnim());
    cycleNextAt = millis() + CYCLE_MS;
  } else if (!deviceConnected && displayMode == MODE_NOW_PLAYING) {
    setDisplayMode(pickRandomAnim());
  } else {
    setDisplayMode(displayMode);
  }
}

void marqueeTick() {
  const unsigned long now = millis();

  if (mpTestOnePass) {                       // BLE asked for a single pass
    mpTestOnePass = false;
    mpPhase = MP_PASS;
    mpSince = now;
    mpPassDone = false;
    setDisplayMode(MODE_INDICATOR);
    return;
  }

  if (deviceConnected && !mpWasLinked) {     // app attached: indicator is over
    mpWasLinked = true;
    mpPhase = MP_ANIM;
    mpSince = now;
    Serial.println("[MARQ] linked -> chosen animation");
    applyChosenMode();
    return;
  }
  if (!deviceConnected && mpWasLinked) {     // app went away: start the clock again
    mpWasLinked = false;
    mpPhase = MP_STARTUP;
    mpSince = now;
    Serial.println("[MARQ] unlinked -> marquee (5 min window)");
    setDisplayMode(MODE_INDICATOR);
    return;
  }
  if (deviceConnected) return;               // the app owns the display

  switch (mpPhase) {
    case MP_STARTUP:
      if (now - mpSince >= MARQUEE_STARTUP_MS) {
        mpPhase = MP_ANIM;
        mpSince = now;
        Serial.println("[MARQ] startup window done -> chosen animation");
        applyChosenMode();
      }
      break;
    case MP_ANIM:
      if (now - mpSince >= MARQUEE_EVERY_MS) {
        mpPhase = MP_PASS;
        mpSince = now;
        mpPassDone = false;
        Serial.println("[MARQ] 10 min unlinked -> one marquee pass");
        setDisplayMode(MODE_INDICATOR);
      }
      break;
    case MP_PASS:
      if (mpPassDone) {
        mpPhase = MP_ANIM;
        mpSince = now;
        Serial.println("[MARQ] pass done -> back to animation");
        applyChosenMode();
      }
      break;
  }
}

// 2 bits per cell: 0 = empty, 1 = primary species, 2 = secondary species.
static inline uint8_t lifeGet(const uint8_t* buf, int x, int y) {
  uint32_t bit = ((uint32_t)y * LIFE_W + x) * 2;
  return (buf[bit >> 3] >> (bit & 7)) & 3;
}

static inline void lifeSet(uint8_t* buf, int x, int y, uint8_t v) {
  uint32_t bit = ((uint32_t)y * LIFE_W + x) * 2;
  const uint8_t sh = (uint8_t)(bit & 7);
  const uint8_t mask = (uint8_t)(3u << sh);
  buf[bit >> 3] = (uint8_t)((buf[bit >> 3] & ~mask) | ((v & 3) << sh));
}

void lifeSeed() {
  randomSeed(esp_random());
  memset(lifeCur, 0, LIFE_BYTES);
  lifeSpr.fillSprite(0);
  for (int y = 0; y < LIFE_H; y++) {
    for (int x = 0; x < LIFE_W; x++) {
      if (random(100) < 22) {
        const uint8_t species = (random(100) < 60) ? 1 : 2;
        lifeSet(lifeCur, x, y, species);
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, species);
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

  // Pending predation conversions (applied after the main pass so the rule
  // always reads the previous generation, never a half-updated field).
  static int16_t eatX[128], eatY[128];

  uint32_t changed = 0;
  uint32_t pop1 = 0, pop2 = 0;
  uint32_t sigHash = 0;   // FNV-1a fold of live-cell coordinates
  int nEat = 0;
  for (int y = 0; y < LIFE_H; y++) {
    const int ym = (y == 0) ? LIFE_H - 1 : y - 1;
    const int yp = (y == LIFE_H - 1) ? 0 : y + 1;
    for (int x = 0; x < LIFE_W; x++) {
      const int xm = (x == 0) ? LIFE_W - 1 : x - 1;
      const int xp = (x == LIFE_W - 1) ? 0 : x + 1;
      int n1 = 0, n2 = 0;
      {
        const uint8_t v0 = lifeGet(lifeCur, xm, ym);
        const uint8_t v1 = lifeGet(lifeCur, x,  ym);
        const uint8_t v2 = lifeGet(lifeCur, xp, ym);
        const uint8_t v3 = lifeGet(lifeCur, xm, y );
        const uint8_t v4 = lifeGet(lifeCur, xp, y );
        const uint8_t v5 = lifeGet(lifeCur, xm, yp);
        const uint8_t v6 = lifeGet(lifeCur, x,  yp);
        const uint8_t v7 = lifeGet(lifeCur, xp, yp);
        n1 = (v0 == 1) + (v1 == 1) + (v2 == 1) + (v3 == 1) +
             (v4 == 1) + (v5 == 1) + (v6 == 1) + (v7 == 1);
        n2 = (v0 == 2) + (v1 == 2) + (v2 == 2) + (v3 == 2) +
             (v4 == 2) + (v5 == 2) + (v6 == 2) + (v7 == 2);
      }
      const uint8_t cur = lifeGet(lifeCur, x, y);
      uint8_t next = 0;
      if (cur == 1) {
        next = (n1 == 2 || n1 == 3) ? 1 : 0;
      } else if (cur == 2) {
        next = (n2 == 2 || n2 == 3) ? 2 : 0;
        // Predation: a well-supported predator eats an adjacent prey cell.
        if (n2 >= 3 && nEat < 128 && random(100) < 15) {
          const int cx[8] = {xm, x, xp, xm, xp, xm, x, xp};
          const int cy[8] = {ym, ym, ym, y, y, yp, yp, yp};
          for (int k = 0; k < 8; k++) {
            if (lifeGet(lifeCur, cx[k], cy[k]) == 1) {
              eatX[nEat] = (int16_t)cx[k];
              eatY[nEat] = (int16_t)cy[k];
              nEat++;
              break;
            }
          }
        }
      } else {
        const bool b1 = (n1 == 3);
        const bool b2 = (n2 == 3);
        if (b1 && b2) next = random(2) ? 1 : 2;
        else if (b1)  next = 1;
        else if (b2)  next = 2;
      }
      lifeSet(lifeNext, x, y, next);
      if (next) {
        if (next == 1) pop1++; else pop2++;
        sigHash ^= (uint32_t)(y * LIFE_W + x);
        sigHash *= 0x01000193u;   // FNV-1a prime
      }
      if (next != cur) {
        changed++;
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, next);
      }
    }
  }
  // Apply predation conversions to the new generation.
  for (int i = 0; i < nEat; i++) {
    const int x = eatX[i], y = eatY[i];
    const uint8_t was = lifeGet(lifeNext, x, y);
    if (was == 1) {
      lifeSet(lifeNext, x, y, 2);
      pop1--;
      pop2++;
      changed++;
      sigHash ^= (uint32_t)(y * LIFE_W + x);
      sigHash *= 0x01000193u;
      lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, 2);
    }
  }
  uint8_t* tmp = lifeCur; lifeCur = lifeNext; lifeNext = tmp;
  lifeSpr.pushSprite(0, 0);

  // Roll the stagnation fingerprint. If 2 of the last 3 generations have the
  // same (count, hash), the field is repeating a pattern (still life, blinker,
  // traffic light) — reseed. Moving structures (gliders, predators crawling a
  // border) keep changing their hash, so they survive.
  const uint32_t totalPop = pop1 + pop2;
  lifeSigCount[lifeSigFill % LIFE_SIG_SLOTS] = (uint16_t)totalPop;
  lifeSigHash[lifeSigFill % LIFE_SIG_SLOTS] = sigHash;
  lifeSigFill++;
  if (lifeSigFill >= LIFE_SIG_SLOTS) {
    const uint32_t b = (lifeSigFill - 1) % LIFE_SIG_SLOTS;  // newest
    const uint32_t a = (lifeSigFill - 2) % LIFE_SIG_SLOTS;
    const uint32_t c = (lifeSigFill - 3) % LIFE_SIG_SLOTS;
    const bool repeat =
        (lifeSigCount[a] == lifeSigCount[b] && lifeSigHash[a] == lifeSigHash[b]) ||
        (lifeSigCount[b] == lifeSigCount[c] && lifeSigHash[b] == lifeSigHash[c]) ||
        (lifeSigCount[a] == lifeSigCount[c] && lifeSigHash[a] == lifeSigHash[c]);
    if (repeat) {
      lifeSeed();
      return;
    }
  }

  // Soup died or collapsed below the watchable threshold.
  const uint32_t minPop = ((uint32_t)LIFE_W * LIFE_H * LIFE_MIN_POP_PCT) / 100;
  if (changed == 0 || totalPop < minPop || pop1 == 0 || pop2 == 0) lifeSeed();
}

void lifeRepaint() {
  // Repaint the sprite from the current field with the current color.
  if (!lifeSpriteReady) return;
  lifeSpr.fillSprite(0);
  for (int y = 0; y < LIFE_H; y++) {
    for (int x = 0; x < LIFE_W; x++) {
      const uint8_t v = lifeGet(lifeCur, x, y);
      if (v) {
        lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL, v);
      }
    }
  }
  lifeSpr.pushSprite(0, 0);
}

// ==== Boids (MODE_BOIDS) ====
// ==== Boids (MODE_BOIDS) ====
void boidInit() {
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];
    b->x = 120.0f + (random(81) - 40);
    b->y = 112.0f + (random(71) - 35);
    b->px = b->x;
    b->py = b->y;
    float a = random(0, 628) / 100.0f;
    float v = 1.2f + random(100) / 100.0f;
    b->vx = cosf(a) * v;
    b->vy = sinf(a) * v;
    b->hx = cosf(a); b->hy = sinf(a);
    b->jumpT = 0;
    b->jx0 = b->x; b->jy0 = b->y; b->jdx = 0; b->jh = 0;
  }
  foodCount = 0;
  foodActive = false;
  nextFoodAt = millis() + FOOD_FIRST_MS;
  for (int i = 0; i < SPLASH_MAX; i++) splashes[i].t = 0;
}

// Drop a pinch of pellets just under the surface; they sink and drift apart.
void spawnFishFood() {
  foodCount = 3 + random(4);                 // 3-6 pellets
  if (foodCount > FOOD_MAX) foodCount = FOOD_MAX;
  const float fx0 = 60.0f + random(120);
  for (int k = 0; k < foodCount; k++) {
    foodX[k] = fx0 + (float)(random(25) - 12);
    foodY[k] = (float)(WATER_Y + 2 + random(4));
    foodVX[k] = (float)(random(9) - 4) / 20.0f;
  }
  foodActive = true;
}

void addSplash(float x) {
  for (int i = 0; i < SPLASH_MAX; i++) {
    if (splashes[i].t == 0) { splashes[i].x = x; splashes[i].t = SPLASH_FRAMES; return; }
  }
}

// Local surface height: two sine components scrolling at different rates so
// the waterline rolls instead of sitting flat.
static inline int waveYAt(int x) {
  return WATER_Y + (int)(1.6f * sinf(x * 0.17f + wavePhase)
                       + 0.9f * sinf(x * 0.41f - wavePhase * 0.7f));
}

void updateBoids() {
  unsigned long now = millis();
  if (now - lastBoidUpdate < 50) return;
  lastBoidUpdate = now;

  wavePhase += 0.09f;
  if (wavePhase > 628.3185f) wavePhase -= 628.3185f;  // keep float precision

  if (!foodActive && (long)(now - nextFoodAt) >= 0) spawnFishFood();

  const float cx = 120.0f, cy = 120.0f, maxR = 105.0f;
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];

    // Airborne: ride the jump arc; no flocking, no waterline, no food.
    if (b->jumpT > 0) {
      b->jumpT--;
      const float p = (float)(JUMP_FRAMES - b->jumpT) / (float)JUMP_FRAMES;
      b->px = b->x; b->py = b->y;
      b->x = b->jx0 + b->jdx * p;
      b->y = b->jy0 - b->jh * 4.0f * p * (1.0f - p);
      b->vx = b->jdx;
      b->vy = -b->jh * (1.0f - 2.0f * p);
      // Keep the leap on the round face.
      float jdx2 = b->x - 120.0f, jdy2 = b->y - 120.0f;
      float jr = sqrtf(jdx2 * jdx2 + jdy2 * jdy2);
      if (jr > 110.0f) { b->x = 120.0f + jdx2 / jr * 110.0f; b->y = 120.0f + jdy2 / jr * 110.0f; }
      if (b->jumpT == 0) {
        addSplash(b->x);
        b->y = (float)(WATER_Y + 6);
        b->vx = b->jdx * 0.8f;
        b->vy = 1.4f;
      }
      continue;
    }

    float ax = 0, ay = 0, cxs = 0, cys = 0, vxs = 0, vys = 0;
    int neighbors = 0;
    for (int j = 0; j < BOID_COUNT; j++) {
      if (j == i) continue;
      Boid* o = &boids[j];
      if (o->jumpT > 0) continue;             // airborne fish don't school
      float dx = o->x - b->x, dy = o->y - b->y;
      float d2 = dx * dx + dy * dy;
      if (d2 > 48.0f * 48.0f) continue;
      neighbors++;
      cxs += o->x; cys += o->y;
      vxs += o->vx; vys += o->vy;
      if (d2 < 14.0f * 14.0f) { ax -= dx; ay -= dy; }
    }
    if (neighbors > 0) {
      const float coh = foodActive ? 0.0025f : 0.006f;
      const float ali = foodActive ? 0.01f : 0.05f;
      ax += (cxs / neighbors - b->x) * coh;
      ay += (cys / neighbors - b->y) * coh;
      ax += (vxs / neighbors - b->vx) * ali;
      ay += (vys / neighbors - b->vy) * ali;
    }
    // Stay inside the round face.
    float dx = b->x - cx, dy = b->y - cy;
    float r = sqrtf(dx * dx + dy * dy);
    if (r > maxR) { ax -= (dx / r) * 0.15f; ay -= (dy / r) * 0.15f; }

    // Stay under the waterline.
    if (b->y < WATER_Y + 6) ay += ((WATER_Y + 6) - b->y) * 0.06f;

    // Frenzy: steer hard at the nearest pellet. Speed cap is unchanged, so
    // this is a fair race -- the panic is in the turning, not the top speed.
    if (foodActive && foodCount > 0) {
      float bestD2 = 1e9f; int bestK = -1;
      for (int k = 0; k < foodCount; k++) {
        float fdx = foodX[k] - b->x, fdy = foodY[k] - b->y;
        float fd2 = fdx * fdx + fdy * fdy;
        if (fd2 < bestD2) { bestD2 = fd2; bestK = k; }
      }
      if (bestK >= 0) {
        float fd = sqrtf(bestD2);
        if (fd > 0.5f) {
          float fa = fd * 0.05f;
          if (fa > 0.35f) fa = 0.35f;
          ax += (foodX[bestK] - b->x) / fd * fa;
          ay += (foodY[bestK] - b->y) / fd * fa;
        }
      }
    }

    // Occasional leap: only near the surface, more likely during a frenzy.
    if (b->y < WATER_Y + 12) {
      const int chance = foodActive ? 400 : 1200;
      if (random(chance) == 0) {
        b->jumpT = JUMP_FRAMES;
        b->jx0 = b->x;
        b->jy0 = b->y;
        b->jdx = (float)(random(41) - 20) * 0.8f;
        b->jh = 16.0f + random(7);
        if (b->jy0 - b->jh < 20.0f) b->jh = b->jy0 - 20.0f;
        if (b->jh < 5.0f) b->jh = 5.0f;
        // Airborne heading: frozen for the whole flight at the takeoff angle,
        // so the fish reads as a thrown body instead of one still swimming.
        // Clamp to 7..57 degrees nose-up; steeper than that looks like a rocket.
        float la = atan2f(-4.0f * b->jh, b->jdx);
        if (la > -0.12f) la = -0.12f;
        if (la < -1.00f) la = -1.00f;
        b->hx = cosf(la); b->hy = sinf(la);
      }
    }

    b->vx += ax; b->vy += ay;
    float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
    if (sp > 2.2f) {
      b->vx = b->vx / sp * 2.2f; b->vy = b->vy / sp * 2.2f;
    } else if (sp < 1.0f && sp > 0.001f) {
      b->vx = b->vx / sp; b->vy = b->vy / sp;
    }

    // Swimming: the body follows the swim direction.
    {
      float hs = sqrtf(b->vx * b->vx + b->vy * b->vy);
      if (hs > 0.001f) { b->hx = b->vx / hs; b->hy = b->vy / hs; }
    }

    b->px = b->x; b->py = b->y;
    b->x += b->vx; b->y += b->vy;
    if (b->x < 0) b->x = 0; if (b->x > 239) b->x = 239;
    if (b->y < 0) b->y = 0; if (b->y > 239) b->y = 239;
  }

  // Pellets sink; a fish inside the eat radius takes it.
  for (int k = 0; k < foodCount; ) {
    foodY[k] += 0.5f;
    foodX[k] += foodVX[k];
    bool eaten = false;
    for (int i = 0; i < BOID_COUNT && !eaten; i++) {
      Boid* b = &boids[i];
      if (b->jumpT > 0) continue;
      float ddx = b->x - foodX[k], ddy = b->y - foodY[k];
      if (ddx * ddx + ddy * ddy < FOOD_EAT_R * FOOD_EAT_R) eaten = true;
    }
    if (eaten || foodY[k] > 232.0f) {
      foodX[k] = foodX[foodCount - 1];
      foodY[k] = foodY[foodCount - 1];
      foodVX[k] = foodVX[foodCount - 1];
      foodCount--;
      continue;
    }
    k++;
  }
  if (foodActive && foodCount == 0) {
    foodActive = false;
    nextFoodAt = now + FOOD_MIN_MS + random(FOOD_JIT_MS);
  }

  for (int i = 0; i < SPLASH_MAX; i++) {
    if (splashes[i].t > 0) splashes[i].t--;
  }

  lifeSpr.fillSprite(0);

  // Water surface: animated sine chord inside the round face, dark band under
  // the crest for depth, plus a couple of flickering glints.
  {
    const int dyw = WATER_Y - 120;
    const int hw = (int)sqrtf((float)(105 * 105 - dyw * dyw));
    for (int x = 120 - hw; x < 120 + hw; x++) {
      const int yy = waveYAt(x);
      lifeSpr.fillRect(x, yy, 1, 1, 3);
      lifeSpr.fillRect(x, yy + 1, 1, 2, 4);
    }
    for (int g = 0; g < 2; g++) {
      if (random(4) == 0) {
        const int gx = 120 - hw + random(2 * hw - 4);
        lifeSpr.fillRect(gx, waveYAt(gx), 2 + random(2), 1, 2);
      }
    }
  }
  // Splash ripples ride the local wave height.
  for (int i = 0; i < SPLASH_MAX; i++) {
    if (splashes[i].t <= 0) continue;
    const int age = SPLASH_FRAMES - splashes[i].t;
    const int rad = 2 + age;
    const int sx = (int)splashes[i].x;
    lifeSpr.fillRect(sx - rad, waveYAt(sx) - 1, rad * 2, 1, 3);
  }
  // Food.
  for (int k = 0; k < foodCount; k++) {
    lifeSpr.fillRect((int)foodX[k] - 1, (int)foodY[k] - 1, 2, 2, 2);
  }
  // Fish.
  for (int i = 0; i < BOID_COUNT; i++) {
    Boid* b = &boids[i];
    // Drawn along the stored heading, not the velocity. In the air the
    // velocity vector sweeps through the entire arc, which is what made the
    // leap read as the fish swimming through air.
    float fx = b->hx, fy = b->hy;
    float px = -fy, py = fx;
    const float L = 5.0f, W = 2.8f;
    int tipx = (int)lroundf(b->x + fx * L);
    int tipy = (int)lroundf(b->y + fy * L);
    int lx = (int)lroundf(b->x - fx * 1.5f + px * W);
    int ly = (int)lroundf(b->y - fy * 1.5f + py * W);
    int rx = (int)lroundf(b->x - fx * 1.5f - px * W);
    int ry = (int)lroundf(b->y - fy * 1.5f - py * W);
    lifeSpr.fillTriangle(tipx, tipy, lx, ly, rx, ry, (i % 5 == 0) ? 2 : 1);
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
    // The flipped binary tile state is also the visible two-colour state;
    // untouched cells remain black. Classic turn rule above is unchanged.
    lifeSpr.fillRect(antX * LIFE_CELL, antY * LIFE_CELL, LIFE_CELL, LIFE_CELL,
                     antGet(antX, antY) ? 2 : 1);
    switch (antDir) {
      case 0: antY = (antY == 0) ? LIFE_H - 1 : antY - 1; break;
      case 1: antX = (antX == LIFE_W - 1) ? 0 : antX + 1; break;
      case 2: antY = (antY == LIFE_H - 1) ? 0 : antY + 1; break;
      default: antX = (antX == 0) ? LIFE_W - 1 : antX - 1; break;
    }
  }
  // Show the ant itself at the head of its trail (self-corrects next flip).
  // Accent color so the ant reads apart from its own trail.
  lifeSpr.fillRect(antX * LIFE_CELL, antY * LIFE_CELL, LIFE_CELL, LIFE_CELL, 2);
  lifeSpr.pushSprite(0, 0);
}


// ==== Fire (MODE_FIRE) ====
static inline uint8_t ffGet(const uint8_t* buf, int x, int y) {
  uint32_t bit = ((uint32_t)y * FF_W + x) * 2;
  return (buf[bit >> 3] >> (bit & 7)) & 3;
}

static inline void ffSet(uint8_t* buf, int x, int y, uint8_t v) {
  uint32_t bit = ((uint32_t)y * FF_W + x) * 2;
  const uint8_t sh = (uint8_t)(bit & 7);
  const uint8_t mask = (uint8_t)(3u << sh);
  buf[bit >> 3] = (uint8_t)((buf[bit >> 3] & ~mask) | ((v & 3) << sh));
}

// Palette slots 5..10 belong to this mode: 5 tree, 6 flame core, 7 flame mid,
// 8 flame deep, 9 ember, 10 ash. Three things vary the colour:
//   1. each burning cell picks its slot from how much fire surrounds it (the
//      inside of a front reads pale-hot, the edge reads deep red),
//   2. a per-cell flicker hash re-rolls that every tick so the front moves,
//   3. a theme retints the whole set every ~90 s so a fire tonight is not a
//      photograph of the fire from ten minutes ago.
// Slots 5..10 are built from the two colours the user picked, same as every
// other mode: primary is the cool end (deep flame, embers, charred ash), the
// secondary is the hot core. No fixed oranges anywhere.
static uint16_t ffDim565(uint16_t c, float k) {
  int r = (int)((float)((c >> 11) & 0x1F) * k);
  int g = (int)((float)((c >> 5) & 0x3F) * k);
  int b = (int)((float)(c & 0x1F) * k);
  if (r > 31) r = 31; if (g > 63) g = 63; if (b > 31) b = 31;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t ffMix565(uint16_t a, uint16_t b, float t) {
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  return (uint16_t)(((int)(ar + (br - ar) * t) << 11) |
                    ((int)(ag + (bg - ag) * t) << 5) |
                     (int)(ab + (bb - ab) * t));
}

static void ffBuildPalette() {
  const uint16_t p = drawColor, s = accentColor;
  lifeSpr.setPaletteColor(5,  ffDim565(p, 0.32f));      // tree: dim primary
  lifeSpr.setPaletteColor(6,  s);                       // flame core: secondary
  lifeSpr.setPaletteColor(7,  ffMix565(p, s, 0.62f));   // flame mid
  lifeSpr.setPaletteColor(8,  p);                       // flame deep: primary
  lifeSpr.setPaletteColor(9,  ffDim565(p, 0.45f));      // ember
  lifeSpr.setPaletteColor(10, ffDim565(p, 0.16f));      // ash
}

static inline uint32_t ffHash(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
               (ffFlicker & 0x3FFu) * 2654435761u;
  h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
  return h;
}

// How much fire is wrapped around this cell: 0 at the leading edge, 2+ inside.
static inline int ffHeat(int x, int y) {
  const int xm = (x == 0) ? FF_W - 1 : x - 1;
  const int xp = (x == FF_W - 1) ? 0 : x + 1;
  const int ym = (y == 0) ? FF_H - 1 : y - 1;
  const int yp = (y == FF_H - 1) ? 0 : y + 1;
  int n = 0;
  n += (ffGet(fireCur, xm, y) == 2);
  n += (ffGet(fireCur, xp, y) == 2);
  n += (ffGet(fireCur, x, ym) == 2);
  n += (ffGet(fireCur, x, yp) == 2);
  return n;
}

static uint8_t ffColorAt(uint8_t st, int x, int y) {
  if (st == 1) return 5;                                   // tree
  if (st == 3) return (ffHash(x, y) & 1u) ? 9 : 10;        // ember / ash
  if (st != 2) return 0;                                   // ground
  const uint32_t h = ffHash(x, y);
  const int heat = ffHeat(x, y);
  int t = (heat >= 2) ? 2 : (heat == 1 ? 1 : 0);           // 0 deep .. 2 core
  if ((h & 7u) == 0) t++;                                  // bright lick
  if ((h & 15u) == 1) t--;                                 // cool gap
  if (t < 0) t = 0;
  if (t > 2) t = 2;
  return (uint8_t)(6 + (2 - t));                           // 6 core, 7 mid, 8 deep
}

static inline void ffDraw(int x, int y, uint8_t st) {
  lifeSpr.fillRect(x * LIFE_CELL, y * LIFE_CELL, LIFE_CELL, LIFE_CELL,
                   ffColorAt(st, x, y));
}

static void ffPlant(int x, int y, uint8_t st) {
  ffSet(fireCur, x, y, st);
  ffDraw(x, y, st);
}

// Full redraw, used when a theme retints the palette.
static void ffRepaint() {
  lifeSpr.fillSprite(0);
  for (int y = 0; y < FF_H; y++)
    for (int x = 0; x < FF_W; x++) {
      const uint8_t st = ffGet(fireCur, x, y);
      if (st) ffDraw(x, y, st);
    }
  lifeSpr.pushSprite(0, 0);
}

void fireInit() {
  randomSeed(esp_random());
  memset(fireCur, 0, FF_BYTES);
  memset(fireNext, 0, FF_BYTES);
  lifeSpr.fillSprite(0);
  ffFlicker = 0;
  ffBuildPalette();
  // A fairly mature forest, with a couple of fires already going.
  for (int y = 0; y < FF_H; y++) {
    for (int x = 0; x < FF_W; x++) {
      if (random(100) < 55) ffPlant(x, y, 1);
    }
  }
  for (int i = 0; i < 3; i++) {
    int x = random(FF_W), y = random(FF_H);
    ffPlant(x, y, 2);
  }
  ffCalmTicks = 0;
  lastFireUpdate = millis();
  lifeSpr.pushSprite(0, 0);
}

void updateFire() {
  unsigned long now = millis();
  if (now - lastFireUpdate < FF_TICK_MS) return;
  lastFireUpdate = now;
  ffFlicker++;

  uint32_t burning = 0;
  for (int y = 0; y < FF_H; y++) {
    for (int x = 0; x < FF_W; x++) {
      const uint8_t cur = ffGet(fireCur, x, y);
      uint8_t nxt = cur;

      if (cur == 2) {
        nxt = 3;                                  // burning -> ember
      } else if (cur == 3) {
        nxt = 0;                                  // ember -> bare ground
      } else if (cur == 1) {
        const int xm = (x == 0) ? FF_W - 1 : x - 1;
        const int xp = (x == FF_W - 1) ? 0 : x + 1;
        const int ym = (y == 0) ? FF_H - 1 : y - 1;
        const int yp = (y == FF_H - 1) ? 0 : y + 1;
        const bool nearFire = (ffGet(fireCur, xm, y) == 2) ||
                              (ffGet(fireCur, xp, y) == 2) ||
                              (ffGet(fireCur, x, ym) == 2) ||
                              (ffGet(fireCur, x, yp) == 2);
        if (nearFire && (int)random(100) < FF_IGNITE_PCT) nxt = 2;
      } else {
        if ((int)random(1000) < FF_GROW_PERMILLE) nxt = 1;   // sprout
      }

      ffSet(fireNext, x, y, nxt);
      if (nxt == 2) burning++;
      // Burning cells redraw every tick so the flame flickers; everything else
      // only redraws when its state actually changes.
      if (nxt != cur || nxt == 2) ffDraw(x, y, nxt);
    }
  }

  // Lightning: rare random strike, and a guaranteed one if the face has been
  // fire-free long enough (otherwise a fully burned/empty field just sits).
  if (burning == 0) ffCalmTicks++; else ffCalmTicks = 0;
  if (ffCalmTicks > 60 || (int)random(1000) < 18) {
    for (int t = 0; t < 40; t++) {
      const int lx = random(FF_W), ly = random(FF_H);
      if (ffGet(fireNext, lx, ly) == 1) {
        ffSet(fireNext, lx, ly, 2);
        ffDraw(lx, ly, 2);
        ffCalmTicks = 0;
        break;
      }
    }
  }

  uint8_t* tmp = fireCur; fireCur = fireNext; fireNext = tmp;
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
  pipeTurn = 0;
  for (int i = 0; i < PIPE_COUNT; i++) {
    Pipe* p = &pipes[i];
    // Enter from a random face of the cube, heading inward: the Win98 pipes
    // grow into the volume instead of materialising inside it.
    p->x = random(PIPE_N); p->y = random(PIPE_N); p->z = random(PIPE_N);
    switch (random(3)) {
      case 0: p->x = random(2) ? PIPE_N - 1 : 0; break;
      case 1: p->y = random(2) ? PIPE_N - 1 : 0; break;
      default: p->z = random(2) ? PIPE_N - 1 : 0; break;
    }
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
  if (now - lastPipeUpdate < PIPE_STEP_MS) return;
  lastPipeUpdate = now;

  // Advance exactly one pipe per tick, round-robin. At 240 ms that is about
  // four segments a second across five tubes -- slow enough to watch a single
  // pipe build, which is the Win98 feel.
  int idx = -1;
  for (int t = 0; t < PIPE_COUNT; t++) {
    const int c = (pipeTurn + t) % PIPE_COUNT;
    if (pipes[c].alive) { idx = c; break; }
  }
  if (idx < 0) return;
  pipeTurn = (idx + 1) % PIPE_COUNT;

  Pipe* p = &pipes[idx];
  bool turned = false;
  if (random(100) < 26) {
    pipePickDir(p, true);              // elbow
    turned = true;
  } else {
    int nx = p->x + p->dx, ny = p->y + p->dy, nz = p->z + p->dz;
    if (nx < 0 || nx >= PIPE_N || ny < 0 || ny >= PIPE_N || nz < 0 || nz >= PIPE_N)
      pipePickDir(p, false);           // wall ahead: try any way out
  }
  if (!p->alive) { pipesAlive--; lifeSpr.pushSprite(0, 0); return; }

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
  if (turned) lifeSpr.fillCircle(x0, y0, thick + 1, 2);   // joint sphere (accent)

  p->x = nx; p->y = ny; p->z = nz; p->steps++;
  if (p->steps >= p->maxSteps) {
    lifeSpr.fillCircle(x1, y1, thick, 2);                  // end cap (accent)
    p->alive = false;
    pipesAlive--;
  }

  lifeSpr.pushSprite(0, 0);
  if (pipesAlive == 0) pipesRespawnAt = millis() + 2400;
}

// ==== Knight Run (MODE_KNIGHT): endless runner, hops bales + walls, ducks arrows ====
// Pipes lives in its own slot again (MODE_PIPES); this mode never shares it.
#define KR_KX        46                  // knight's fixed x
#define KR_KW        24
#define KR_KH        36
#define KR_GROUND    186                 // feet line
#define KR_TICK      33                  // ~30 fps
#define KR_GRAV      0.248f
#define KR_JUMP_V    4.85f               // ~47 px apex, ~1.3 s of air
#define KR_SPEED_MIN 2.00f               // slowest arc that still clears a 22 px bale
#define KR_SPEED_MAX 3.40f
#define KR_RAMP_MS   120000UL            // full speed after two minutes
#define KR_MAX_OBS   6
#define KR_ARROW_CY  138                 // clears a standing knight by 8 px, hits a jumper
#define KR_GAP_MIN   130
#define KR_GAP_MAX   210
#define KR_ARROW_GAP 195                 // arrows get a clear runway either side
#define KR_REACT     9.0f                // frames of lookahead before it hops
#define KR_REACT_JIT 3.0f                // human-ish timing slop; makes it stumble now and then

enum KrKind : uint8_t { KR_BALE, KR_WALL, KR_ARROW };
struct KrObs { uint8_t kind; float x; bool live; };
static KrObs krObs[KR_MAX_OBS];
static float krSpeed = KR_SPEED_MIN;
static float krLift = 0.0f, krVel = 0.0f;   // height above ground, px / frame
static bool krAir = false, krDead = false;
static int krScore = 0, krHi = 0;
static float krSpawnGap = 150.0f;
static uint8_t krRunPhase = 0, krRunTick = 0;
static unsigned long krLastTick = 0, krStartAt = 0, krDeadUntil = 0, krScoreAcc = 0;

// 3x5 blocky digits: no font dependency, and it matches the pixel grid.
static const uint8_t KR_GLYPH[10][5] = {
  {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
  {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
  {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

static void blockNumber(int v, int x, int y, int s, uint8_t c) {
  char buf[6];
  if (v < 0) v = 0;
  if (v > 9999) v = 9999;
  const int n = snprintf(buf, sizeof(buf), "%d", v);
  for (int i = 0; i < n; i++) {
    const int d = buf[i] - '0';
    for (int r = 0; r < 5; r++)
      for (int q = 0; q < 3; q++)
        if (KR_GLYPH[d][r] & (1 << (2 - q)))
          lifeSpr.fillRect(x + q * s, y + r * s, s, s, c);
    x += 4 * s;
  }
}

static void krDims(uint8_t kind, int* w, int* h) {
  if (kind == KR_WALL)       { *w = 16; *h = 28; }
  else if (kind == KR_ARROW) { *w = 18; *h = 8;  }
  else                       { *w = 22; *h = 16; }
}

static void krDrawKnight(int x, int feet, bool air, uint8_t body, uint8_t trim) {
  const int top = feet - KR_KH;
  lifeSpr.fillRect(x + 16, top - 4, 8, 6, trim);      // plume
  lifeSpr.fillRect(x + 4, top, 16, 12, body);         // helmet
  lifeSpr.fillRect(x + 4, top + 12, 2, 4, body);      // jaw
  lifeSpr.fillRect(x + 14, top + 4, 6, 4, 0);         // visor slit
  lifeSpr.fillRect(x + 6, top + 14, 14, 12, body);    // cuirass
  lifeSpr.fillRect(x + 6, top + 24, 14, 2, trim);     // belt
  lifeSpr.fillRect(x - 2, top + 14, 8, 12, trim);     // shield rim
  lifeSpr.fillRect(x, top + 16, 4, 8, body);          // shield face
  lifeSpr.fillRect(x + 20, top + 8, 4, 16, trim);     // sword arm
  lifeSpr.fillRect(x + 24, top - 2, 2, 14, trim);     // blade
  if (air) {
    lifeSpr.fillRect(x + 6, top + 26, 6, 8, body);    // legs tucked
    lifeSpr.fillRect(x + 14, top + 26, 6, 6, body);
    lifeSpr.fillRect(x + 4, feet - 2, 8, 2, trim);
  } else {
    const int a = (krRunPhase & 1) ? 2 : -2;
    lifeSpr.fillRect(x + 4 + a, top + 26, 6, 10, body);
    lifeSpr.fillRect(x + 14 - a, top + 26, 6, 10, body);
    lifeSpr.fillRect(x + 2 + a, feet - 2, 8, 2, trim);
    lifeSpr.fillRect(x + 12 - a, feet - 2, 8, 2, trim);
  }
}

static void krRender() {
  lifeSpr.fillSprite(0);

  // ground: chunky dashes, not a hairline
  for (int x = 18; x < 222; x += 8) lifeSpr.fillRect(x, KR_GROUND + 2, 4, 2, 1);

  for (int i = 0; i < KR_MAX_OBS; i++) {
    if (!krObs[i].live) continue;
    const int x = (int)krObs[i].x;
    if (krObs[i].kind == KR_BALE) {
      const int y = KR_GROUND - 16;
      lifeSpr.fillRect(x, y, 22, 16, 1);
      lifeSpr.fillRect(x, y, 3, 16, 2);
      lifeSpr.fillRect(x + 3, y + 4, 17, 2, 2);
      lifeSpr.fillRect(x + 3, y + 11, 17, 2, 2);
    } else if (krObs[i].kind == KR_WALL) {
      const int y = KR_GROUND - 28;
      lifeSpr.fillRect(x, y, 16, 28, 1);
      for (int r = 0; r < 4; r++) lifeSpr.fillRect(x, y + r * 8, 16, 1, 2);
      lifeSpr.fillRect(x + 7, y + 1, 1, 7, 2);
      lifeSpr.fillRect(x + 3, y + 9, 1, 7, 2);
      lifeSpr.fillRect(x + 11, y + 9, 1, 7, 2);
      lifeSpr.fillRect(x + 7, y + 17, 1, 7, 2);
      lifeSpr.fillRect(x + 3, y + 25, 1, 3, 2);
      lifeSpr.fillRect(x + 11, y + 25, 1, 3, 2);
    } else {
      const int y = KR_ARROW_CY;
      lifeSpr.fillRect(x, y - 4, 6, 8, 1);            // head, pointing left
      lifeSpr.fillRect(x + 4, y - 1, 12, 3, 2);       // shaft
      lifeSpr.fillRect(x + 13, y - 4, 5, 3, 2);       // fletching
      lifeSpr.fillRect(x + 13, y + 2, 5, 3, 2);
    }
  }

  const int feet = KR_GROUND - (int)krLift;
  krDrawKnight(KR_KX, feet, krAir, krDead ? 2 : 1, krDead ? 1 : 2);

  blockNumber(krScore, 58, 42, 3, 1);
  blockNumber(krHi,    58, 62, 2, 2);

  lifeSpr.pushSprite(0, 0);
}

void knightInit() {
  randomSeed(esp_random());
  lifeSpr.fillSprite(0);
  for (int i = 0; i < KR_MAX_OBS; i++) krObs[i].live = false;
  krSpeed = KR_SPEED_MIN;
  krLift = 0.0f; krVel = 0.0f;
  krAir = false; krDead = false;
  krScore = 0; krHi = 0;                 // "high score that session"
  krSpawnGap = 150.0f;
  krRunPhase = 0; krRunTick = 0;
  krScoreAcc = 0;
  krLastTick = millis();
  krStartAt = krLastTick;
  krDeadUntil = 0;
  krRender();
}

static void krSpawn() {
  int slot = -1;
  for (int i = 0; i < KR_MAX_OBS; i++) if (!krObs[i].live) { slot = i; break; }
  if (slot < 0) return;
  const int r = random(100);
  uint8_t kind = KR_BALE;
  if (r < 34)      kind = KR_WALL;
  else if (r < 52) kind = KR_ARROW;
  krObs[slot].kind = kind;
  krObs[slot].x = 252.0f;
  krObs[slot].live = true;
  krSpawnGap = (kind == KR_ARROW) ? (float)(KR_ARROW_GAP + random(70))
                                  : (float)(KR_GAP_MIN + random(KR_GAP_MAX - KR_GAP_MIN + 1));
}

void updateKnight() {
  const unsigned long now = millis();
  if (now - krLastTick < KR_TICK) return;
  const unsigned long dt = now - krLastTick;
  krLastTick = now;

  if (krDead) {
    if (now < krDeadUntil) return;
    for (int i = 0; i < KR_MAX_OBS; i++) krObs[i].live = false;
    krLift = 0.0f; krVel = 0.0f; krAir = false; krDead = false;
    krScore = 0; krSpeed = KR_SPEED_MIN;
    krSpawnGap = 150.0f; krRunPhase = 0; krRunTick = 0;
    krScoreAcc = 0; krStartAt = now;
    krRender();
    return;
  }

  // ---- speed ramp: 1.2 px/frame to 3.0 over two minutes ----
  float ramp = (float)(now - krStartAt) / (float)KR_RAMP_MS;
  if (ramp > 1.0f) ramp = 1.0f;
  krSpeed = KR_SPEED_MIN + (KR_SPEED_MAX - KR_SPEED_MIN) * ramp;

  // ---- one point per second, on real time not frame count ----
  krScoreAcc += dt;
  while (krScoreAcc >= 1000UL) { krScoreAcc -= 1000UL; krScore++; }

  // ---- jump arc ----
  if (krAir) {
    krVel -= KR_GRAV;
    krLift += krVel;
    if (krLift <= 0.0f) { krLift = 0.0f; krVel = 0.0f; krAir = false; }
  }

  // ---- obstacles ----
  krSpawnGap -= krSpeed;
  if (krSpawnGap <= 0.0f) krSpawn();

  for (int i = 0; i < KR_MAX_OBS; i++) {
    if (!krObs[i].live) continue;
    krObs[i].x -= krSpeed;
    int w, h; krDims(krObs[i].kind, &w, &h);
    if (krObs[i].x + w < -4.0f) krObs[i].live = false;
  }

  // ---- AI: hop ground obstacles, never hop at an arrow ----
  if (!krAir) {
    const float lead = krSpeed * (KR_REACT + ((float)random(-(int)(KR_REACT_JIT * 10.0f), (int)(KR_REACT_JIT * 10.0f) + 1) / 10.0f)) + 2.0f;
    for (int i = 0; i < KR_MAX_OBS; i++) {
      if (!krObs[i].live || krObs[i].kind == KR_ARROW) continue;
      const float d = krObs[i].x - (KR_KX + KR_KW);
      if (d < lead && d > -6.0f) { krAir = true; krVel = KR_JUMP_V; break; }
    }
  }

  // ---- collision ----
  const float kx0 = (float)KR_KX, kx1 = (float)(KR_KX + KR_KW);
  const float ky1 = (float)KR_GROUND - krLift, ky0 = ky1 - (float)KR_KH;
  for (int i = 0; i < KR_MAX_OBS; i++) {
    if (!krObs[i].live) continue;
    int w, h; krDims(krObs[i].kind, &w, &h);
    const float ox0 = krObs[i].x, ox1 = ox0 + (float)w;
    float oy0, oy1;
    if (krObs[i].kind == KR_ARROW) { oy0 = KR_ARROW_CY - 4.0f; oy1 = KR_ARROW_CY + 4.0f; }
    else                           { oy1 = (float)KR_GROUND;   oy0 = oy1 - (float)h; }
    if (kx1 > ox0 && kx0 < ox1 && ky1 > oy0 && ky0 < oy1) {
      krDead = true;
      krDeadUntil = now + 1200UL;
      if (krScore > krHi) krHi = krScore;
      break;
    }
  }

  // ---- run cycle ----
  if (!krAir && ++krRunTick >= 4) { krRunTick = 0; krRunPhase++; }

  krRender();
}

// ==== Coin Dozer (MODE_COIN): plate shoves a pile of blocky coins off the edge ====
// Stacking is a 1D sandpile per column, not rigid-body collisions: a coin can
// never sink into another, which is the failure the circle solver showed.
#define CD_COLS      12
#define CD_X0        24                  // left edge of the table
#define CD_COL_W     16
#define CD_CAP       6                   // coins a column stacks before it spills
#define CD_FLOOR_Y   186                 // table surface
#define CD_COIN_H    6
#define CD_TICK      33                  // ~30 fps
#define CD_STROKE_MS 22000UL             // slow forward shove
#define CD_RETRACT_MS 2000UL
#define CD_DROP_MIN  2200UL
#define CD_DROP_MAX  4200UL
#define CD_MAX_FALL  3

static uint8_t  cdCol[CD_COLS];
static int      cdScore = 0;
static float    cdStroke = 0.0f;         // 0 = retracted, 1 = full extension
static bool     cdRetracting = false;
static unsigned long cdStrokeAt = 0, cdNextDrop = 0, cdLastTick = 0;
static bool cdScoreFlash = false;

struct CdFall { bool live; float x, y; int8_t col; };
static CdFall cdFall[CD_MAX_FALL];

static int cdCoinTop(int col) { return CD_FLOOR_Y - cdCol[col] * CD_COIN_H; }

static void cdScoreOne() {
  cdScore++;
  cdScoreFlash = true;
}

// Any column over capacity tips toward the emptier neighbour, and a full end
// column drops its top coin off the edge.
static void cdSettle(int startCol) {
  for (int pass = 0; pass < 8; pass++) {
    bool moved = false;
    int i = startCol;
    for (int n = 0; n < CD_COLS; n++, i = (i + 1) % CD_COLS) {
      if (cdCol[i] <= CD_CAP) continue;
      if (i == CD_COLS - 1) {
        cdCol[i]--;
        cdScoreOne();
        moved = true;
        continue;
      }
      const int left  = (i > 0) ? cdCol[i - 1] : 255;
      const int right = cdCol[i + 1];
      const int j = (right <= left) ? i + 1 : i - 1;
      if (j < 0) continue;
      cdCol[i]--;
      cdCol[j]++;
      moved = true;
    }
    if (!moved) break;
  }
}

// The plate reaches full extension: every column hands one coin to the right.
static void cdShove() {
  for (int i = CD_COLS - 1; i >= 0; i--) {
    if (cdCol[i] == 0) continue;
    cdCol[i]--;
    if (i == CD_COLS - 1) cdScoreOne();
    else cdCol[i + 1]++;
  }
  for (int i = 0; i < CD_COLS; i++) cdSettle(i);
}

void coinInit() {
  randomSeed(esp_random());
  for (int i = 0; i < CD_COLS; i++) cdCol[i] = 0;
  for (int i = 0; i < CD_MAX_FALL; i++) cdFall[i].live = false;
  cdScore = 0;
  cdStroke = 0.0f;
  cdRetracting = false;
  cdScoreFlash = false;
  const unsigned long now = millis();
  cdStrokeAt = now;
  cdLastTick = now;
  cdNextDrop = now + 1200;
  lifeSpr.fillSprite(0);
}

static void cdDrop() {
  const int n = (random(100) < 60) ? 1 : ((random(100) < 75) ? 2 : 3);
  const int col = random(CD_COLS);
  for (int k = 0; k < n; k++) {
    for (int s = 0; s < CD_MAX_FALL; s++) {
      if (cdFall[s].live) continue;
      cdFall[s].live = true;
      cdFall[s].col = (int8_t)col;
      cdFall[s].x = CD_X0 + col * CD_COL_W + 1;
      cdFall[s].y = 24.0f - k * 14.0f;   // staggered, so a triple reads as a burst
      break;
    }
  }
}

static void cdDrawCoin(int x, int y, uint8_t body, uint8_t rim) {
  // Blocky disc: narrow top and bottom rows, wide middle.
  lifeSpr.fillRect(x + 2, y,     10, 1, body);
  lifeSpr.fillRect(x + 1, y + 1, 12, 1, body);
  lifeSpr.fillRect(x,     y + 2, 14, 2, body);
  lifeSpr.fillRect(x + 1, y + 4, 12, 1, body);
  lifeSpr.fillRect(x + 2, y + 5, 10, 1, body);
  lifeSpr.fillRect(x + 3, y + 1,  8, 1, rim);   // top face catches the light
}

static void cdRender() {
  lifeSpr.fillSprite(0);

  // table + the drop edge on the right
  lifeSpr.fillRect(CD_X0, CD_FLOOR_Y, CD_COLS * CD_COL_W, 2, 1);
  lifeSpr.fillRect(CD_X0 + CD_COLS * CD_COL_W + 2, CD_FLOOR_Y, 2, 16, 2);
  for (int x = CD_X0; x < CD_X0 + CD_COLS * CD_COL_W; x += 8)
    lifeSpr.fillRect(x, CD_FLOOR_Y + 4, 3, 1, 1);

  // the pile
  for (int c = 0; c < CD_COLS; c++) {
    const int x = CD_X0 + c * CD_COL_W + 1;
    const int base = cdCoinTop(c);
    for (int k = 0; k < cdCol[c]; k++)
      cdDrawCoin(x, base + k * CD_COIN_H, 1, 2);
  }

  // coins in the air
  for (int i = 0; i < CD_MAX_FALL; i++) {
    if (!cdFall[i].live) continue;
    cdDrawCoin((int)cdFall[i].x, (int)cdFall[i].y, 2, 1);
  }

  // plate: travels from retracted to full extension over the stroke
  const int plateX = 16 + (int)(cdStroke * 58.0f);
  lifeSpr.fillRect(plateX, CD_FLOOR_Y - 44, 10, 44, 2);
  lifeSpr.fillRect(plateX + 2, CD_FLOOR_Y - 40, 6, 36, 1);
  lifeSpr.fillRect(plateX, CD_FLOOR_Y - 6, 10, 6, 2);

  blockNumber(cdScore, 58, 42, 3, cdScoreFlash ? 2 : 1);

  lifeSpr.pushSprite(0, 0);
  cdScoreFlash = false;
}

void updateCoin() {
  const unsigned long now = millis();
  if (now - cdLastTick < CD_TICK) return;
  const unsigned long dt = now - cdLastTick;
  cdLastTick = now;

  // ---- plate stroke ----
  if (cdRetracting) {
    if (now - cdStrokeAt >= CD_RETRACT_MS) {
      cdRetracting = false;
      cdStroke = 0.0f;
      cdStrokeAt = now;
    }
  } else {
    cdStroke = (float)(now - cdStrokeAt) / (float)CD_STROKE_MS;
    if (cdStroke >= 1.0f) {
      cdStroke = 1.0f;
      cdShove();
      cdRetracting = true;
      cdStrokeAt = now;
    }
  }

  // ---- drops: single, double or triple ----
  if (now >= cdNextDrop) {
    cdNextDrop = now + CD_DROP_MIN + random(CD_DROP_MAX - CD_DROP_MIN);
    cdDrop();
  }

  // ---- falling coins land on the pile ----
  for (int i = 0; i < CD_MAX_FALL; i++) {
    if (!cdFall[i].live) continue;
    cdFall[i].y += 2.2f + (float)(dt / 8);
    const int top = cdCoinTop(cdFall[i].col);
    if ((int)cdFall[i].y >= top - CD_COIN_H) {
      cdFall[i].live = false;
      if (cdCol[cdFall[i].col] < 255) cdCol[cdFall[i].col]++;
      cdSettle(cdFall[i].col);
    }
  }

  cdRender();
}

// ==== Pet (MODE_RADAR) ====
static void tamaLoad() {
  tamaPrefs.begin("tama", false);
  tHunger = tamaPrefs.getUChar("hunger", 80);
  tHappy  = tamaPrefs.getUChar("happy", 70);
  tEnergy = tamaPrefs.getUChar("energy", 90);
  tPoops  = tamaPrefs.getUChar("poops", 0);
  tAgeMin = tamaPrefs.getUShort("age", 0);
}

static void tamaSave() {
  tamaPrefs.putUChar("hunger", tHunger);
  tamaPrefs.putUChar("happy", tHappy);
  tamaPrefs.putUChar("energy", tEnergy);
  tamaPrefs.putUChar("poops", tPoops);
  tamaPrefs.putUShort("age", tAgeMin);
}

void radarInit() {                     // mode entry
  randomSeed(esp_random());
  tamaLoad();
  lifeSpr.fillSprite(0);
  const unsigned long now = millis();
  tLastTick = now;
  tLastSave = now;
  tEatUntil = tPlayUntil = tSleepUntil = 0;
  tBlinkAt = now + 1500 + random(2500);
  tBlinkUntil = 0;
  tPoopAt = now + 150000UL + random(120000UL);
  tHungerAcc = tHappyAcc = tEnergyAcc = tMinuteAcc = 0;
  tSleeping = false;
  tOffX = 0; tDir = 1; tWanderAt = now + 4000; tPlayCheckAt = now + 20000;
  tPoopClearAt = 0;
}

static void tamaHeart(int x, int y) {
  lifeSpr.fillRect(x,     y,     2, 2, 2);
  lifeSpr.fillRect(x + 3, y,     2, 2, 2);
  lifeSpr.fillRect(x + 1, y + 2, 3, 2, 2);
}

static void tamaZ(int x, int y, int s) {
  lifeSpr.fillRect(x, y, 4 * s, s, 2);
  lifeSpr.fillRect(x + s, y + s, 2 * s, s, 2);
  lifeSpr.fillRect(x, y + 2 * s, 4 * s, s, 2);
}

void updateTama() {
  const unsigned long now = millis();
  if (now - tLastTick < TAMA_TICK_MS) return;
  const unsigned long dt = now - tLastTick;
  tLastTick = now;

  // ---- stats, decayed in real time ----
  tHungerAcc += (uint32_t)dt;
  tHappyAcc  += (uint32_t)dt;
  tEnergyAcc += (uint32_t)dt;
  tMinuteAcc += (uint32_t)dt;
  if (tMinuteAcc >= 60000UL) { tMinuteAcc = 0; if (tAgeMin < 65000) tAgeMin++; }

  if (tHungerAcc >= 20000UL) { tHungerAcc = 0; if (tHunger) tHunger--; }
  if (tHappyAcc >= (tPoops ? 20000UL : 45000UL)) {
    tHappyAcc = 0;
    if (tHappy) tHappy--;
  }
  if (tEnergyAcc >= (tSleeping ? 2500UL : 12000UL)) {
    tEnergyAcc = 0;
    if (tSleeping) { if (tEnergy < 100) tEnergy += 2; }
    else if (tEnergy) tEnergy--;
  }

  // ---- sleep cycle ----
  if (!tSleeping && tEnergy <= 12 && now > tEatUntil && now > tPlayUntil) {
    tSleeping = true;
    tSleepUntil = now + 45000UL + random(45000UL);
  }
  if (tSleeping && now >= tSleepUntil) tSleeping = false;

  // ---- autonomous foraging: it feeds itself when hungry ----
  if (!tSleeping && tHunger < 25 && now > tEatUntil) {
    tEatUntil = now + 2600;
  }
  if (tEatUntil && now >= tEatUntil) {
    tEatUntil = 0;
    if (tHunger < 100) tHunger = (tHunger > 70) ? 100 : (uint8_t)(tHunger + 30);
    if (tHappy < 100) tHappy += 5;
  }

  // ---- poops: it leaves one, then tidies up after itself ----
  if (now >= tPoopAt && tPoops < 3) {
    tPoops++;
    tPoopAt = now + 180000UL + random(180000UL);
    tPoopClearAt = now + 22000UL;
  }
  if (tPoopClearAt && now >= tPoopClearAt) {
    tPoopClearAt = 0;
    if (tPoops) tPoops--;
    if (tPoops) tPoopClearAt = now + 6000UL;
  }

  // ---- blink ----
  if (!tBlinkUntil && now >= tBlinkAt) {
    tBlinkUntil = now + 220;
  } else if (tBlinkUntil && now >= tBlinkUntil) {
    tBlinkUntil = 0;
    tBlinkAt = now + 2000UL + random(4000UL);
  }

  // ---- wander: it potters around the face on its own ----
  if (now >= tWanderAt) {
    tWanderAt = now + 5000UL + random(6000UL);
    if (random(100) < 45) tDir = (int8_t)(random(2) ? 1 : -1);
    if (random(100) < 15) tDir = 0;                 // pause and look around
  }
  if (!tSleeping) {
    tOffX += tDir;
    if (tOffX > 26) { tOffX = 26; tDir = -1; }
    if (tOffX < -26) { tOffX = -26; tDir = 1; }
  }

  // ---- spontaneous play: no prompting required ----
  if (now >= tPlayCheckAt) {
    tPlayCheckAt = now + 18000UL + random(20000UL);
    if (!tSleeping && tEnergy > 35 && tHappy < 85) {
      tPlayUntil = now + 2400UL;
      tHappy = (tHappy > 80) ? 100 : (uint8_t)(tHappy + 18);
      if (tEnergy > 10) tEnergy -= 4;
    }
  }
  if (tPlayUntil && now >= tPlayUntil) tPlayUntil = 0;

  // ---- persistence every 5 min ----
  if (now - tLastSave >= 300000UL) { tLastSave = now; tamaSave(); }

  // ---- draw: chunky pixel goo ----
  lifeSpr.fillSprite(0);
  const bool eating  = (tEatUntil != 0);
  const bool playing = (tPlayUntil != 0);
  const bool shut    = tSleeping || (tBlinkUntil != 0);

  // Wobble: every sprite row slides sideways a little, phase-lagged down the
  // body, so the whole mass jiggles like something with no skeleton.
  tGooPhase += playing ? 0.55f : 0.22f;
  if (tGooPhase > 6.28318f) tGooPhase -= 6.28318f;

  const int cxPix = tOffX / GOO_CELL;              // wander, in sprite pixels
  int top = 100;                                   // blob base lands on y=164
  if (tSleeping) top += 8;                         // settles when it sleeps
  const int squash = playing ? (int)((now / 160) & 1) : 0;

  // Body: a droplet, widest near the base. Half-widths in sprite pixels.
  static const uint8_t GOO_HW[16] = {3, 4, 4, 5, 5, 6, 6, 7, 7, 7, 8, 8, 8, 8, 8, 7};
  for (int r = 0; r < 16; r++) {
    if (tSleeping && r < 2) continue;              // flattens while asleep
    const int hw  = GOO_HW[r] + ((squash && r > 10) ? 1 : 0);
    const int off = gooRowOff(r);
    lifeSpr.fillRect(120 + (cxPix + off - hw) * GOO_CELL, top + r * GOO_CELL,
                     (2 * hw + 1) * GOO_CELL, GOO_CELL, 1);
  }

  // Puddle: it always sits in a little of its own goo, and drips rejoin it.
  const int puddleY = top + 16 * GOO_CELL;
  const int puddleHw = 9 + (tPuddlePulse ? 1 : 0) + (playing ? 1 : 0);
  lifeSpr.fillRect(120 + (cxPix - puddleHw) * GOO_CELL, puddleY,
                   (2 * puddleHw + 1) * GOO_CELL, GOO_CELL, 1);
  lifeSpr.fillRect(120 + (cxPix - puddleHw + 2) * GOO_CELL, puddleY + GOO_CELL,
                   (2 * puddleHw - 3) * GOO_CELL, GOO_CELL, 1);
  if (tPuddlePulse) tPuddlePulse--;

  // Falling bead off one flank.
  if (tDripRow >= 0) {
    tDripRow++;
    if (tDripRow >= 16) { tDripRow = -1; tPuddlePulse = 8; }
    else {
      const int dhw = GOO_HW[tDripRow];
      lifeSpr.fillRect(120 + (cxPix + tDripSide * dhw) * GOO_CELL,
                       top + tDripRow * GOO_CELL, GOO_CELL, GOO_CELL, 1);
    }
  } else if (random(240) == 0 && !tSleeping) {
    tDripRow = 9;
    tDripSide = (int8_t)(random(2) ? 1 : -1);
  }

  // Eyes: two dark blocks punched straight through the goo, 2x2 sprite pixels.
  const int eyeOff = gooRowOff(5);
  const int eyeY   = top + 4 * GOO_CELL;
  const int eyeH   = shut ? GOO_CELL : 2 * GOO_CELL;
  const int eyeL   = 120 + (cxPix + eyeOff - 4) * GOO_CELL;
  const int eyeR   = 120 + (cxPix + eyeOff + 2) * GOO_CELL;
  lifeSpr.fillRect(eyeL, eyeY, 2 * GOO_CELL, eyeH, 0);
  lifeSpr.fillRect(eyeR, eyeY, 2 * GOO_CELL, eyeH, 0);
  if (!shut) {                                   // one glint pixel each
    lifeSpr.fillRect(eyeL, eyeY, GOO_CELL, GOO_CELL, 2);
    lifeSpr.fillRect(eyeR, eyeY, GOO_CELL, GOO_CELL, 2);
  }

  // Mouth: 2-4 sprite pixels, no curves.
  const int mOff = gooRowOff(8);
  const int mx   = 120 + (cxPix + mOff) * GOO_CELL;
  const int my   = top + 8 * GOO_CELL;
  if (eating || tHunger < 25) {                  // open maw
    lifeSpr.fillRect(mx - GOO_CELL, my, 2 * GOO_CELL, 2 * GOO_CELL, 0);
  } else if (tHappy >= 70) {                     // corners up
    lifeSpr.fillRect(mx - 2 * GOO_CELL, my - GOO_CELL, GOO_CELL, GOO_CELL, 0);
    lifeSpr.fillRect(mx, my, GOO_CELL, GOO_CELL, 0);
    lifeSpr.fillRect(mx + GOO_CELL, my - GOO_CELL, GOO_CELL, GOO_CELL, 0);
  } else if (tHappy < 30) {                      // corners down
    lifeSpr.fillRect(mx, my, GOO_CELL, GOO_CELL, 0);
    lifeSpr.fillRect(mx - 2 * GOO_CELL, my + GOO_CELL, GOO_CELL, GOO_CELL, 0);
    lifeSpr.fillRect(mx + GOO_CELL, my + GOO_CELL, GOO_CELL, GOO_CELL, 0);
  } else {
    lifeSpr.fillRect(mx - GOO_CELL, my, 2 * GOO_CELL, GOO_CELL, 0);
  }

  // Food while eating: a chunky pellet it leans toward.
  if (eating) {
    const int fy = 120 + ((now / 200) & 1) * GOO_CELL;
    lifeSpr.fillRect(120 + (cxPix + 12) * GOO_CELL, fy, 2 * GOO_CELL, 2 * GOO_CELL, 5);
  }
  // Hearts when content.
  if (tHappy >= 70 && !tSleeping && ((now / 900) & 1)) {
    tamaHeart(150, top - 16);
    tamaHeart(166, top - 34);
  }
  // Zzz while asleep.
  if (tSleeping) {
    tamaZ(150, top - 16, 2);
    tamaZ(170, top - 36, 1);
  }
  // Poops: squat pixel clumps on the floor.
  for (int i = 0; i < tPoops; i++) {
    const int px = 88 + i * 20;
    lifeSpr.fillRect(px, 184, 3 * GOO_CELL, 2 * GOO_CELL, 7);
    lifeSpr.fillRect(px + GOO_CELL, 180, GOO_CELL, GOO_CELL, 7);
  }

  lifeSpr.pushSprite(0, 0);
}

// ==== Crystal (MODE_STARFIELD): diffusion-limited aggregation ====
// One random walker at a time diffuses around the deposit and sticks where it
// touches. Walkers launch just outside the current surface instead of from a
// far circle -- true DLA launches far away, but that costs millions of steps
// per particle and would take an hour to fill a 240px face.
#define DLA_TICK_MS 30
#define DLA_STEPS 400              // walker steps per tick
#define DLA_SKIN 14                // walker gives up this far past the surface
#define DLA_RMAX 55                // visible radius is ~58 cells
#define DLA_MAXCELLS 4000

// Paint one deposit cell as a dithered mix of the user's two colours:
// primary (slot 1) at the seed, secondary (slot 2) at the rim. Each cell is
// 2x2 px and gets its own Bayer thresholds, so the blend reads as a smooth
// gradient instead of bands.
static void dlaDrawCell(int ix, int iy, int r) {
  float t = (float)r / (float)DLA_RMAX;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  const uint16_t thresh = (uint16_t)(t * 255.0f);
  const int px = ix * LIFE_CELL, py = iy * LIFE_CELL;
  for (int yy = 0; yy < LIFE_CELL; yy++)
    for (int xx = 0; xx < LIFE_CELL; xx++) {
      const uint8_t b = BAYER4[(py + yy) & 3][(px + xx) & 3];
      lifeSpr.drawPixel(px + xx, py + yy, ((uint16_t)b * 16u + 8u) < thresh ? 2 : 1);
    }
}

static void dlaNewCrystal() {
  memset(dlaGrid, 0, sizeof(dlaGrid));
  lifeSpr.fillSprite(0);
  dlaCx = 60; dlaCy = 60;
  dlaR = 1; dlaCells = 1;
  dlaWalker = false;
  dlaResetAt = 0;
  dlaGrid[dlaCy * DLA_W + dlaCx] = 1;
  dlaDrawCell(dlaCx, dlaCy, 0);
  lifeSpr.pushSprite(0, 0);
  Serial.println("[DLA] new crystal");
}

static void dlaLaunch() {
  const float a = random(628) / 100.0f;
  float r = (float)dlaR + 2.5f;
  if (r > DLA_RMAX) r = DLA_RMAX;
  dlaWx = dlaCx + r * cosf(a);
  dlaWy = dlaCy + r * sinf(a);
  dlaWalker = true;
}

void dlaInit() {
  randomSeed(esp_random());
  dlaNewCrystal();
  lastDlaUpdate = millis();
}

void updateDla() {
  unsigned long now = millis();
  if (now - lastDlaUpdate < DLA_TICK_MS) return;
  lastDlaUpdate = now;

  if (dlaResetAt) {
    if ((long)(now - dlaResetAt) < 0) return;
    dlaNewCrystal();
    return;
  }

  static const int8_t DX[8] = { 1, -1,  0,  0,  1,  1, -1, -1};
  static const int8_t DY[8] = { 0,  0,  1, -1,  1, -1,  1, -1};
  bool dirty = false;

  for (int s = 0; s < DLA_STEPS; s++) {
    if (!dlaWalker) { dlaLaunch(); continue; }

    const int dir = random(8);
    dlaWx += DX[dir];
    dlaWy += DY[dir];
    int ix = (int)lroundf(dlaWx), iy = (int)lroundf(dlaWy);
    if (ix < 1) ix = 1; if (ix > DLA_W - 2) ix = DLA_W - 2;
    if (iy < 1) iy = 1; if (iy > DLA_H - 2) iy = DLA_H - 2;

    const int dx = ix - dlaCx, dy = iy - dlaCy;
    const int r2 = dx * dx + dy * dy;
    const int skin = dlaR + DLA_SKIN;
    if (r2 > skin * skin) { dlaWalker = false; continue; }   // escaped: respawn

    // Contact with the deposit (or standing on it) freezes the walker.
    const bool touch = dlaGrid[iy * DLA_W + ix] ||
                       dlaGrid[iy * DLA_W + ix - 1] ||
                       dlaGrid[iy * DLA_W + ix + 1] ||
                       dlaGrid[(iy - 1) * DLA_W + ix] ||
                       dlaGrid[(iy + 1) * DLA_W + ix];
    if (touch) {
      const int r = (int)lroundf(sqrtf((float)r2));
      dlaGrid[iy * DLA_W + ix] = 1;
      dlaDrawCell(ix, iy, r);
      if (r > dlaR) dlaR = r;
      dlaCells++;
      dlaWalker = false;
      dirty = true;
      if (dlaR >= DLA_RMAX || dlaCells >= DLA_MAXCELLS) {
        dlaResetAt = now + 2500;         // hold the finished crystal, then regrow
        break;
      }
    }
  }

  if (dirty) lifeSpr.pushSprite(0, 0);
}

// ==== Snake (MODE_OSC): the pod plays itself, trying not to die ====
static inline int snkIdx(int x, int y) { return y * SNK_W + x; }

static inline void snkPlot(int x, int y, int slot) {
  lifeSpr.fillRect(SNK_X0 + x * SNK_CELL, SNK_Y0 + y * SNK_CELL,
                   SNK_CELL, SNK_CELL, (uint8_t)slot);
}

static void snkDrawFood() {
  // a pellet inside the cell, not a full block
  const int px = SNK_X0 + snkFX * SNK_CELL, py = SNK_Y0 + snkFY * SNK_CELL;
  lifeSpr.fillRect(px, py, SNK_CELL, SNK_CELL, 0);
  lifeSpr.fillRect(px + 1, py + 1, 2, 2, 2);
}

static void snkSpawnFood() {
  for (int t = 0; t < 400; t++) {
    const int x = random(SNK_W), y = random(SNK_H);
    if (!snkOcc[snkIdx(x, y)]) { snkFX = (uint8_t)x; snkFY = (uint8_t)y; snkDrawFood(); return; }
  }
  snkFX = 0; snkFY = 0;                 // board full: nowhere to put it
}

static void snkReset() {
  memset(snkOcc, 0, sizeof(snkOcc));
  lifeSpr.fillSprite(0);
  const int cx = SNK_W / 2, cy = SNK_H / 2;
  snkLen = 4;
  for (int i = 0; i < snkLen; i++) {
    snkSegX[i] = (uint8_t)(cx - i);
    snkSegY[i] = (uint8_t)cy;
    snkOcc[snkIdx(cx - i, cy)] = 1;
    snkPlot(cx - i, cy, i ? 1 : 2);
  }
  snkDX = 1; snkDY = 0;
  snkDeadAt = 0;
  snkSpawnFood();
  lifeSpr.pushSprite(0, 0);
}

// Free cells reachable from (sx,sy) with the body as it will be after the move
// (the tail cell is freed first). This is what keeps the AI from sealing itself
// into a pocket: a move that can still reach fewer cells than it has body is
// suicide, however close it puts the head to the pellet.
static int snkFlood(int sx, int sy, int tx, int ty) {
  static uint8_t seen[SNK_MAX];
  static uint16_t q[SNK_MAX];
  memset(seen, 0, sizeof(seen));
  int qh = 0, qt = 0, count = 0;
  const int start = snkIdx(sx, sy);
  q[qt++] = (uint16_t)start;
  seen[start] = 1;
  while (qh < qt) {
    const uint16_t c = q[qh++];
    const int cx = c % SNK_W, cy = c / SNK_W;
    count++;
    for (int k = 0; k < 4; k++) {
      static const int8_t NX[4] = {1, -1, 0, 0};
      static const int8_t NY[4] = {0, 0, 1, -1};
      const int nx = (cx + NX[k] + SNK_W) % SNK_W;
      const int ny = (cy + NY[k] + SNK_H) % SNK_H;
      const int ni = snkIdx(nx, ny);
      if (seen[ni]) continue;
      bool blocked = snkOcc[ni] != 0;
      if (blocked && nx == tx && ny == ty) blocked = false;   // tail moves away
      if (blocked) continue;
      seen[ni] = 1;
      if (qt < SNK_MAX) q[qt++] = (uint16_t)ni;
    }
  }
  return count;
}

static void snkDie() {
  snkDeadAt = millis() + SNK_DEAD_HOLD;
  if (snkLen > snkBest) { snkBest = snkLen; Serial.printf("[SNAKE] best %d\n", (int)snkBest); }
  // blink the corpse so the death is legible, then it starts over
  for (int i = 0; i < snkLen; i++)
    snkPlot(snkSegX[i], snkSegY[i], 2);
  lifeSpr.pushSprite(0, 0);
}

static void snkMove() {
  const int hx = snkSegX[0], hy = snkSegY[0];
  const int tx = snkSegX[snkLen - 1], ty = snkSegY[snkLen - 1];
  static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  int safeDir = -1, safeDist = 1 << 20;
  int anyDir = -1, anyReach = -1;

  for (int k = 0; k < 4; k++) {
    if (D[k][0] == -snkDX && D[k][1] == -snkDY) continue;      // no reversing
    const int nx = (hx + D[k][0] + SNK_W) % SNK_W;
    const int ny = (hy + D[k][1] + SNK_H) % SNK_H;
    if (snkOcc[snkIdx(nx, ny)] && !(nx == tx && ny == ty)) continue;
    const bool eats = (nx == snkFX && ny == snkFY);
    const int reach = snkFlood(nx, ny, tx, ty);
    if (reach > anyReach) { anyReach = reach; anyDir = k; }
    const int need = snkLen + (eats ? 1 : 0) - 1;              // room left to coil
    if (reach >= need) {
      const int dist = abs(nx - snkFX) + abs(ny - snkFY);
      if (safeDir < 0 || dist < safeDist) { safeDir = k; safeDist = dist; }
    }
  }

  const int dir = (safeDir >= 0) ? safeDir : anyDir;
  if (dir < 0) { snkDie(); return; }
  snkDX = D[dir][0];
  snkDY = D[dir][1];

  const int nx = (hx + snkDX + SNK_W) % SNK_W;
  const int ny = (hy + snkDY + SNK_H) % SNK_H;
  const bool eats = (nx == snkFX && ny == snkFY);

  if (eats) {
    if (snkLen < SNK_MAX) {
      memmove(&snkSegX[1], &snkSegX[0], snkLen * sizeof(uint8_t));
      memmove(&snkSegY[1], &snkSegY[0], snkLen * sizeof(uint8_t));
      snkLen++;
    } else {
      memmove(&snkSegX[1], &snkSegX[0], (snkLen - 1) * sizeof(uint8_t));
      memmove(&snkSegY[1], &snkSegY[0], (snkLen - 1) * sizeof(uint8_t));
    }
  } else {
    memmove(&snkSegX[1], &snkSegX[0], (snkLen - 1) * sizeof(uint8_t));
    memmove(&snkSegY[1], &snkSegY[0], (snkLen - 1) * sizeof(uint8_t));
    snkOcc[snkIdx(tx, ty)] = 0;
    snkPlot(tx, ty, 0);                       // tail vacates
  }
  snkSegX[0] = (uint8_t)nx;
  snkSegY[0] = (uint8_t)ny;
  snkOcc[snkIdx(nx, ny)] = 1;
  snkPlot(hx, hy, 1);                         // old head becomes body
  snkPlot(nx, ny, 2);                         // new head
  if (eats) snkSpawnFood();
}

void snkInit() {
  randomSeed(esp_random());
  snkBest = 0;
  snkLast = millis();
  snkReset();
}

void updateSnake() {
  unsigned long now = millis();
  if (snkDeadAt) {
    if ((long)(now - snkDeadAt) < 0) return;
    snkReset();
    snkLast = now;
    return;
  }
  if (now - snkLast < SNK_STEP_MS) return;
  snkLast = now;
  snkMove();
  lifeSpr.pushSprite(0, 0);
}

// ==== Rally (MODE_RALLY) ====
// Blocky 3x5 digit font, MSB-left per row — authentic rally scoreboard.
static const uint8_t RALLY_DIGITS[10][5] = {
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

static void rallyDigit(int d, int x, int y) {
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 3; c++)
      if (RALLY_DIGITS[d][r] & (0b100 >> c))
        lifeSpr.fillRect(x + c * 3, y + r * 3, 3, 3, 1);
}

static void rallyScoreDraw(int score, int x) {
  if (score >= 10) {
    rallyDigit(score / 10, x - 6, 36);
    rallyDigit(score % 10, x + 6, 36);
  } else {
    rallyDigit(score, x, 36);
  }
}

static void rallyServe(int dir) {
  rallyBallX = 120; rallyBallY = 120;
  rallyBallVX = 2.3f * dir;
  rallyBallVY = (random(200) - 100) / 50.0f;      // -2..2
  rallyServeDelay = 45;                            // freeze ~1.4 s at serve
}

void rallyInit() {
  randomSeed(esp_random());
  rallyScoreL = rallyScoreR = 0;
  rallyFrame = 0;
  rallyL.y = rallyR.y = 120;
  rallyServe(random(2) ? 1 : -1);
  lifeSpr.fillSprite(0);
}

static void rallyStepPaddle(RallyPaddle* pad, bool toward) {
  if (rallyFrame % 45 == 0) pad->err = (random(200) - 100) / 4.0f;   // -25..25
  float ty = toward ? (rallyBallY + pad->err) : 120.0f;
  float dy = ty - pad->y;
  if (dy > 2.8f) dy = 2.8f; else if (dy < -2.8f) dy = -2.8f;
  pad->y += dy;
  float lo = RALLY_TOP + RALLY_PAD_H / 2, hi = RALLY_BOT - RALLY_PAD_H / 2;
  if (pad->y < lo) pad->y = lo;
  if (pad->y > hi) pad->y = hi;
}

void updateRally() {
  unsigned long now = millis();
  if (now - lastRallyUpdate < 30) return;
  lastRallyUpdate = now;
  rallyFrame++;

  // Paddle AI: track when the ball is coming, drift to center otherwise.
  rallyStepPaddle(&rallyL, rallyBallVX < 0);
  rallyStepPaddle(&rallyR, rallyBallVX > 0);

  if (rallyServeDelay > 0) {
    rallyServeDelay--;
  } else {
    rallyBallX += rallyBallVX;
    rallyBallY += rallyBallVY;

    // Walls
    if (rallyBallY < RALLY_TOP + 2) { rallyBallY = RALLY_TOP + 2; rallyBallVY = fabsf(rallyBallVY); }
    if (rallyBallY > RALLY_BOT - 2) { rallyBallY = RALLY_BOT - 2; rallyBallVY = -fabsf(rallyBallVY); }

    // Paddle collisions (ball is a 4px square, +/-2 around center)
    if (rallyBallVX < 0 && rallyBallX <= RALLY_PAD_X_L + 4 &&
        rallyBallX >= RALLY_PAD_X_L - 8) {
      if (fabsf(rallyBallY - rallyL.y) <= RALLY_PAD_H / 2 + 2) {
        rallyBallX = RALLY_PAD_X_L + 4;
        rallyBallVX = -rallyBallVX;
        rallyBallVY += (rallyBallY - rallyL.y) * 0.09f;      // angle from hit spot
        rallyBallVX *= 1.04f;                              // rally speeds up
      }
    }
    if (rallyBallVX > 0 && rallyBallX >= RALLY_PAD_X_R - 4 &&
        rallyBallX <= RALLY_PAD_X_R + 8) {
      if (fabsf(rallyBallY - rallyR.y) <= RALLY_PAD_H / 2 + 2) {
        rallyBallX = RALLY_PAD_X_R - 4;
        rallyBallVX = -rallyBallVX;
        rallyBallVY += (rallyBallY - rallyR.y) * 0.09f;
        rallyBallVX *= 1.04f;
      }
    }
    float sp = rallyBallVY;
    if (sp > 5.0f) rallyBallVY = 5.0f; else if (sp < -5.0f) rallyBallVY = -5.0f;
    if (rallyBallVX > 4.5f) rallyBallVX = 4.5f;
    if (rallyBallVX < -4.5f) rallyBallVX = -4.5f;

    // Score when the ball escapes a side, then serve toward the loser.
    if (rallyBallX < -4) {
      rallyScoreR++;
      if (rallyScoreR >= 11) { rallyScoreL = 0; rallyScoreR = 0; }
      rallyServe(-1);
    } else if (rallyBallX > 244) {
      rallyScoreL++;
      if (rallyScoreL >= 11) { rallyScoreL = 0; rallyScoreR = 0; }
      rallyServe(1);
    }
  }

  // Draw
  lifeSpr.fillSprite(0);
  for (int y = RALLY_TOP; y < RALLY_BOT; y += 10)          // dashed center line
    lifeSpr.fillRect(119, y, 2, 5, 1);
  lifeSpr.fillRect(RALLY_PAD_X_L, (int)rallyL.y - RALLY_PAD_H / 2, 4, RALLY_PAD_H, 2);
  lifeSpr.fillRect(RALLY_PAD_X_R, (int)rallyR.y - RALLY_PAD_H / 2, 4, RALLY_PAD_H, 2);
  lifeSpr.fillRect((int)rallyBallX - 2, (int)rallyBallY - 2, 4, 4, 2);
  rallyScoreDraw(rallyScoreL, 86);
  rallyScoreDraw(rallyScoreR, 154);
  lifeSpr.pushSprite(0, 0);
}

// ==== Eye (MODE_EQ): a pixel-art eyeball that gets bored ====
// Everything lands on a 4px cell grid, same chunky language as the rest of the
// roster. The eyeball drifts around slowly when it has nothing to look at, and
// occasionally snaps hard left or right, lid up, like something caught its
// attention. Species rotates at each blink: human, cat, goat, robot.
#define EYE_CELL 4
#define EYE_CX 30                 // centre in cells (240px / 4 / 2)
#define EYE_CY 30
#define EYE_A 21                  // sclera half-width, cells
#define EYE_B 13                  // sclera half-height, cells
#define EYE_IRIS 11
#define EYE_TICK_MS 40

static const char* EYE_SPECIES_NAME[4] = {"human", "cat", "goat", "robot"};

static void eyeHrun(int yc, int x0, int x1, uint8_t slot) {
  if (x1 < x0) return;
  lifeSpr.fillRect(x0 * EYE_CELL, yc * EYE_CELL, (x1 - x0 + 1) * EYE_CELL, EYE_CELL, slot);
}

// Pupil extent on one row of the iris. Species-specific: round, slit, bar.
static bool eyePupilSpan(uint8_t sp, int row, int icx, int* p0, int* p1) {
  if (sp == 0) {                                   // human: round pupil
    if (abs(row) > 5) return false;
    const int w = (int)lroundf(5.0f * sqrtf(1.0f - (float)(row * row) / 25.0f));
    *p0 = icx - w; *p1 = icx + w;
    return true;
  }
  if (sp == 1) {                                   // cat: vertical slit
    if (abs(row) > 9) return false;
    const int w = (abs(row) > 7) ? 0 : 1;
    *p0 = icx - w; *p1 = icx + w;
    return true;
  }
  if (sp == 2) {                                   // goat: horizontal bar
    if (abs(row) > 1) return false;
    *p0 = icx - 6; *p1 = icx + 6;
    return true;
  }
  return false;
}

static void eyeDrawBall(uint8_t sp, int A, int B) {
  const int icx = EYE_CX + (int)lroundf(eyeGX / EYE_CELL);
  const int icy = EYE_CY + (int)lroundf(eyeGY / EYE_CELL);
  const int Beff = (int)lroundf(B * eyeLid);
  if (Beff <= 0) return;

  for (int y = EYE_CY - Beff; y <= EYE_CY + Beff; y++) {
    const float t = (float)(y - EYE_CY) / (float)B;
    const int ws = (int)lroundf(A * sqrtf(1.0f - t * t));
    if (ws <= 0) continue;
    const int s0 = EYE_CX - ws, s1 = EYE_CX + ws;
    eyeHrun(y, s0, s1, 11);                       // sclera

    const int dyr = y - icy;
    if (abs(dyr) > EYE_IRIS) continue;
    int i0 = icx - (int)lroundf(EYE_IRIS * sqrtf(1.0f - (float)(dyr * dyr) / (float)(EYE_IRIS * EYE_IRIS)));
    int i1 = icx + (int)lroundf(EYE_IRIS * sqrtf(1.0f - (float)(dyr * dyr) / (float)(EYE_IRIS * EYE_IRIS)));
    if (i0 < s0) i0 = s0;
    if (i1 > s1) i1 = s1;
    if (i1 < i0) continue;
    eyeHrun(y, i0, i1, 1);                        // iris, in the user's primary

    int p0, p1;
    if (eyePupilSpan(sp, dyr, icx, &p0, &p1)) {
      if (p0 < i0) p0 = i0;
      if (p1 > i1) p1 = i1;
      if (p1 >= p0) eyeHrun(y, p0, p1, 0);        // pupil, black
    }
  }
  // one glint block, upper-left of the iris
  lifeSpr.fillRect((icx - 4) * EYE_CELL, (icy - 4) * EYE_CELL, 2 * EYE_CELL, 2 * EYE_CELL, 2);
}

static void eyeDrawRobot() {
  const int ccx = EYE_CX + (int)lroundf(eyeGX / EYE_CELL);
  const int ccy = EYE_CY + (int)lroundf(eyeGY / EYE_CELL);
  if (eyeLid <= 0.45f) return;                    // powering off reads as the blink

  // dark housing disc
  for (int y = EYE_CY - 17; y <= EYE_CY + 17; y++) {
    const int dy = y - EYE_CY;
    const int w = (int)lroundf(sqrtf(17.0f * 17.0f - (float)(dy * dy)));
    eyeHrun(y, EYE_CX - w, EYE_CX + w, 12);
  }
  // outer LED ring (accent) and inner ring (primary)
  for (int a = 0; a < 24; a++) {
    const float t = (float)a * 6.28318f / 24.0f;
    const int x = ccx + (int)lroundf(13.0f * cosf(t));
    const int y = ccy + (int)lroundf(13.0f * sinf(t));
    lifeSpr.fillRect(x * EYE_CELL, y * EYE_CELL, EYE_CELL, EYE_CELL, 2);
  }
  for (int a = 0; a < 12; a++) {
    const float t = (float)a * 6.28318f / 12.0f;
    const int x = ccx + (int)lroundf(7.0f * cosf(t));
    const int y = ccy + (int)lroundf(7.0f * sinf(t));
    lifeSpr.fillRect(x * EYE_CELL, y * EYE_CELL, EYE_CELL, EYE_CELL, 1);
  }
  // aperture
  lifeSpr.fillRect((ccx - 3) * EYE_CELL, (ccy - 3) * EYE_CELL, 6 * EYE_CELL, 6 * EYE_CELL, 0);
  lifeSpr.fillRect((ccx - 1) * EYE_CELL, (ccy - 1) * EYE_CELL, 2 * EYE_CELL, 2 * EYE_CELL, 2);
}

static void eyeRender() {
  lifeSpr.fillSprite(0);
  if (eyeSpecies == 3) eyeDrawRobot();
  else if (eyeSpecies == 1) eyeDrawBall(1, 21, 10);   // cat: narrower almond
  else if (eyeSpecies == 2) eyeDrawBall(2, 23, 12);   // goat: wider
  else                      eyeDrawBall(0, EYE_A, EYE_B);
  lifeSpr.pushSprite(0, 0);
}

static void eyeNewSpecies(uint8_t sp) {
  eyeSpecies = sp;
  Serial.printf("[EYE] %s\n", EYE_SPECIES_NAME[sp]);
}

void eyeInit() {
  randomSeed(esp_random());
  lifeSpr.setPaletteColor(11, tft.color565(232, 230, 218));   // sclera
  lifeSpr.setPaletteColor(12, tft.color565(28, 28, 34));      // robot housing
  eyeGX = eyeGY = 0.0f;
  eyeLid = 1.0f;
  eyeSnap = false;
  eyeNewSpecies((uint8_t)random(4));
  const unsigned long now = millis();
  eyeNextDriftAt = now + 2000;
  eyeNextSnapAt = now + 7000 + random(6000);
  eyeNextBlinkAt = now + 2500 + random(3000);
  eyeNextSpeciesAt = now + 25000 + random(25000);
  eyeSnapEnd = 0;
  eyeBlinkEnd = 0;
  eyeRender();
}

void updateEye() {
  const unsigned long now = millis();
  if (now - lastEyeUpdate < EYE_TICK_MS) return;
  lastEyeUpdate = now;

  // ---- bored drift: small targets, slow travel ----
  if (now >= eyeNextDriftAt && !eyeSnap) {
    eyeNextDriftAt = now + 2500 + random(3000);
    eyeTargetX = (float)(random(37) - 18);
    eyeTargetY = (float)(random(15) - 7);
  }
  // ---- interest: hard snap to one side, lid up, hold, then let go ----
  if (!eyeSnap && now >= eyeNextSnapAt) {
    eyeSnap = true;
    eyeSnapEnd = now + 1200 + random(1300);
    const float side = random(2) ? 1.0f : -1.0f;
    eyeTargetX = side * (22.0f + random(5));
    eyeTargetY = (float)(random(9) - 4);
  }
  if (eyeSnap && now >= eyeSnapEnd) {
    eyeSnap = false;
    eyeNextSnapAt = now + 7000 + random(11000);
    eyeNextDriftAt = now + 400;
  }
  const float k = eyeSnap ? 0.42f : 0.10f;
  eyeGX += (eyeTargetX - eyeGX) * k;
  eyeGY += (eyeTargetY - eyeGY) * k;

  // ---- blink ----
  if (!eyeBlinkEnd && now >= eyeNextBlinkAt) {
    eyeBlinkEnd = now + 180;
    // species rotates while the lid is shut, so the swap is never seen
    if (now >= eyeNextSpeciesAt) {
      eyeNextSpeciesAt = now + 25000 + random(25000);
      uint8_t sp = (uint8_t)random(4);
      if (sp == eyeSpecies) sp = (uint8_t)((sp + 1) % 4);
      eyeNewSpecies(sp);
    }
  }
  if (eyeBlinkEnd) {
    if (now >= eyeBlinkEnd) { eyeBlinkEnd = 0; eyeNextBlinkAt = now + 2500 + random(4000); }
  }
  const float wantLid = eyeBlinkEnd ? 0.0f : (eyeSnap ? 1.12f : 1.0f);
  eyeLid += (wantLid - eyeLid) * (eyeBlinkEnd ? 0.55f : 0.25f);

  eyeRender();
}
void renderDisplay() {
  switch (activeMode) {
    case MODE_INDICATOR:   updateMarquee(); break;
    case MODE_COIN:        updateCoin(); break;
    case MODE_PIPES:       updatePipes(); break;
    case MODE_STATIC:      updateLife(); break;
    case MODE_NOW_PLAYING: updateNowPlaying(); break;
    case MODE_BOIDS:       updateBoids(); break;
    case MODE_ANT:         updateAnt(); break;
    case MODE_FIRE:        updateFire(); break;
    case MODE_KNIGHT:      updateKnight(); break;
    case MODE_RADAR:       updateTama(); break;
    case MODE_STARFIELD:   updateDla(); break;
    case MODE_OSC:         updateSnake(); break;
    case MODE_RALLY:        updateRally(); break;
    case MODE_EQ:          updateEye(); break;
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
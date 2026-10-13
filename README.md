# EyeSwap

Personal Media & Navigation Controller for Vehicle Use

**Copyright © 2026 Shawn Baird**

---

## ⚠️ CRITICAL SAFETY DISCLAIMER

**DO NOT USE THIS DEVICE WHILE OPERATING A MOTOR VEHICLE.**

**DO NOT USE THIS DEVICE ON PUBLIC ROADS.**

**DO NOT USE THIS DEVICE NEAR OTHER HUMANS.**

**DO NOT USE THIS DEVICE WITHOUT:**
- Insurance (life, liability, and a good lawyer on retainer)
- Your mother's explicit written permission and direct supervision
- A blast shelter with air-gapped supercomputer access
- A complete psychological evaluation confirming you should not be doing this

EyeSwap is designed for **stationary fleet vehicles** in **controlled environments**. It is not a replacement for attention, situational awareness, or basic common sense. It will absolutely not prevent you from driving into a ditch while arguing with Audible's chapter skip timing.

**The author, contributors, and this README itself assume zero liability for:**
- Distracted driving incidents
- Fleet manager disciplinary actions
- Insurance claim denials
- Your mother being disappointed in you

> The Android app now shows this notice on first launch, before any permissions prompt. Tap **I UNDERSTAND** once and it won't nag again.

---

## Android App

Flutter-based Android controller — live BLE link to the EyeSwap hardware, with the on-screen 3-button UI for bench testing.

### Features
- BLE connection to EyeSwap hardware (flutter_blue_plus, 512-byte MTU)
- **Auto-reconnect**: watches the BLE link and rescans automatically after a pod reboot or dropout (~3 s retry), so the app no longer strands on "SCANNING" until a manual restart
- Startup prompt + "!" indicator for missing Notification Access / Accessibility / runtime permissions (checks the real component names, so it stops nagging once granted)
- **First-launch safety disclaimer** (see above)
- Settings screen wired end-to-end: brightness offset, **primary + accent color**, idle animation mode
- Now Playing: polls Android media sessions every 4 s, pushes title/artist/state/source only on change
- 3-button digital representation of physical EyeSwap
- Round display simulation (240×240 GC9A01 equivalent)
- Scrolling text effects for status/feedback
- Media control: YouTube Music ↔ Audible switching
- Black/grey/red chunky UI aesthetic

### Idle Animation Modes
Selectable from the settings dropdown; the choice is saved and re-pushed on connect.

| # | Mode | What it does |
|---|------|--------------|
| 0 | **Text Marquee** | Scrolling one-way ticker |
| 1 | **Game of Life** | Conway's Game of Life, 120×120 torus, reseeds when it dies/freezes |
| 2 | **Now Playing** | PLAYING/PAUSED + title + artist card |
| 3 | **Boids** | Flocking sim — "little fish" 🐟 |
| 4 | **Langton's Ant** | Two-state turmite trail |
| 5 | **Fire** | Doom-style rising flame |
| 6 | **Pipes** | 5 plumbers wandering a 7×7×7 lattice, ortho 3/4 view |
| 7 | **Radar Sweep** | Phosphor radar with rotating beam + fading blips |
| 8 | **Starfield** | Warp streaks rushing out of the center |
| 9 | **Oscilloscope** | Lissajous trace over a faint graticule |
| 10 | **Pong** | Self-playing pong against AI paddles |
| 11 | **Spectrum (EQ)** | Fake LED spectrum bars with a synthetic beat |
| — | **Random Cycle** | Auto-rotates through the visual roster every **10 minutes** |

**Two-tone color:** every animation uses a **primary** and an **accent** color. The accent highlights the element that should pop — the white-hot core of the fire, every fifth boid, the ant itself, pipe joints/end caps, the radar sweep beam, the oscilloscope trace, starfield warp streaks, pong paddles + ball, and the EQ peak-hold caps. Set **Accent = primary** (or hit **Match primary**) for a single-color look. Both colors ride in the config packet and persist in NVS.

### Controls
Physical pod buttons map to functions in the app (configurable per button, short + long press):

- **Button 1 (Media A)**: Focus & Autoplay YouTube Music
- **Button 2 (Media B)**: Focus & Autoplay Audible
- **Button 3 (Comm)**: Deny Incoming Call + Send Custom SMS

### Long Press Actions
- Hold Button 1: Next Track
- Hold Button 2: Skip 30 Seconds Backward
- Hold Button 3: Accept Call (Bluetooth → Vehicle Speakers)

The default mappings are baked into the app; the **Button Config** screen overrides them per button.

---

## Hardware Notes

Designed for older fleet vehicles without Android Auto support. Physical button interface reduces touchscreen dependency during operation.

### Wiring

**Round display — GC9A01, 240×240, SPI** (mirrors the firmware `build_flags` in `eyeswap-firmware/platformio.ini`):

| Signal | ESP32-S3 GPIO | Notes |
|--------|---------------|-------|
| MOSI / SDA | 13 | `TFT_MOSI` |
| SCLK / SCL | 14 | `TFT_SCLK` |
| MISO | — | not used (`TFT_MISO = -1`) |
| CS | 10 | `TFT_CS` |
| DC | 8 | `TFT_DC` |
| RST | 9 | `TFT_RST` |
| BL | 7 | **Not connected on this module** — no backlight pin, so brightness is approximated in firmware by scaling the draw color |
| VCC / GND | 3V3 / GND | — |

**Buttons** (INPUT_PULLUP, active LOW, long-press threshold 600 ms):

| Button | GPIO | Default action |
|--------|------|----------------|
| 1 | 4 | YouTube Music |
| 2 | 5 | Audible |
| 3 | 6 | Call / Gemini |

> Pin 3 is avoided (reset/boot strap). See the firmware's `BTN_*_PIN` defines.

### ESP32 Firmware

Located in `eyeswap-firmware/`. PlatformIO / Arduino ESP32-S3 firmware for the GC9A01 round display and BLE communication.

**Implemented:**
- BLE GATT service for app communication (config characteristic takes JSON commands, button characteristic notifies taps/long-presses)
- Button press detection (tap / long-press)
- Display brightness + **two-tone color** config (brightness approximated by color scaling — no backlight pin)
- **12 idle animations + Random Cycle** (see table above)
- **Random Cycle** persists across power cycles (NVS), and any explicit pick cancels it
- **Settings persistence**: mode, brightness, primary color, accent color, and cycle state all survive a power cycle
- One-writer rendering rule: BLE callbacks only record state; the loop task does all TFT/SPI work (two tasks racing one SPI bus corrupts the transaction mutex and panics)
- Shared animation sprite is a 4bpp palette (index 0 = black, 1 = primary, 2 = accent)

**Flashing:**
```bash
cd eyeswap-firmware
pio run -t upload --upload-port /dev/ttyUSB0
# serial monitor @ 115200
pio device monitor -p /dev/ttyUSB0 -b 115200
```

**TODO / Stubs:**
- **Dual long-press**: Buttons 1+3 held together kills display (privacy mode)
- **Time sync**: App sends current time on connect for clock display
- **Boot screen**: Show welcome ASCII art when powered on / disconnected

---

## Protocol

BLE UUIDs (must match the Flutter app exactly):

| Role | UUID |
|------|------|
| Service | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Config characteristic (app → ESP32, write) | `beb5483e-36e1-4688-b7f5-ea07361b26a8` |
| Button characteristic (ESP32 → app, notify) | `a1e60244-960d-4d06-aca2-a2fc604f09be` |

```json
// App → ESP32 config (brightness, primary + accent color)
{"type":"config","brightness_offset":0,"bg_color":"#FFAA00","accent_color":"#0066FF"}

// App → ESP32 animation mode
//   marquee | static | now_playing | boids | ant | fire | pipes |
//   radar | starfield | oscilloscope | pong | eq | cycle
{"type":"mode","animation":"fire"}
{"type":"mode","animation":"cycle"}   // Random Cycle ("random" also accepted)

// App → ESP32 now-playing metadata (pushed only on change)
{"type":"now_playing","title":"Song","artist":"Artist","state":"playing","source":"com.google.android.apps.youtube.music"}

// ESP32 → App button event
{"type":"button","button":1,"action":"tap"}

// ESP32 → App (boot status)
{"type":"status","state":"boot"}

// App → ESP32 (init response with time + animation)
{"type":"init","time":"13:45","animation":"marquee"}

// ESP32 → App (disconnected from phone)
{"type":"status","state":"searching"}
```

**Reserved boot screen (stub):**
```
        WELCOME TO EYESWAP
         [SEARCHING...]
**Not your mother. Operate responsibly.**
```

---

## Building the Android APK

The APK is built in CI (GitHub Actions) on every push to `master`/`main` — workflow `.github/workflows/build.yml` (Flutter 3.22.0, Android SDK 34). The release artifact is uploaded as **`eyeswap-ble-apk`** from each run. Local build (if you have Flutter + the Android SDK):

```bash
flutter pub get
dart run flutter_launcher_icons:generate
flutter build apk --release
```

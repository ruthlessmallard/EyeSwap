# EyeSwap

**Screen-free media control.**

> **All the media. None of the machine.**

Pesky controlling boss looking over your shoulder? Trying to keep your media
away from the doomscroll? EyeSwap puts three tactile buttons and a small round
status display on your desk — and your phone in your pocket.

EyeSwap is for people who've caught themselves staring at a screen they didn't
mean to open. The phone stays in your pocket, dark, doing nothing but playing
the music. The controls live on your desk where your hand already is — three
buttons and a small round display that shows you what's playing and nothing
else. No feed. No notifications. No doomscroll between you and the next track.
It's the media you actually wanted, with the slot machine left out.

*Play, pause, skip — without the casino that came attached to your phone.*

> **Not for vehicle use. Not for use in dangerous situations. Not a workaround
> for screen-free or device-prohibited situations.**

---

## What it is

- Your **phone** does the actual media work (YouTube Music, Audible, any
  Android media session) and acts as the brain.
- The **pod** is the front panel: three tactile buttons, a round 240×240
  display, and a BLE link to the phone. No touchscreen, no screen time.
- The phone stays dark and out of reach — you get the music without the
  machine that comes with it.

The author makes no claims about workplace rules, privacy policies, or how your
boss interprets a small round display. That's between you, them, and your
mother's explicit written permission.

---

## Android App

Flutter-based Android controller — live BLE link to the EyeSwap hardware, with
the on-screen 3-button UI for bench testing.

### Features

- BLE connection to EyeSwap hardware (flutter_blue_plus, 512-byte MTU)
- **Auto-reconnect**: watches the BLE link and rescans automatically after a
  pod reboot or dropout (~3 s retry), so the app no longer strands on
  "SCANNING" until a manual restart
- Startup prompt + "!" indicator for missing Notification Access / Accessibility
  / runtime permissions (checks the real component names, so it stops nagging
  once granted)
- **First-launch scope notice** (see above)
- Settings screen wired end-to-end: brightness offset, **primary + accent
  color**, idle animation mode
- Now Playing: polls Android media sessions every 4 s, pushes
  title/artist/state/source only on change
- 3-button digital representation of physical EyeSwap
- Round display simulation (240×240 GC9A01 equivalent)
- Scrolling text effects for status/feedback
- Media control: YouTube Music ↔ Audible switching
- Black/grey/red chunky UI aesthetic

### Marquee: connection indicator, not an animation

The scrolling EYESWAP marquee is no longer selectable. It is the pod's link
indicator:

- Boot, or any disconnect → marquee for **5 minutes**, or until the app connects.
- Still unlinked after 5 minutes → the user's chosen animation plays.
- Every **10 minutes** of unlinked animation → **one full scroll pass** of the
  marquee, then back to the animation.
- If the chosen animation is **Now Playing** and nothing is linked, a random
  animation is substituted — there is no track to show without the phone.

`{"type":"mode","animation":"marquee"}` is still accepted, but it only runs one
pass as a test; it is never saved as your choice.

### Idle Animation Modes

Selectable from the settings dropdown; the choice is saved and re-pushed on
connect.

| Mode | What it does |
|------|--------------|
| **Game of Life** | Conway's Game of Life, 120×120 torus, reseeds when it dies/freezes |
| **Now Playing** | PLAYING/PAUSED + title + artist card |
| **Boids** | Flocking sim — "little fish" 🐟 |
| **Fire** | Doom-style rising flame |
| **Knight Run** | Self-playing runner — hops hay bales and brick walls, ducks under arrows |
| **Pet** | Ambient pixel-goo creature; feeds, plays, and tidies itself |
| **Crystal** | Diffusion-limited aggregation; grows, holds, regrows |
| **Quickdraw** | Pixel eyeball duel — draw on the tell |
| **Random Cycle** | Auto-rotates through the ambient roster every **10 minutes** |

Random Cycle runs Game of Life, Boids, Fire, Knight Run, Pet, and Crystal.
Quickdraw and Now Playing are pick-only: they are things you choose, not
background ambience.

**Two-tone color:** every animation uses a **primary** and an **accent** color.
The accent highlights the element that should pop — the white-hot core of the
fire, every fifth boid, the goo creature's eyes, crystal rim, the eyeball's
iris, the knight's plume + blade, and the runner's high score. Set
**Accent = primary** (or hit **Match primary**) for
a single-color look. Both colors ride in the config packet and persist in NVS.

### Controls

Physical pod buttons map to functions in the app (configurable per button,
short + long press):

- **Button 1 (Media A)**: Focus & Autoplay YouTube Music
- **Button 2 (Media B)**: Focus & Autoplay Audible
- **Button 3 (Optional — calls)**: Deny Incoming Call + Send Custom SMS

### Long Press Actions

- Hold Button 1: Next Track
- Hold Button 2: Skip 30 Seconds Backward
- Hold Button 3: Accept Call (Bluetooth → Speaker)

The default mappings are baked into the app; the **Button Config** screen
overrides them per button.

---

## Hardware Notes

Designed for a desk, not a dashboard — three tactile buttons and a round status
display, because reaching for a phone to change the song is how you end up
reading a feed.

### Wiring

**Round display — GC9A01, 240×240, SPI** (mirrors the firmware `build_flags` in
`eyeswap-firmware/platformio.ini`):

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

Located in `eyeswap-firmware/`. PlatformIO / Arduino ESP32-S3 firmware for the
GC9A01 round display and BLE communication.

**Implemented:**
- BLE GATT service for app communication (config characteristic takes JSON
  commands, button characteristic notifies taps/long-presses)
- Button press detection (tap / long-press)
- Display brightness + **two-tone color** config (brightness approximated by
  color scaling — no backlight pin)
- **8 idle animations + Random Cycle** (see table above)
- **Random Cycle** persists across power cycles (NVS), and any explicit pick
  cancels it
- **Settings persistence**: mode, brightness, primary color, accent color, and
  cycle state all survive a power cycle
- One-writer rendering rule: BLE callbacks only record state; the loop task
  does all TFT/SPI work (two tasks racing one SPI bus corrupts the transaction
  mutex and panics)
- Shared animation sprite is a 4bpp palette (index 0 = black, 1 = primary,
  2 = accent)

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
//   static | now_playing | boids | fire | knight |
//   pet | crystal | eye | cycle
//   (legacy aliases still accepted: radar, starfield, eq)
//   ("marquee" is accepted but only runs one indicator pass; it is not a mode)
//   Add "resync":true when re-pushing stored settings on connect -- the pod
//   ignores a resync while it is Random Cycling, so a reconnect cannot stop it.
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

The APK is built in CI (GitHub Actions) on every push to `master`/`main` —
workflow `.github/workflows/build.yml` (Flutter 3.22.0, Android SDK 34). The
release artifact is uploaded as **`eyeswap-ble-apk`** from each run. Local
build (if you have Flutter + the Android SDK):

```bash
flutter pub get
dart run flutter_launcher_icons:generate
flutter build apk --release
```

---

## License

EyeSwap's own code is licensed under **Creative Commons
Attribution-NonCommercial-NoDerivatives 4.0 International (CC BY-NC-ND 4.0)** —
see [`LICENSE`](LICENSE).

You may **view, run, and share this work verbatim** for non-commercial purposes,
with credit. You may **not** modify it, rebrand it, or use it commercially.

Full terms: https://creativecommons.org/licenses/by-nc-nd/4.0/

Bundled third-party components (Flutter packages, TFT_eSPI, ArduinoJson,
NimBLE-Arduino) keep their own licenses — see
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

EyeSwap is a **personal project, shared as-is** — see the scope notice at the
top. It is not certified or validated for safety-critical use.

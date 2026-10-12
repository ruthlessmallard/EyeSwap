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

---

## Android App (v1.0.0)

Flutter-based Android controller — live BLE link to the EyeSwap hardware, with the on-screen 3-button UI for bench testing.

### Shipped in v1.0.0
- BLE connection to EyeSwap hardware (flutter_blue_plus, 512-byte MTU)
- Startup prompt + "!" indicator for missing Notification Access / Accessibility / runtime permissions
- Settings screen wired end-to-end: brightness offset, display color, idle animation mode
- Now Playing: polls Android media sessions every 4 s, pushes title/artist/state/source only on change
- Fixed: manifest now declares the real notification listener (media sessions were never reachable before)

### Features
- 3-button digital representation of physical EyeSwap
- Round display simulation (240x240 GC9A01 equivalent)
- Scrolling text effects for status/feedback
- Media control: YouTube Music ↔ Audible switching
- Black/grey/red chunky UI aesthetic

### Controls
- **Button 1 (Media A)**: Focus & Autoplay YouTube Music
- **Button 2 (Media B)**: Focus & Autoplay Audible
- **Button 3 (Comm)**: Deny Incoming Call + Send Custom SMS

### Long Press Actions
- Hold Button 1: Next Track
- Hold Button 2: Skip 30 Seconds Backward
- Hold Button 3: Accept Call (Bluetooth → Vehicle Speakers)

---

## Hardware Notes

Designed for older fleet vehicles without Android Auto support. Physical button interface reduces touchscreen dependency during operation.

### ESP32 Firmware (v1.0.0)

Located in `eyeswap-firmware/` directory. PlatformIO / Arduino ESP32-S3 firmware for GC9A01 round display and BLE communication.

**Implemented:**
- BLE GATT service for app communication (config characteristic takes JSON commands, button characteristic notifies taps)
- Button press detection (tap/long-press)
- Display brightness and color config (brightness approximated by text-color scaling — no backlight pin)
- **Idle Animations**: amber marquee ticker, Conway's Game of Life, now-playing card
  - Game of Life: 120x120 torus field, 2x2 px cells, reseeds when the soup dies or freezes (`LIFE_CELL` is the zoom knob)
  - Now Playing: PLAYING/PAUSED + title (auto-shrink, then scroll when too wide) + artist, pushed only on change
- One-writer rendering rule: BLE callbacks only record state; the loop task does all TFT/SPI work (two tasks racing one SPI bus corrupts the transaction mutex and panics)

**TODO / Stubs:**
- **Dual long-press**: Buttons 1+3 held together kills display (privacy mode)
- **Time sync**: App sends current time on connect for clock display
- **Boot screen**: Show welcome ASCII art when powered on / disconnected

**Protocol:**
```json
// App → ESP32 config
{"type":"config","brightness_offset":0,"bg_color":"#FFAA00"}

// App → ESP32 animation mode
{"type":"mode","animation":"marquee"}

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

**Boot ASCII Art (240x240 GC9A01):**
```
       _____                _   
      | ____|__ _ _ __   __| |  
      |  _| / _` | '_ \ / _` |  
      | |__| (_| | | | | (_| |  
      |_____\__, |_| |_|\__,_|  
            |___/               

        WELCOME TO EYESWAP
         [SEARCHING...]
```
Display this centered on boot. Switch to clock + animation mode after app connects and sends `{"type":"init"}`.

**Not your mother. Operate responsibly.**

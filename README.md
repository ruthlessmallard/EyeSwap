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

## V0.05 Prototype

Flutter-based Android app for testing the EyeSwap control schema before hardware build.

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

### ESP32 Firmware (WIP)

Located in `eyeswap-firmware/` directory. Arduino-based ESP32-S3 firmware for GC9A01 round display and BLE communication.

**Implemented:**
- BLE GATT service for app communication
- Button press detection (tap/long-press)
- Display brightness and color config
- JSON command protocol

**TODO / Stubs:**
- **Idle Animations**: Marquee text scroll, static noise, now playing modes
  - App sends: `{"type":"mode","animation":"marquee|static|now_playing"}`
  - ESP32 renders selected animation when no media active
  - `now_playing` shows track metadata received from app
- **Dual long-press**: Buttons 1+3 held together kills display (privacy mode)
- **Now Playing**: Receive metadata from app, display on round screen
- **Time sync**: App sends current time on connect for clock display
- **Boot screen**: Show welcome ASCII art when powered on / disconnected

**Protocol:**
```json
// App → ESP32 config
{"type":"config","brightness_offset":0,"bg_color":"#FFAA00"}

// App → ESP32 animation mode
{"type":"mode","animation":"marquee"}

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

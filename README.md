# XPAD-NEO

Open-source Arduino firmware for the XTIA XPAD keyboard platform.

Designed as a learning resource for embedded vibe-coding with AI assistance.
All source files are annotated with explanations of the underlying concepts.

---

## Features

| Feature | Status |
|---|---|
| MX mechanical switch scanning | ✅ Implemented |
| ADC hall-effect key scanning | ✅ Implemented |
| EMA noise filtering | ✅ Implemented |
| Fixed-threshold trigger | ✅ Implemented |
| Rapid Trigger mode | ✅ Implemented |
| USB HID keyboard (6KRO) | ✅ Implemented |
| WebUSB config protocol | ✅ Implemented |
| XTIA web UI compatible | ✅ Compatible |
| LittleFS config persistence | ✅ Implemented |
| Rotary encoder | 🔲 FEATURE_ENCODER |
| Text macros | 🔲 FEATURE_MACRO |
| I2S microphone | 🔲 FEATURE_MIC |
| Vibration motor | 🔲 FEATURE_RUMBLE |

---

## Hardware

- **MCU**: Waveshare RP2040 Zero (same as used in XPAD and GP2040-CE)
- **ADC keys**: GP26, GP27, GP28, GP29 (hall-effect, 12-bit ADC)
- **MX keys**: GP0–GP5 by default (configurable in `config.h`)

---

## Arduino IDE Setup

### 1. Install the arduino-pico board package

In Arduino IDE → File → Preferences → Additional Boards Manager URLs, add:

```
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

Then go to Tools → Board → Boards Manager, search for **rp2040** and install
**Raspberry Pi RP2040 Boards** by Earle F. Philhower III.

### 2. Select your board

Tools → Board → Raspberry Pi RP2040 Boards → **Waveshare RP2040 Zero**

### 3. Select USB Stack

Tools → USB Stack → **Adafruit TinyUSB**

This is required. The default "Pico SDK" USB stack does not support the
composite HID + Vendor device used here.

### 4. Set USB VID/PID

The XTIA web UI filters USB devices by Vendor ID `0xCafe`. You need to set
this before uploading.

**Option A — build flags (PlatformIO / command line):**
```
-DUSB_VID=0xCafe -DUSB_PID=0x4002
```

**Option B — edit `boards.txt`:**
Find your arduino-pico installation (usually in
`~/AppData/Local/Arduino15/packages/rp2040/hardware/rp2040/x.x.x/`),
open `boards.txt`, find the `waveshare_rp2040_zero` section, and change:
```
waveshare_rp2040_zero.build.vid=0x2E8A
waveshare_rp2040_zero.build.pid=0x000a
```
to:
```
waveshare_rp2040_zero.build.vid=0xCafe
waveshare_rp2040_zero.build.pid=0x4002
```

### 5. Upload

Hold the BOOT button on the RP2040, connect USB, release BOOT. The board
appears as a USB drive. Click Upload in Arduino IDE.

---

## Using the XTIA Web UI

Open `http://118.31.120.202` (or your local server) in Chrome or Edge.

- **Layout Generator** — assign keys and download STL/SCAD files
- **ADC Calibration** — calibrate hall-effect axes and set trigger thresholds
- Click **Read from XPAD** to connect via WebUSB

The web UI works identically with XPAD firmware and XPAD-NEO firmware.

---

## Adding a New Feature

### Example: add encoder support

1. Uncomment `#define FEATURE_ENCODER` in `config.h`
2. Create `encoder.h` and `encoder.cpp` with `encoder_setup()` and `encoder_task()`
3. To add a new USB command, add one entry to `kHandlers[]` in `webusb_handler.cpp`

That's it. The main sketch (`xpad_neo.ino`) already has the `#ifdef FEATURE_ENCODER`
hooks in place.

---

## File Overview

```
xpad_neo/
├── xpad_neo.ino          Entry point: setup() and loop()
├── config.h              Pin assignments, feature flags, USB IDs
├── xpad_config.h         Shared config struct (binary-compatible with XPAD)
├── flash_storage.h/.cpp  LittleFS read/write
├── adc_keys.h/.cpp       ADC hall-effect scanning + EMA + trigger logic
├── mx_keys.h/.cpp        MX switch scanning + debounce
├── hid_keyboard.h/.cpp   USB HID keyboard reports
└── webusb_handler.h/.cpp USB config protocol dispatch table
```

---

## Learning Resources

The code is annotated with explanations of key concepts. Suggested reading order:

1. `config.h` — understand what can be configured
2. `xpad_config.h` — understand the data model
3. `mx_keys.cpp` — simple digital I/O + debounce
4. `adc_keys.cpp` — analog reading + EMA filter + trigger logic
5. `hid_keyboard.cpp` — USB HID reports
6. `webusb_handler.cpp` — protocol dispatch pattern
7. `xpad_neo.ino` — how everything connects in the main loop

Paste any file into your preferred AI assistant and ask it to explain sections
you don't understand, or ask it to help you add new features.

---

## License

MIT — free to use, modify, and distribute.

---

## Acknowledgements

Special thanks to the **TinyUSB** project and the **GP2040-CE** team for their
outstanding work and the inspiration they provided for this project.

- [TinyUSB](https://github.com/hathach/tinyusb) — the lightweight USB stack that makes embedded HID and vendor-class devices accessible to everyone.
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) — an open-source gamepad firmware for RP2040 that demonstrated how powerful this platform can be, and directly inspired the hall-effect calibration model used in XPAD-NEO.



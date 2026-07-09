# XPAD-NEO

XPAD-NEO is a single-file Arduino sketch for an MX-only XPAD mini keyboard.

It is intentionally small: one `.ino` file scans mechanical MX switches and
sends normal USB HID keyboard reports. The project is aimed at beginner-friendly
hardware testing and teaching, not full XPAD firmware compatibility.

## Current Status

| Feature | Status |
|---|---|
| MX mechanical switch scanning | Supported |
| USB HID keyboard output | Supported |
| Single-file Arduino sketch | Supported |
| Default Notepad test output | `1` to `8` |
| WebUSB / Layout Generator | Deferred |
| LittleFS config storage | Removed |
| ADC / magnetic axis keys | Not supported |
| Encoder | Not supported |
| Microphone | Not supported |
| Vibration | Not supported |
| Macro playback | Not supported |

## Source Layout

```text
xpad_neo/
  xpad_neo.ino
```

The active firmware is only [xpad_neo.ino](xpad_neo/xpad_neo.ino).

Generated build output under `xpad_neo/build/` is ignored and should not be
committed.

## Default Test Mapping

The sketch currently assumes eight active-low MX switches on GP0-GP7:

```cpp
static const MxKey MX_KEYS[] = {
    { 0, 0x1E, 0 }, // 1
    { 1, 0x1F, 0 }, // 2
    { 2, 0x20, 0 }, // 3
    { 3, 0x21, 0 }, // 4
    { 4, 0x22, 0 }, // 5
    { 5, 0x23, 0 }, // 6
    { 6, 0x24, 0 }, // 7
    { 7, 0x25, 0 }, // 8
};
```

Open Notepad after flashing. Pressing the eight MX keys should type `1` through
`8` if the GPIO map matches the hardware.

## Arduino IDE Setup

1. Install Arduino IDE 2.x.
2. Open File > Preferences.
3. Add this Boards Manager URL:

```text
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

4. Install this board package:

```text
Raspberry Pi Pico/RP2040/RP2350 by Earle F. Philhower, III
```

5. Select the board currently being flashed. For the user's current test board:

```text
Board: Raspberry Pi Pico
USB Stack: Adafruit TinyUSB
```

For Waveshare RP2040 Zero hardware, select:

```text
Board: Waveshare RP2040 Zero
USB Stack: Adafruit TinyUSB
```

No LittleFS partition is required by the current single-file firmware.

## Command-Line Compile

Arduino IDE includes `arduino-cli`. On this machine it was found at:

```text
C:\Users\steven\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe
```

Compile for Raspberry Pi Pico:

```powershell
& 'C:\Users\steven\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn rp2040:rp2040:rpipico:usbstack=tinyusb --warnings all 'D:\tempFiles\vibecoding\XPAD-NEO-master\xpad_neo'
```

Recent compile result:

```text
Sketch uses 78536 bytes (3%) of program storage space.
Global variables use 16972 bytes (6%) of dynamic memory.
```

## Hardware Test

1. Flash the sketch from Arduino IDE.
2. Open Notepad.
3. Press each MX key once.
4. Expected output is `1`, `2`, `3`, `4`, `5`, `6`, `7`, `8`.

If no key types anything, the most likely issue is that the real XPAD MX GPIO
pins are not GP0-GP7. In that case, use a temporary GPIO diagnostic sketch and
then update the `MX_KEYS` table.

## Deferred Work

Layout Generator compatibility is deferred until the leader provides the main
XPAD source or protocol details. It should be added later as a deliberate task,
not guessed into the current beginner firmware.

## Development Rules

- Keep the active firmware MX-only.
- Keep the firmware one `.ino` file unless there is a clear reason to split it.
- Prioritize compiling, flashing, and hardware testing.
- Do not add WebUSB, LittleFS, ADC, magnetic axis, encoder, microphone,
  vibration, or macro support without a new explicit requirement.
- Do not change UI or visual assets in this firmware task.

## License

MIT - free to use, modify, and distribute.

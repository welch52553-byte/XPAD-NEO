# XPAD-NEO

XPAD-NEO is a single-file Arduino sketch for an MX-only XPAD mini keyboard.

It is intentionally small: one `.ino` file scans mechanical MX switches and
sends normal USB HID keyboard reports. The project is aimed at beginner-friendly
hardware testing and teaching, not full XPAD firmware compatibility.

The firmware is designed to work without the original XPAD host tools. When no
valid Flash layout is available, it uses the hardcoded default GPIO-to-HID
mapping near the top of `xpad_neo.ino`. In a later task, a valid Flash layout
can override those defaults once the layout format is known.

## Current Status

| Feature | Status |
|---|---|
| MX mechanical switch scanning | Supported |
| USB HID keyboard output | Supported |
| Single-file Arduino sketch | Supported |
| Default HID output | `F13` to `F20` |
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

## Default HID Mapping

The sketch currently assumes eight active-low MX switches on GP0-GP7. This is
the default fallback mapping and should stay easy to find near the top of
`xpad_neo.ino`:

```cpp
static const MxKey DEFAULT_MX_KEYS[] = {
    { 0, 0x68, 0 }, // F13
    { 1, 0x69, 0 }, // F14
    { 2, 0x6A, 0 }, // F15
    { 3, 0x6B, 0 }, // F16
    { 4, 0x6C, 0 }, // F17
    { 5, 0x6D, 0 }, // F18
    { 6, 0x6E, 0 }, // F19
    { 7, 0x6F, 0 }, // F20
};
```

After flashing, use any tool that can observe keyboard HID events or react to
function keys. For Small Deck testing, enable **Show all F13-F24**, bind actions
to `F13` through `F20`, then press each MX key.

Future layout loading should follow this priority:

1. Use a valid Flash layout if one exists.
2. Otherwise use the hardcoded fallback mapping above.

The current firmware does not parse a real Flash layout yet because the stored
layout format and protocol are not defined in this repository.

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
2. Confirm the device enumerates as a USB keyboard.
3. Choose a test target for the current task:
   - keyboard event viewer or OS-level hotkey tool for raw HID checks
   - Small Deck with **Show all F13-F24** enabled for launcher checks
   - a temporary diagnostic sketch if GPIO mapping is unknown
4. Press each MX key once.
5. Expected behavior: each key produces the configured HID output.

If no key types anything, the most likely issue is that the real XPAD MX GPIO
pins are not GP0-GP7. In that case, use a temporary GPIO diagnostic sketch and
then update the `DEFAULT_MX_KEYS` table.

## Deferred Work

Layout Generator or Flash layout compatibility is deferred until the leader
provides the main XPAD source, protocol details, or stored layout format. It
should be added later as a deliberate task, not guessed into the current
beginner firmware.

## Development Rules

- Keep the active firmware MX-only.
- Keep the firmware one `.ino` file unless there is a clear reason to split it.
- Prioritize compiling, flashing, and hardware testing.
- Do not add WebUSB, LittleFS, ADC, magnetic axis, encoder, microphone,
  vibration, or macro support without a new explicit requirement.
- Do not change UI or visual assets in this firmware task.

## License

MIT - free to use, modify, and distribute.

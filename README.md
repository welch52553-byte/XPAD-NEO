# XPAD-NEO

XPAD-NEO is a small Arduino firmware project for an eight-key MX mechanical
keyboard based on RP2040.

The firmware scans active-low MX switches, sends standard USB HID keyboard
reports, and provides the minimal WebUSB protocol required by the Layout
Generator at `https://xtiaconfiger.com`.

XPAD-NEO is intentionally not a full XPAD firmware clone. Magnetic switches,
encoders, microphones, vibration, macro playback, and non-Layout-Generator
tools are outside the project scope.

## Current Status

| Feature | Status |
|---|---|
| Eight MX mechanical switches | Supported |
| Software debounce | Supported |
| USB HID keyboard output | Supported |
| Default output | `F13` through `F20` |
| Layout Generator connection | Supported |
| Layout/keymap read and write | Supported |
| MX input test mode | Supported |
| Flash preset persistence | Supported |
| Invalid preset fallback | Supported |
| LittleFS | Not used |
| Magnetic/ADC switches | Not supported |
| Encoder, microphone, vibration | Not supported |
| Macro playback | Not supported |
| Full XPAD protocol | Not supported |

The firmware has passed compile, flash, Layout Generator, HID, and unplug/replug
persistence tests on Raspberry Pi Pico hardware.

## Project Structure

```text
xpad_neo/
  xpad_neo.ino             Beginner-facing MX scan and HID flow
  layout_generator.ino     WebUSB protocol and Flash preset storage
```

Arduino IDE displays these as two tabs in one sketch. Arduino combines both
`.ino` files into one firmware image; there is no custom library or framework.

Start with `xpad_neo.ino` when learning the project. Its main path is:

```text
MX switch -> GPIO -> debounce -> HID report -> computer
```

`layout_generator.ino` is the advanced compatibility layer and can be read
later.

Generated output under `xpad_neo/build/` is ignored and should not be committed.

## Hardware Model

XPAD-NEO always has eight MX switch positions. Each switch is active-low:

```text
released: GPIO reads HIGH through INPUT_PULLUP
pressed:  switch connects GPIO to GND, so GPIO reads LOW
```

The built-in fallback mapping is kept near the top of `xpad_neo.ino`:

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

`F13-F20` avoid normal typing conflicts and work well with Small Deck and other
shortcut applications. Notepad normally shows no visible text for these keys;
use a keyboard event viewer or enable **Show all F13-F24** in Small Deck.

If the real board uses different GPIO pins, update only the first field in each
entry after confirming the wiring.

## Preset Selection and Flash

Startup uses this order:

1. Load the saved XPAD-NEO preset from RP2040 Flash.
2. Validate its magic, version, size, checksum, GPIO values, and eight-key limit.
3. Use the saved preset when validation succeeds.
4. Otherwise use `DEFAULT_MX_KEYS`.

Layout Generator writes update the active HID mapping and are committed through
the arduino-pico `EEPROM` Flash emulation library. LittleFS is not required.

The storage record is intentionally local to XPAD-NEO. Corrupt or incompatible
records are rejected instead of being partially applied.

## Layout Generator Compatibility

The USB interface layout is:

```text
Interface 0: HID Keyboard
Interface 1: WebUSB Vendor
Vendor OUT:  endpoint 2
Vendor IN:   endpoint 2
```

The firmware handles only the commands needed by the current Layout Generator:

```text
0x30  write keymap
0x31  write layout matrix
0x32  read layout matrix
0x33  read keymap
0x34  read MX input state
0x35  acknowledge macro write without macro support
0x36  return an empty macro slot
0x37  acknowledge encoder write without encoder support
0x38  return an empty encoder configuration
0x39  acknowledge vibration test without vibration support
```

Incoming layouts are rejected when they exceed eight keys, contain duplicate or
invalid GPIO values, or have invalid packet dimensions. WebUSB response writes
have a stall timeout so a disconnected browser cannot stop HID key scanning.

## Arduino IDE Setup

1. Install Arduino IDE 2.x.
2. Open **File > Preferences**.
3. Add this Boards Manager URL:

```text
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```

4. Install **Raspberry Pi Pico/RP2040/RP2350 by Earle F. Philhower, III**.
5. Open `xpad_neo/xpad_neo.ino`.
6. Select:

```text
Board: Raspberry Pi Pico
USB Stack: Adafruit TinyUSB
```

The current firmware was verified with arduino-pico `5.6.1`.

For Waveshare RP2040 Zero hardware, select that board instead while keeping the
Adafruit TinyUSB stack.

## Compile

Compile from Arduino IDE with **Sketch > Verify/Compile**, or use an
`arduino-cli` available on your `PATH`:

```powershell
arduino-cli compile --fqbn rp2040:rp2040:rpipico:usbstack=tinyusb --warnings all xpad_neo
```

Latest verified Raspberry Pi Pico result:

```text
Sketch uses 81276 bytes (3%) of program storage space.
Global variables use 17676 bytes (6%) of dynamic memory.
```

## Flash and Test

1. Compile and upload the sketch.
2. If automatic upload cannot find the board, hold **BOOTSEL** while connecting
   the Pico and upload to the `RPI-RP2` drive.
3. Confirm the device appears as `XPAD-NEO` and as a USB keyboard.
4. Test all eight MX switches with a keyboard event viewer or Small Deck.
5. Connect to `https://xtiaconfiger.com` using Chrome or Edge.
6. Read the current layout and test MX input state.
7. Write a clearly different preset and confirm HID output changes.
8. Unplug and reconnect the keyboard.
9. Read the preset again and confirm the saved layout and HID output remain.

`No drive to deploy` is an upload failure, not a compile failure. Enter BOOTSEL
mode and retry the upload when this appears.

## Development Rules

- Keep XPAD-NEO fixed at eight MX switch positions.
- Keep the active firmware to these two Arduino tabs unless requirements change.
- Keep `xpad_neo.ino` readable as the beginner path.
- Keep WebUSB limited to Layout Generator compatibility.
- Keep Flash storage small, validated, and versioned.
- Do not add LittleFS or restore removed XPAD modules without a new requirement.
- Prioritize compile, hardware behavior, and clarity over abstraction.

## License

MIT - free to use, modify, and distribute.

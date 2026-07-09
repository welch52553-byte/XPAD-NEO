# XPAD-NEO V2 Single-File Plan

This plan replaces the earlier modular slimdown plan.

The current product direction is a very small Arduino teaching sketch, not a
modular firmware project. Code size may be relaxed to around 500 lines, or
temporarily more if needed, but the project should be easy to compile and test.

## Current Understanding

XPAD-NEO V2 should be:

- one Arduino sketch file if possible
- only MX switch scanning and USB HID keyboard output
- simple enough for a beginner to read and change
- focused on compiling, flashing, and hardware testing

XPAD-NEO V2 should not keep:

- custom `.h` / `.cpp` module libraries
- WebUSB or Layout Generator runtime code
- LittleFS storage
- configurable keymap persistence
- layout matrix storage
- ADC / magnetic axis code
- encoder code
- microphone code
- vibration code
- macro playback code
- UI or visual changes

## Priorities

1. Compile successfully.
2. Flash successfully.
3. Support hardware key testing.
4. Keep the project to 1-2 source files.
5. Keep code clear and beginner-readable.
6. Reduce line count only after the firmware works.

## Target Structure

Preferred final firmware structure:

```text
xpad_neo/
  xpad_neo.ino
```

Documentation files may remain at the project root:

```text
README.md
AI_PROJECT_BRIEF.md
V2_PLAN.md
```

## Node 1: Collapse Firmware To One `.ino`

Status: completed.

Goal: make the active Arduino firmware a single file.

Work:

- Move the needed MX scan state and functions into `xpad_neo.ino`.
- Move the needed HID keyboard setup/report code into `xpad_neo.ino`.
- Put user-editable constants at the top:
  - USB VID/PID
  - MX GPIO pins
  - default HID keycodes
  - debounce time
- Remove LittleFS/config loading.
- Delete custom firmware module files:
  - `config.h`
  - `mx_keys.h`
  - `mx_keys.cpp`
  - `hid_keyboard.h`
  - `hid_keyboard.cpp`
  - `xpad_config.h`
  - `flash_storage.h`
  - `flash_storage.cpp`
- Compile with Arduino CLI/IDE.

Stop report:

- final firmware source files
- approximate line count
- compile result
- any uncertainty or behavior change

Result:

- Active firmware source is now only `xpad_neo.ino`.
- The file is about 163 lines.
- Arduino compile passed for Raspberry Pi Pico with Adafruit TinyUSB.
- GPIO pins are still the assumed GP0-GP7 mapping and must be hardware-tested.

## Node 2: Document The Single-File Firmware

Status: completed.

Goal: make the docs match the new shape.

Work:

- Update `README.md` to describe the one-file sketch.
- Explain how to change GPIO pins and default keycodes.
- Clearly state that Layout Generator/WebUSB is deferred.
- Clearly state that non-MX modules are not supported.
- Update `AI_PROJECT_BRIEF.md` for future AI work.

Stop report:

- docs changed
- removed claims
- remaining deferred items

Result:

- `README.md` now describes the single-file sketch and 1-8 test mapping.
- `AI_PROJECT_BRIEF.md` now tells future AI work to keep the active firmware in
  `xpad_neo/xpad_neo.ino`.
- `.gitignore` ignores Arduino build output and local agent metadata.

## Node 3: Hardware Test

Goal: test the firmware on the user's XPAD hardware.

Work:

- Flash from Arduino IDE.
- Open Notepad.
- Press every MX key.
- Confirm whether default keys output characters.

Stop report:

- which keys work
- whether GPIO mapping is likely wrong
- whether a temporary GPIO diagnostic sketch is needed

## Node 4: Optional GPIO Diagnostic

Goal: find real XPAD GPIO pins if default GP0-GP7 does not work.

Work:

- Create a temporary diagnostic sketch only if needed.
- Do not merge diagnostic complexity into the main project.
- Record detected GPIO numbers.
- Update the constants in `xpad_neo.ino`.

Stop report:

- detected GPIO map
- updated constants
- compile result
- hardware retest result

## Acceptance Criteria

V2 single-file firmware is acceptable when:

- active firmware is one `.ino` file
- it compiles for Raspberry Pi Pico / RP2040 with Adafruit TinyUSB
- it scans only MX GPIO switches
- it sends USB HID keyboard reports
- it has no custom module `.h/.cpp` files
- WebUSB/Layout Generator work is deferred, not half-kept
- non-MX modules are absent

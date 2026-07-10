# XPAD-NEO AI Project Brief

Read this file before changing XPAD-NEO.

## Current Direction

XPAD-NEO V2 is a single-file Arduino MX keyboard sketch with a minimal
WebUSB/Layout Generator compatibility layer.

It is not a full XPAD firmware clone, not a modular firmware framework, and not
a full XPAD firmware framework. The active goal is to keep one simple Arduino
sketch that compiles, flashes, scans MX switches, sends USB HID keyboard
reports, and supports only the Layout Generator commands currently needed for
basic read/write/test compatibility.

The firmware should be able to run without the original XPAD host tools. Its
default behavior is a hardcoded GPIO-to-HID layout in `xpad_neo.ino`. Later, if
a valid Flash layout is written by a supported tool, that Flash layout should
override the hardcoded defaults. If no valid Flash layout exists, the firmware
must fall back to the hardcoded defaults.

## Product Goal

Make XPAD-NEO suitable as a beginner embedded mini project:

- easy to open in Arduino IDE
- easy to read in one sitting
- easy to modify by changing constants near the top of the sketch
- easy to test with appropriate keyboard or application tools
- focused on real hardware behavior

Code size is allowed to be practical. Around 500 lines of code is acceptable if
it helps clarity and testing; comments can be more generous because this project
is meant to be readable by beginners and AI assistants.

## Active Source Shape

```text
xpad_neo/
  xpad_neo.ino
```

The active firmware should remain one `.ino` file unless the user explicitly
changes direction.

Root documentation:

```text
README.md
AI_PROJECT_BRIEF.md
```

Generated Arduino build output under `xpad_neo/build/` is not source and should
not be committed.

## Priorities

Work in this order:

1. Compile successfully.
2. Flash successfully.
3. Support hardware key testing.
4. Keep the active firmware to one `.ino` file.
5. Keep code clear and beginner-readable.
6. Reduce line count only after the firmware works.

Do not optimize for modularity, extensibility, or protocol compatibility during
the current V2 work.

## Supported Behavior

The firmware supports only:

- active-low MX switch GPIO scanning
- software debounce
- USB HID keyboard reports
- minimal WebUSB vendor interface for Layout Generator compatibility
- in-memory Layout Generator layout/keymap read and write
- Layout Generator input-state reads for MX hardware testing
- hardcoded default GPIO/keycode constants in `xpad_neo.ino`
- a future Flash layout override point, once the layout format is known

Current default fallback mapping:

```text
GP0 -> F13
GP1 -> F14
GP2 -> F15
GP3 -> F16
GP4 -> F17
GP5 -> F18
GP6 -> F19
GP7 -> F20
```

The default mapping should be kept near the top of `xpad_neo.ino` and documented
with clear English comments. This section is intentionally beginner-facing: a
new reader should be able to see which GPIO pin maps to which HID output before
reading the scan loop.

## Layout Selection Model

The intended layout priority is:

1. Use a valid Flash layout if one exists.
2. Otherwise use the hardcoded default layout in `xpad_neo.ino`.

Do not implement real Flash layout parsing until the XPAD source, Layout
Generator protocol, or stored layout format is available. Until then, keep only
a small, obvious placeholder function if needed so the main scan/HID logic does
not need to be rewritten later.

## Minimal Layout Generator Model

The firmware exposes WebUSB only for the active Layout Generator workflow at
`https://xtiaconfiger.com`.

Intended USB shape:

```text
Interface 0: HID Keyboard
Interface 1: WebUSB Vendor
Vendor OUT:  endpoint 2
Vendor IN:   endpoint 2
```

Implementation notes:

- Register HID before WebUSB so TinyUSB allocates interface 0 to HID and
  interface 1 to WebUSB.
- Enable the HID OUT endpoint so HID consumes endpoint 1 in both directions and
  WebUSB is more likely to receive endpoint 2 in both directions.
- Keep one `Preset` as the shared source of truth for Layout Generator and HID
  scanning.
- Support temporary in-memory layout/keymap writes so application-side presets
  can immediately change HID output.
- Do not save Layout Generator writes to Flash until the stored layout format is
  explicitly defined.
- Keep this compatibility layer narrow; it is not permission to restore the full
  XPAD runtime.

Currently handled commands:

```text
0x30 write keymap
0x31 write layout matrix
0x32 read layout matrix
0x33 read keymap
0x34 read MX input state
0x35 acknowledge macro write only
0x36 read empty macro slot
0x37 acknowledge encoder write only
0x38 read empty encoder config
0x39 acknowledge vibration test only
```

## Not Supported

Do not add these unless the user explicitly gives a new requirement:

- full WebUSB runtime beyond Layout Generator compatibility
- non-required Layout Generator protocol areas
- LittleFS config storage
- dynamic keymap persistence, except for a future explicitly defined Flash
  layout format
- persistent layout matrix storage, except for a future explicitly defined Flash
  format
- ADC or magnetic axis keys
- Rapid Trigger
- encoder support
- microphone support
- vibration support
- macro playback
- non-Layout-Generator XPAD tools
- UI or visual changes

Flash persistence and broader protocol compatibility are deferred until the
leader provides the main XPAD source or enough protocol detail to implement them
deliberately.

## Engineering Rules

1. Keep the firmware simple before making it clever.
2. Prefer constants at the top of `xpad_neo.ino` over configuration systems.
3. Prioritize compile success and hardware testing.
4. Avoid custom `.h/.cpp` modules in the active firmware.
5. Do not reintroduce deleted modules casually.
6. If a hardware test fails, first suspect GPIO mapping.
7. Use a temporary diagnostic sketch only when needed; do not merge diagnostic
   complexity into the main firmware.

## Hardware Test Flow

The hardware test flow should stay open and match the current task:

1. Flash `xpad_neo/xpad_neo.ino` from Arduino IDE.
2. Confirm the board enumerates as a USB keyboard.
3. Choose a test target:
   - keyboard event viewer or OS-level hotkey tool for raw HID checks
   - Small Deck with **Show all F13-F24** enabled for launcher checks
   - a temporary diagnostic sketch if GPIO mapping is unknown
4. Press every MX key once.
5. Confirm whether the expected HID output or app action occurs.

If no key types anything, do not add more features to the main firmware. Create
a temporary GPIO diagnostic sketch, find the real XPAD MX pins, then update the
`DEFAULT_MX_KEYS` table in `xpad_neo.ino`.

## Current Acceptance Criteria

The V2 firmware is acceptable when:

- active firmware is one `.ino` file
- it compiles for Raspberry Pi Pico / RP2040 with Adafruit TinyUSB
- it scans only MX GPIO switches
- it sends USB HID keyboard reports
- it has no custom module `.h/.cpp` files
- it exposes a minimal WebUSB vendor interface for Layout Generator
- it can read/write the active in-memory Layout Generator preset
- Flash preset persistence is deferred, not guessed
- non-MX modules are absent

## Suggested AI Prompt

```text
Read AI_PROJECT_BRIEF.md first. XPAD-NEO V2 is a single-file Arduino sketch
that scans MX switches, sends USB HID keyboard reports, and exposes a minimal
WebUSB/Layout Generator compatibility layer. Do not add LittleFS, broad XPAD
runtime code, ADC/magnetic axis support, encoder support, microphone support,
vibration, macro playback, or Flash persistence unless explicitly requested.
Keep the active firmware in xpad_neo/xpad_neo.ino and prioritize
compile/hardware testing.
```

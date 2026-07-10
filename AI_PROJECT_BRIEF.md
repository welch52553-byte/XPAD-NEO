# XPAD-NEO AI Project Brief

Read this file before changing XPAD-NEO.

## Product Direction

XPAD-NEO is a beginner-friendly Arduino firmware project for exactly eight MX
mechanical switches on RP2040.

It is not a simplified copy that must preserve the full XPAD architecture. It is
a small independent firmware with only:

- active-low MX GPIO scanning
- software debounce
- USB HID keyboard output
- minimal Layout Generator WebUSB compatibility
- validated Flash preset persistence

Magnetic switches, ADC/Rapid Trigger, encoders, microphones, vibration, macro
playback, LittleFS, non-Layout-Generator tools, and full XPAD protocol emulation
are outside the project scope.

## Source Architecture

```text
xpad_neo/
  xpad_neo.ino
  layout_generator.ino
```

Both files are Arduino tabs in one sketch and compile as one translation unit.
Do not add `.h/.cpp` modules, classes, or a framework unless a future requirement
creates a concrete need.

### xpad_neo.ino

This is the beginner-facing main path. It owns:

- USB identity and HID setup
- `MxKey` and `Preset` shared structures
- fixed eight-key default mapping
- `setup()` and `loop()`
- active preset application
- GPIO scanning and debounce
- keyboard report generation

A student should be able to understand this flow without reading the second tab:

```text
MX switch -> GPIO -> debounce -> HID report -> computer
```

### layout_generator.ino

This is the advanced compatibility layer. It owns:

- WebUSB interface setup
- Layout Generator packet parsing and responses
- layout/keymap validation
- unsupported capability stubs required by the web page
- EEPROM/Flash save and load
- storage magic, version, size, and checksum validation

Keep this file procedural and explicit. Do not turn it into a reusable protocol
framework.

## Hardware and Default Mapping

XPAD-NEO always has eight MX switch positions. Presets may disable positions but
must never define more than eight keys.

Current fallback mapping:

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

The fallback table must stay near the top of `xpad_neo.ino`. Source comments are
English and should explain active-low wiring and HID keycodes clearly.

## Preset Selection

Startup follows one path:

1. Initialize EEPROM Flash emulation.
2. Read the stored XPAD-NEO record.
3. Validate magic, version, structure size, checksum, layout, key count, and GPIO
   values.
4. Use the stored preset when valid.
5. Otherwise copy the hardcoded default into `activePreset`.
6. Configure GPIO and start scanning from `activePreset.keymap`.

`activePreset` is the runtime source used by both HID and Layout Generator.

Layout and keymap arrive in separate protocol commands. Each candidate preset is
fully validated and committed to Flash before it replaces `activePreset`.

The current storage format is XPAD-NEO-specific and versioned. It does not need
to reproduce an unknown legacy XPAD LittleFS format.

## WebUSB Boundary

Required USB shape:

```text
Interface 0: HID Keyboard
Interface 1: WebUSB Vendor
Vendor OUT:  endpoint 2
Vendor IN:   endpoint 2
```

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

Commands `0x35-0x39` are compatibility responses, not implementations of macro,
encoder, or vibration features.

Protocol safety rules:

- reject more than eight key mappings
- reject invalid or duplicate GPIO values
- reject invalid layout dimensions and packet lengths
- do not partially apply invalid candidates
- return a nonzero status for rejected writes
- never let a stalled WebUSB endpoint block HID forever
- force an empty HID report after applying a new keymap so old keys cannot stick

## Engineering Priorities

Work in this order:

1. Compile with Raspberry Pi Pico and Adafruit TinyUSB.
2. Flash successfully.
3. Verify all eight MX inputs and HID output.
4. Verify Layout Generator connection/read/write/test.
5. Verify unplug/replug Flash persistence.
6. Preserve the two-tab beginner/advanced boundary.
7. Reduce code only when readability and behavior remain clear.

Avoid abstractions whose only benefit is fewer lines. Straightforward loops,
small structs, named command constants, and explicit validation are preferred.

## Verified Environment

```text
Board: Raspberry Pi Pico
Arduino core: rp2040 5.6.1
USB Stack: Adafruit TinyUSB
```

Latest verified compile:

```text
Sketch uses 81276 bytes (3%) of program storage space.
Global variables use 17676 bytes (6%) of dynamic memory.
```

Hardware verification has passed for:

- default HID output
- Layout Generator pairing and connection
- layout/keymap read and write
- MX input test mode
- active keymap behavior
- saved preset recovery after USB unplug/replug
- two-tab firmware after the structural split

## Acceptance Criteria

The current firmware is acceptable when:

- only eight MX switch positions are supported
- the main learning path remains readable in `xpad_neo.ino`
- Layout Generator and persistence stay in `layout_generator.ino`
- both tabs compile as one Arduino sketch
- invalid Flash data falls back to the default preset
- invalid WebUSB data cannot corrupt the active preset
- WebUSB failure cannot permanently stop HID scanning
- no removed XPAD hardware modules return

## Suggested AI Prompt

```text
Read AI_PROJECT_BRIEF.md first. XPAD-NEO is a two-tab Arduino sketch for exactly
eight MX switches. Keep the beginner MX scan/HID flow in xpad_neo.ino and the
minimal Layout Generator plus validated Flash persistence in
layout_generator.ino. Do not add LittleFS, magnetic/ADC keys, Rapid Trigger,
encoders, microphones, vibration, macro playback, non-Layout-Generator tools,
or a framework. Compile and preserve hardware-tested behavior.
```

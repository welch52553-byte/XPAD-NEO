# XPAD-NEO AI Project Brief

Read this file before changing XPAD-NEO.

## Current Direction

XPAD-NEO V2 is a single-file Arduino MX keyboard sketch.

It is not a full XPAD firmware clone, not a modular firmware framework, and not
a WebUSB/Layout Generator implementation. The active goal is to keep one simple
Arduino sketch that compiles, flashes, scans MX switches, and sends USB HID
keyboard reports.

## Product Goal

Make XPAD-NEO suitable as a beginner embedded mini project:

- easy to open in Arduino IDE
- easy to read in one sitting
- easy to modify by changing constants near the top of the sketch
- easy to test in Notepad
- focused on real hardware behavior

Code size is allowed to be practical. Around 500 lines is acceptable if it helps
clarity and testing. The current sketch is much smaller.

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
V2_PLAN.md
```

Generated Arduino build output under `xpad_neo/build/` is not source and should
not be committed.

## Supported Behavior

The firmware supports only:

- active-low MX switch GPIO scanning
- software debounce
- USB HID keyboard reports
- fixed GPIO/keycode constants in `xpad_neo.ino`

Current default test mapping:

```text
GP0 -> 1
GP1 -> 2
GP2 -> 3
GP3 -> 4
GP4 -> 5
GP5 -> 6
GP6 -> 7
GP7 -> 8
```

## Not Supported

Do not add these unless the user explicitly gives a new requirement:

- WebUSB runtime
- Layout Generator compatibility runtime
- LittleFS config storage
- dynamic keymap persistence
- layout matrix storage
- ADC or magnetic axis keys
- Rapid Trigger
- encoder support
- microphone support
- vibration support
- macro playback
- non-Layout-Generator XPAD tools
- UI or visual changes

Layout Generator compatibility is deferred until the leader provides the main
XPAD source or enough protocol detail to implement it deliberately.

## Engineering Rules

1. Keep the firmware simple before making it clever.
2. Prefer constants at the top of `xpad_neo.ino` over configuration systems.
3. Prioritize compile success and hardware testing.
4. Avoid custom `.h/.cpp` modules in the active firmware.
5. Do not reintroduce deleted modules casually.
6. If a hardware test fails, first suspect GPIO mapping.
7. Use a temporary diagnostic sketch only when needed; do not merge diagnostic
   complexity into the main firmware.

## Suggested AI Prompt

```text
Read AI_PROJECT_BRIEF.md first. XPAD-NEO V2 is a single-file Arduino sketch
that only scans MX switches and sends USB HID keyboard reports. Do not add
WebUSB, LittleFS, Layout Generator runtime code, ADC/magnetic axis support,
encoder support, microphone support, vibration, or macros. Keep the active
firmware in xpad_neo/xpad_neo.ino and prioritize compile/hardware testing.
```

// =============================================================================
// XPAD-NEO — Open-source Arduino firmware for the XTIA XPAD keyboard
//
// Platform : Waveshare RP2040 Zero (or any RP2040 board)
// Arduino  : arduino-pico core (Earle Philhower)
//            https://github.com/earlephilhower/arduino-pico
// USB lib  : Adafruit TinyUSB (bundled with arduino-pico)
//
// Features implemented:
//   ✓ MX mechanical switch scanning (GPIO, active-low, debounced)
//   ✓ ADC hall-effect key scanning (EMA filter, fixed threshold + Rapid Trigger)
//   ✓ USB HID keyboard output (6-key rollover + modifier keys)
//   ✓ WebUSB config protocol (compatible with XTIA web UI)
//   ✓ LittleFS persistent config (ADC calibration + key mappings + layout)
//
// Optional modules (uncomment in config.h to enable):
//   ○ FEATURE_ENCODER  — Rotary encoder support
//   ○ FEATURE_MACRO    — Text macro playback
//   ○ FEATURE_MIC      — I2S microphone
//   ○ FEATURE_RUMBLE   — Vibration motor
//
// Quick start:
//   1. Install arduino-pico board package in Arduino IDE.
//   2. Select board: "Waveshare RP2040 Zero" (or your board).
//   3. Select USB Stack: "Adafruit TinyUSB" in Tools menu.
//   4. USB VID/PID (0x1209/0x0002) are applied at runtime via setID() —
//      no boards.txt edits required (see README).
//   5. Upload. Open XTIA web UI to calibrate and configure.
// =============================================================================

// --- Core Arduino library ---
#include <Arduino.h>

// --- TinyUSB must be included before any other USB headers ---
#include <Adafruit_TinyUSB.h>

// --- XPAD-NEO modules ---
#include "config.h"
#include "xpad_config.h"
#include "flash_storage.h"
#include "adc_keys.h"
#include "mx_keys.h"
#include "hid_keyboard.h"
#include "webusb_handler.h"

// Optional feature modules — guarded by feature flags from config.h.
// Uncomment the matching #define in config.h to activate.
#ifdef FEATURE_ENCODER
  #include "encoder.h"
#endif
#ifdef FEATURE_MACRO
  #include "macro_player.h"
#endif
#ifdef FEATURE_MIC
  #include "i2s_mic.h"
#endif
#ifdef FEATURE_RUMBLE
  #include "rumble.h"
#endif

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------
static uint32_t s_last_adc_scan_ms = 0;

// ---------------------------------------------------------------------------
void setup() {
    // Step 1: Load config from LittleFS (or defaults if first boot).
    // This must happen before any module that reads the config.
    config_load();

    // Step 2: Set up USB stack.
    // TinyUSB requires USB to be fully configured before the main loop runs.
    // The Vendor interface (WebUSB) must be registered before USBDevice.begin().
    webusb_handler_setup();
    hid_keyboard_setup();

    // USB identity. The VID must be 0x1209 (pid.codes) — the XTIA web
    // configurator's WebUSB device picker filters on it. Setting it here
    // overrides whatever boards.txt provides, so no core edits are needed.
    TinyUSBDevice.setID(XPAD_USB_VID, XPAD_USB_PID);
    TinyUSBDevice.setManufacturerDescriptor("XTIA");
    TinyUSBDevice.setProductDescriptor("XPAD-NEO");

    // Wait for USB enumeration (important on RP2040 which has no native USB init delay).
    // On battery-powered builds you can remove this wait.
    while (!TinyUSBDevice.mounted()) delay(1);

    // Step 3: Auto-calibrate ADC rest positions (sample while no keys are pressed).
    // This must happen before mx_keys_setup() and the main loop.
    adc_keys_setup();
    adc_keys_calibrate_rest();

    // Step 4: Configure MX switch GPIO pins.
    mx_keys_setup();

    // Step 5: Initialise optional modules.
#ifdef FEATURE_ENCODER
    encoder_setup();
#endif
#ifdef FEATURE_MACRO
    macro_player_setup();
#endif
#ifdef FEATURE_MIC
    i2s_mic_setup();
#endif
#ifdef FEATURE_RUMBLE
    rumble_setup();
#endif
}

// ---------------------------------------------------------------------------
void loop() {
    // --- USB tasks ---
    // tud_task() drives the TinyUSB state machine. Must be called frequently
    // (at least every 1 ms) to keep USB transfers flowing.
    // In arduino-pico this is usually called automatically in the background,
    // but calling it here too makes timing more predictable.
    // tud_task();   // Uncomment if you see USB stalls on your board.

    // --- WebUSB config protocol ---
    // Check for incoming packets from the web UI and respond.
    webusb_handler_task();

    // --- ADC scanning ---
    // Hall-effect keys are scanned on a fixed interval to avoid wasting CPU.
    uint32_t now = millis();
    if (now - s_last_adc_scan_ms >= ADC_SCAN_INTERVAL_MS) {
        s_last_adc_scan_ms = now;
        adc_keys_scan();
    }

    // --- MX switch scanning ---
    // Mechanical switches are scanned every loop iteration; debounce logic
    // inside mx_keys_scan() handles the timing.
    mx_keys_scan();

    // --- USB HID output ---
    // Build and send a keyboard report from the current pressed key state.
    hid_keyboard_task();

    // --- Optional modules ---
#ifdef FEATURE_ENCODER
    encoder_task();
#endif
#ifdef FEATURE_MACRO
    macro_player_task();
#endif
#ifdef FEATURE_MIC
    i2s_mic_task();
#endif
#ifdef FEATURE_RUMBLE
    rumble_task();
#endif
}

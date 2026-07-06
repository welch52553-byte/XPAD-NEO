#pragma once

// =============================================================================
// config.h — Hardware pin assignments and compile-time feature flags
//
// This is the first file to edit when porting XPAD-NEO to a new board or
// adding new hardware features. All pin numbers refer to RP2040 GPIO numbers
// (not physical pin positions on the PCB).
// =============================================================================

// -----------------------------------------------------------------------------
// USB identifiers
// The web configurator (index.html / adc_calibration.html) filters USB devices
// by VID == 0xCafe. Keep this value to stay compatible with the XTIA web UI.
// -----------------------------------------------------------------------------
#define XPAD_USB_VID  0xCafe
#define XPAD_USB_PID  0x4002   // 0x4001 = XPAD firmware; 0x4002 = XPAD-NEO

// -----------------------------------------------------------------------------
// ADC pins  (Hall-effect / analog keys)
// RP2040 ADC channels:  GP26 = ADC0, GP27 = ADC1, GP28 = ADC2, GP29 = ADC3
// These pins must not be changed — they are the only analog-capable GPIO on
// the RP2040. The channel index (0-3) maps directly to the pin number (26-29).
// -----------------------------------------------------------------------------
#define ADC_PIN_BASE  26          // First ADC GPIO (GP26 = channel 0)
#define ADC_CHANNEL_COUNT  4      // RP2040 has 4 ADC channels total

// -----------------------------------------------------------------------------
// MX switch GPIO pins
// Adjust this list to match your physical wiring. Each entry is one GPIO pin
// connected to one MX key (active-low, internal pull-up enabled).
// The mapping from pin → HID keycode is stored in flash config, not here.
// -----------------------------------------------------------------------------
#define MX_GPIO_PINS  { 0, 1, 2, 3, 4, 5, 6, 7 }   // default 8-key layout
#define MX_KEY_COUNT  8

// Debounce time in milliseconds. Increase if you see spurious key events.
#define MX_DEBOUNCE_MS  5

// -----------------------------------------------------------------------------
// Feature flags
// Uncomment a line to enable an optional module. Each module lives in its own
// .h/.cpp file so unused modules cost zero flash/RAM when disabled.
// -----------------------------------------------------------------------------

// #define FEATURE_ENCODER        // Rotary encoder (EC06) on GP20/GP21/GP22
// #define FEATURE_MACRO          // Text macro playback via USB HID
// #define FEATURE_MIC            // I2S microphone input (GP6/GP7/GP8)
// #define FEATURE_RUMBLE         // Vibration motor PWM output

// -----------------------------------------------------------------------------
// ADC scan rate
// The main loop polls ADC this often. Lower = more responsive; higher = less
// CPU time spent on ADC. 1 ms is a good default for hall-effect switches.
// -----------------------------------------------------------------------------
#define ADC_SCAN_INTERVAL_MS  1

// -----------------------------------------------------------------------------
// Flash / LittleFS
// Config is stored as a single binary file at this path in LittleFS.
// -----------------------------------------------------------------------------
#define CONFIG_FILE_PATH  "/xpad_config.bin"

// Magic word written at the start of the config struct. If the stored magic
// does not match this value, factory defaults are loaded instead.
// Value must match XPAD firmware's CONFIG_MAGIC so both can read the same file
// if you ever copy flash images between devices.
#define CONFIG_MAGIC  0x58544948UL   // ASCII "XTIH"

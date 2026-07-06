#pragma once
#include <stdint.h>

// =============================================================================
// mx_keys.h — MX mechanical switch scanning
//
// MX switches are simple normally-open contacts. We use the RP2040's internal
// pull-up resistors so the pin reads HIGH at rest and LOW when pressed.
//
// Debouncing: raw GPIO transitions are filtered by a small time window
// (MX_DEBOUNCE_MS). A state change is only accepted after the pin has been
// stable for the full debounce period. This prevents false key events from
// electrical bounce when the contacts first touch.
// =============================================================================

// Configure GPIO pins defined in config.h as inputs with pull-ups.
// Must be called after config_load() in setup().
void mx_keys_setup();

// Re-run pin setup and reset debounce state after keys[] changed at runtime
// (e.g. the web configurator wrote a new keymap). Mirrors the XPAD firmware's
// gpio_keys_reinit().
void mx_keys_reinit();

// Sample all MX GPIO pins and update g_mx_pressed_mask.
// Call this every loop iteration — debounce timing is handled internally.
void mx_keys_scan();

// Bitmask of currently debounced-pressed MX keys, indexed by position in
// config.keys[]. Bit N is set when the MX key at keys[N] is pressed.
extern volatile uint32_t g_mx_pressed_mask;

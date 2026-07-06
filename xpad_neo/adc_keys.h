#pragma once
#include <stdint.h>

// =============================================================================
// adc_keys.h — Hall-effect (analog) key scanning
//
// Hall-effect switches produce a continuous analog voltage that corresponds to
// magnet position. We read the voltage with the RP2040's 12-bit ADC (0-4095)
// and compare it to calibrated thresholds to decide if a key is "pressed".
//
// Two trigger modes (selected per-device in config):
//   Fixed threshold — key fires when ADC crosses adc_trigger[ch] toward pressed
//   Rapid Trigger   — key fires/releases on any direction reversal ≥ rt_sensitivity
// =============================================================================

// Initialise ADC hardware and load calibration parameters from config.
// Must be called after config_load() in setup().
void adc_keys_setup();

// Read all ADC channels and update the pressed/released state of each ADC key.
// Call this every ADC_SCAN_INTERVAL_MS milliseconds from the main loop.
void adc_keys_scan();

// Reload calibration parameters from the in-RAM config.
// Call this after the web UI writes new calibration data (CMD_WRITE_PARAMS).
void adc_keys_update_params();

// Auto-calibrate rest positions: sample each ADC channel N times at boot
// (while no keys are pressed) and store the average as adc_rest[].
// Result is written to RAM only — not saved to flash.
void adc_keys_calibrate_rest();

// Raw ADC reading for each channel (0-4095), updated every scan cycle.
// Read by the WebUSB handler to stream live values to the web UI.
extern volatile uint16_t g_adc_raw[4];

// Percentage of travel for each channel (0-100), computed from calibration.
// 0 = fully released, 100 = fully pressed.
extern volatile uint8_t  g_adc_pct[4];

// Bitmask of currently pressed ADC keys, indexed by key position in config.keys[].
// Bit N is set when the ADC key at keys[N] is considered pressed.
// The HID layer reads this to build keyboard reports.
extern volatile uint32_t g_adc_pressed_mask;

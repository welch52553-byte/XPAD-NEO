#pragma once
#include <stdint.h>
#include "config.h"

// =============================================================================
// xpad_config.h — Shared configuration structure
//
// This struct is binary-compatible with the XPAD firmware's XpadConfig in
// src/storage/flash_config.h. Both devices speak the same USB protocol and
// store the same fields, so the XTIA web UI works with both without changes.
//
// Rule: never reorder or remove fields. If you add new fields, append them at
// the end and bump CONFIG_MAGIC so old configs are safely ignored.
// =============================================================================

#define MAX_KEYS          64
#define LAYOUT_MAX_ROWS    8   // must match XPAD firmware / web configurator
#define LAYOUT_MAX_COLS   10

// Key type stored per-key in the keys[] array.
#define KEY_TYPE_NONE   0
#define KEY_TYPE_MX     1   // Digital MX switch — reads GPIO, reports HID keycode
#define KEY_TYPE_ADC    2   // Analog hall-effect — reads ADC, fires at threshold

// Encoder sub-action types (used in enc_ccw/cw/sw_type fields).
// Reserved for FEATURE_ENCODER — unused when encoder is disabled.
#define ENC_TYPE_NONE      0
#define ENC_TYPE_KEYBOARD  1   // Fire a standard HID keyboard keycode
#define ENC_TYPE_CONSUMER  2   // Fire a HID Consumer Control code (media key)

// Maximum number of keys that can carry a text macro.
// Reserved for FEATURE_MACRO.
#define MACRO_KEYS_MAX   6
#define MACRO_TEXT_LEN  60   // characters per macro (fits in one 64-byte USB packet)

// Per-key mapping: one entry per physical switch/sensor.
typedef struct {
    uint8_t  gpio_pin;    // RP2040 GPIO number (0-29)
    uint8_t  hid_keycode; // HID Usage ID (e.g. 0x04 = 'A')
    uint8_t  modifiers;   // HID modifier byte: bit0=Ctrl bit1=Shift bit2=Alt bit3=Win
    uint8_t  type;        // KEY_TYPE_* above
} KeyMapping;

// Full device configuration — everything that is saved to flash.
//
// ADC calibration model (mirrors GP2040-CE hall-effect trigger):
//   adc_rest[i]    — idle position, auto-captured at boot
//   adc_active[i]  — full-press position, captured via web UI calibration
//   adc_trigger[i] — fixed threshold OR rapid-trigger actuation point
//   rt_sensitivity — ADC delta from last reversal required to fire/release
typedef struct {
    // ── Identity ────────────────────────────────────────────────────────────
    uint32_t   magic;                     // Must equal CONFIG_MAGIC

    // ── Key mappings ─────────────────────────────────────────────────────────
    KeyMapping keys[MAX_KEYS];            // All MX + ADC keys

    // ── ADC calibration ──────────────────────────────────────────────────────
    uint16_t   adc_rest[4];               // Idle ADC value, auto-calibrated at boot
    uint16_t   adc_active[4];             // Full-press ADC value
    uint16_t   adc_trigger[4];            // Trigger threshold (raw ADC units)
    uint8_t    adc_smoothing;             // EMA alpha %: 1 = max smooth, 99 = raw
    uint8_t    adc_polarity;              // Bitmask: bit N set → axis N decreases when pressed

    // ── Visual layout (set by web configurator) ───────────────────────────────
    uint8_t    layout_rows;               // 0 = no layout saved yet
    uint8_t    layout_cols;
    uint8_t    layout_matrix[LAYOUT_MAX_ROWS][LAYOUT_MAX_COLS]; // gpio_pin per cell; 0xFF = empty

    // ── Text macros (FEATURE_MACRO) ──────────────────────────────────────────
    // These fields are reserved even when FEATURE_MACRO is disabled so the
    // struct layout stays identical to the XPAD firmware.
    uint8_t    macro_gpio[MACRO_KEYS_MAX];             // GPIO for each macro slot; 0xFF = empty
    char       macro_text[MACRO_KEYS_MAX][MACRO_TEXT_LEN]; // Null-terminated strings

    // ── Rapid Trigger ────────────────────────────────────────────────────────
    uint8_t    rt_enabled;                // 0 = fixed threshold mode, 1 = rapid trigger
    uint16_t   rt_sensitivity;            // ADC delta required to fire after reversal

    // ── Rotary encoder (FEATURE_ENCODER) ─────────────────────────────────────
    // Reserved for future use. Kept here so the struct matches XPAD firmware.
    uint8_t    enc_ccw_type;
    uint8_t    enc_ccw_code;
    uint8_t    enc_ccw_mod;
    uint8_t    enc_cw_type;
    uint8_t    enc_cw_code;
    uint8_t    enc_cw_mod;
    uint8_t    enc_sw_type;
    uint8_t    enc_sw_code;
    uint8_t    enc_sw_mod;

} XpadConfig;

// Returns a pointer to the live in-RAM config. Never cache the pointer across
// a flash_config_save() call — the struct may be re-read from disk.
XpadConfig* config_get();

// Load config from LittleFS. Call once in setup(). Falls back to defaults if
// no valid config is found.
void config_load();

// Persist the current in-RAM config to LittleFS.
void config_save();

// Overwrite adc_rest[] in RAM without saving to flash. Called at boot after
// the auto-calibration pass captures the resting ADC values.
void config_update_rest_ram(const uint16_t rest[4]);

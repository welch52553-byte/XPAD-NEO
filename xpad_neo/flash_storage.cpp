#include "flash_storage.h"
#include "xpad_config.h"
#include "config.h"
#include <LittleFS.h>
#include <string.h>

// =============================================================================
// flash_storage.cpp — LittleFS-backed config persistence
// =============================================================================

// The single in-RAM copy of the config. All subsystems read from this struct.
static XpadConfig s_config;

// Factory defaults — loaded when no valid config exists on disk.
// MX keys use GPIO 0-5; ADC keys use GPIO 26-29 (the four ADC-capable pins).
// HID keycodes: 0x04='A', 0x1E='1', etc. See USB HID Usage Tables §10.
static const XpadConfig k_default_config = {
    .magic = CONFIG_MAGIC,

    .keys = {
        // --- MX switches (digital, active-low) ---
        { .gpio_pin = 0,  .hid_keycode = 0x04, .modifiers = 0, .type = KEY_TYPE_MX }, // A
        { .gpio_pin = 1,  .hid_keycode = 0x05, .modifiers = 0, .type = KEY_TYPE_MX }, // B
        { .gpio_pin = 2,  .hid_keycode = 0x06, .modifiers = 0, .type = KEY_TYPE_MX }, // C
        { .gpio_pin = 3,  .hid_keycode = 0x07, .modifiers = 0, .type = KEY_TYPE_MX }, // D
        { .gpio_pin = 4,  .hid_keycode = 0x08, .modifiers = 0, .type = KEY_TYPE_MX }, // E
        { .gpio_pin = 5,  .hid_keycode = 0x09, .modifiers = 0, .type = KEY_TYPE_MX }, // F
        { .gpio_pin = 6,  .hid_keycode = 0x0A, .modifiers = 0, .type = KEY_TYPE_MX }, // G
        { .gpio_pin = 7,  .hid_keycode = 0x0B, .modifiers = 0, .type = KEY_TYPE_MX }, // H
        // --- ADC hall-effect keys ---
        { .gpio_pin = 26, .hid_keycode = 0x1E, .modifiers = 0, .type = KEY_TYPE_ADC }, // 1
        { .gpio_pin = 27, .hid_keycode = 0x1F, .modifiers = 0, .type = KEY_TYPE_ADC }, // 2
        { .gpio_pin = 28, .hid_keycode = 0x20, .modifiers = 0, .type = KEY_TYPE_ADC }, // 3
        { .gpio_pin = 29, .hid_keycode = 0x21, .modifiers = 0, .type = KEY_TYPE_ADC }, // 4
        // Remaining entries are zero-initialised (KEY_TYPE_NONE = 0).
    },

    // ADC calibration — overwritten by web UI after the user runs calibration.
    .adc_rest    = { 2500, 2500, 2500, 2500 }, // typical idle value; updated at boot
    .adc_active  = { 4050, 4050, 4050, 4050 }, // typical full-press value
    .adc_trigger = { 3000, 3000, 3000, 3000 }, // midpoint trigger

    .adc_smoothing = 10,   // EMA alpha = 10 % → moderate smoothing
    .adc_polarity  = 0,    // all axes increase when pressed (adjust if inverted)

    .layout_rows = 0,      // 0 = no visual layout saved yet
    .layout_cols = 0,
    .layout_matrix = {},   // zero = 0xFF would mean empty; web UI fills this

    // Macro slots: 0xFF gpio means unused.
    .macro_gpio = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    .macro_text = { "", "", "", "", "", "" },

    .rt_enabled    = 0,    // start in fixed-threshold mode
    .rt_sensitivity = 200, // 200 ADC units delta to fire in rapid-trigger mode

    // Encoder: all zeros = disabled (ENC_TYPE_NONE = 0)
    .enc_ccw_type = 0, .enc_ccw_code = 0, .enc_ccw_mod = 0,
    .enc_cw_type  = 0, .enc_cw_code  = 0, .enc_cw_mod  = 0,
    .enc_sw_type  = 0, .enc_sw_code  = 0, .enc_sw_mod  = 0,
};

// ---------------------------------------------------------------------------
XpadConfig* config_get() {
    return &s_config;
}

// ---------------------------------------------------------------------------
void config_update_rest_ram(const uint16_t rest[4]) {
    for (int i = 0; i < 4; i++) s_config.adc_rest[i] = rest[i];
    // Note: intentionally NOT calling config_save() here.
    // Rest values are captured fresh at every boot; persisting them would mean
    // they get stale if you move the device to a different temperature.
}

// ---------------------------------------------------------------------------
void config_load() {
    // Mount the filesystem. Format it if it doesn't exist yet (first boot).
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }

    File f = LittleFS.open(CONFIG_FILE_PATH, "r");
    if (!f) {
        // No config file found — use factory defaults.
        s_config = k_default_config;
        return;
    }

    // Read exactly sizeof(XpadConfig) bytes.
    size_t n = f.read((uint8_t*)&s_config, sizeof(s_config));
    f.close();

    // Validate magic. If it doesn't match (e.g. after a firmware update that
    // changed the struct layout), reset to defaults so we don't run garbage.
    if (n != sizeof(s_config) || s_config.magic != CONFIG_MAGIC) {
        s_config = k_default_config;
    }
}

// ---------------------------------------------------------------------------
void config_save() {
    File f = LittleFS.open(CONFIG_FILE_PATH, "w");
    if (!f) return; // Filesystem error — skip silently
    f.write((const uint8_t*)&s_config, sizeof(s_config));
    f.close();
}

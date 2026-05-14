#include "adc_keys.h"
#include "xpad_config.h"
#include "config.h"
#include <Arduino.h>

// =============================================================================
// adc_keys.cpp — Hall-effect key scanning implementation
//
// Learning notes:
//   EMA (Exponential Moving Average) — a simple digital low-pass filter.
//     new_value = alpha * raw + (1 - alpha) * old_value
//   With alpha = smoothing/100 (e.g. 10 % → heavy smoothing):
//     • High alpha (close to 1) = fast response, more noise
//     • Low alpha (close to 0) = slow response, very smooth
//
//   Rapid Trigger — instead of a fixed threshold, the key fires whenever the
//   ADC value reverses direction by at least rt_sensitivity counts. This lets
//   the key re-trigger without fully releasing to the resting position.
// =============================================================================

volatile uint16_t g_adc_raw[4]          = {};
volatile uint8_t  g_adc_pct[4]          = {};
volatile uint32_t g_adc_pressed_mask    = 0;

// Smoothed ADC values maintained across scan calls (EMA state).
static float s_smoothed[4] = { 2500, 2500, 2500, 2500 };

// Runtime calibration snapshot (copied from config at startup / param update).
static uint16_t s_rest[4];
static uint16_t s_active[4];
static uint16_t s_trigger[4];
static uint8_t  s_smoothing;
static uint8_t  s_polarity;
static uint8_t  s_rt_enabled;
static uint16_t s_rt_sensitivity;

// Rapid-trigger state per channel.
static bool     s_rt_pressed[4]         = {};
static float    s_rt_last_reversal[4]   = {};  // ADC value at last direction change
static bool     s_rt_going_up[4]        = {};  // direction flag

// ---------------------------------------------------------------------------
// Convert raw ADC value to 0-100 % travel using calibrated endpoints.
static uint8_t to_percent(int channel, uint16_t raw) {
    uint16_t r = s_rest[channel];
    uint16_t a = s_active[channel];
    if (a == r) return 0;

    // Handle both polarities: some magnets read higher when pressed, some lower.
    bool inverted = (s_polarity >> channel) & 1;
    int32_t span  = inverted ? (int32_t)r - a : (int32_t)a - r;
    int32_t delta = inverted ? (int32_t)r - raw : (int32_t)raw - r;

    int32_t pct = (delta * 100) / span;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

// ---------------------------------------------------------------------------
void adc_keys_update_params() {
    const XpadConfig* cfg = config_get();
    for (int i = 0; i < 4; i++) {
        s_rest[i]    = cfg->adc_rest[i];
        s_active[i]  = cfg->adc_active[i];
        s_trigger[i] = cfg->adc_trigger[i];
    }
    s_smoothing    = cfg->adc_smoothing;
    s_polarity     = cfg->adc_polarity;
    s_rt_enabled   = cfg->rt_enabled;
    s_rt_sensitivity = cfg->rt_sensitivity;
}

// ---------------------------------------------------------------------------
void adc_keys_setup() {
    // analogReadResolution(12) sets the ADC to 12-bit mode (0-4095).
    // The default on Arduino RP2040 is 10-bit (0-1023); XPAD uses 12-bit.
    analogReadResolution(12);

    // Warm up the ADC by discarding the first few readings.
    for (int ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
        analogRead(ADC_PIN_BASE + ch);
    }

    adc_keys_update_params();
}

// ---------------------------------------------------------------------------
void adc_keys_calibrate_rest() {
    const int SAMPLES = 64;
    uint32_t sum[4] = {};

    for (int s = 0; s < SAMPLES; s++) {
        for (int ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
            sum[ch] += analogRead(ADC_PIN_BASE + ch);
        }
        delay(1);
    }

    uint16_t rest[4];
    for (int ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
        rest[ch] = (uint16_t)(sum[ch] / SAMPLES);
        // Seed the EMA with the measured rest value to avoid startup transient.
        s_smoothed[ch] = rest[ch];
    }

    // Store in RAM (not flash — we recalibrate at every boot).
    config_update_rest_ram(rest);
    adc_keys_update_params(); // Refresh s_rest[] from updated config
}

// ---------------------------------------------------------------------------
void adc_keys_scan() {
    const XpadConfig* cfg = config_get();
    const float alpha = s_smoothing / 100.0f;

    for (int ch = 0; ch < ADC_CHANNEL_COUNT; ch++) {
        // Read raw ADC (12-bit, 0-4095).
        uint16_t raw = (uint16_t)analogRead(ADC_PIN_BASE + ch);
        g_adc_raw[ch] = raw;

        // Apply EMA smoothing to reduce sensor noise.
        s_smoothed[ch] = alpha * raw + (1.0f - alpha) * s_smoothed[ch];
        uint16_t smooth = (uint16_t)s_smoothed[ch];

        // Update travel percentage for the web UI.
        g_adc_pct[ch] = to_percent(ch, smooth);
    }

    // Determine pressed state for each ADC key and update the pressed mask.
    uint32_t new_mask = g_adc_pressed_mask;

    for (int i = 0; i < MAX_KEYS; i++) {
        const KeyMapping* k = &cfg->keys[i];
        if (k->type != KEY_TYPE_ADC) continue;

        // Derive the ADC channel from the GPIO pin number (GP26=ch0 ... GP29=ch3).
        int ch = k->gpio_pin - ADC_PIN_BASE;
        if (ch < 0 || ch >= ADC_CHANNEL_COUNT) continue;

        uint16_t smooth = (uint16_t)s_smoothed[ch];
        bool pressed;

        if (!s_rt_enabled) {
            // ── Fixed threshold mode ──────────────────────────────────────
            // Key is pressed when the smoothed ADC value has crossed the
            // trigger threshold in the "toward pressed" direction.
            bool inverted = (s_polarity >> ch) & 1;
            if (inverted) {
                pressed = smooth < s_trigger[ch];
            } else {
                pressed = smooth > s_trigger[ch];
            }

        } else {
            // ── Rapid Trigger mode ────────────────────────────────────────
            // Fire/release on any reversal that exceeds rt_sensitivity counts.
            // This allows very fast re-actuation without releasing fully.
            float cur  = s_smoothed[ch];
            float prev = s_rt_last_reversal[ch];
            bool going_up = cur > prev;

            if (going_up != s_rt_going_up[ch]) {
                // Direction changed — record the reversal point.
                s_rt_going_up[ch]        = going_up;
                s_rt_last_reversal[ch]   = cur;
            }

            float travel = fabsf(cur - s_rt_last_reversal[ch]);
            bool inverted = (s_polarity >> ch) & 1;

            if (!s_rt_pressed[ch]) {
                // Not pressed: fire if we've moved enough toward "active".
                bool toward_active = inverted ? !going_up : going_up;
                if (toward_active && travel >= s_rt_sensitivity) {
                    s_rt_pressed[ch] = true;
                }
            } else {
                // Pressed: release if we've moved enough toward "rest".
                bool toward_rest = inverted ? going_up : !going_up;
                if (toward_rest && travel >= s_rt_sensitivity) {
                    s_rt_pressed[ch] = false;
                }
            }
            pressed = s_rt_pressed[ch];
        }

        if (pressed) new_mask |=  (1u << i);
        else         new_mask &= ~(1u << i);
    }

    g_adc_pressed_mask = new_mask;
}

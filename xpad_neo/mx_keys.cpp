#include "mx_keys.h"
#include "xpad_config.h"
#include "config.h"
#include <Arduino.h>
#include <string.h>

// =============================================================================
// mx_keys.cpp — MX switch debounce and scanning
//
// Learning notes:
//   Contact bounce — when a mechanical switch closes, the contacts physically
//   bounce several times before settling. This produces a burst of rapid
//   HIGH/LOW transitions that can be mistaken for multiple key presses.
//   A simple software fix is to ignore transitions shorter than a threshold
//   (MX_DEBOUNCE_MS). See Ganssle's "A Guide to Debouncing" for deeper study.
// =============================================================================

volatile uint32_t g_mx_pressed_mask = 0;

// Per-key debounce state.
struct DebounceState {
    bool     raw_pressed;         // Last raw GPIO reading
    bool     debounced_pressed;   // Stable (confirmed) state
    uint32_t last_change_ms;      // millis() when the raw state last changed
};

static DebounceState s_state[MAX_KEYS] = {};

// ---------------------------------------------------------------------------
void mx_keys_setup() {
    const XpadConfig* cfg = config_get();

    for (int i = 0; i < MAX_KEYS; i++) {
        const KeyMapping* k = &cfg->keys[i];
        if (k->type != KEY_TYPE_MX) continue;
        if (k->gpio_pin > 29) continue;

        // INPUT_PULLUP: pin reads HIGH at rest, LOW when switch is closed.
        pinMode(k->gpio_pin, INPUT_PULLUP);
    }
}

// ---------------------------------------------------------------------------
void mx_keys_reinit() {
    // Drop all debounce state and the pressed mask — slot→key assignments may
    // have changed, so per-slot state from the old keymap is meaningless.
    memset((void*)s_state, 0, sizeof(s_state));
    g_mx_pressed_mask = 0;
    mx_keys_setup();
}

// ---------------------------------------------------------------------------
void mx_keys_scan() {
    const XpadConfig* cfg = config_get();
    uint32_t now = millis();
    uint32_t new_mask = g_mx_pressed_mask;

    for (int i = 0; i < MAX_KEYS; i++) {
        const KeyMapping* k = &cfg->keys[i];
        if (k->type != KEY_TYPE_MX) continue;
        if (k->gpio_pin > 29) continue;

        // Active-low: LOW = pressed, HIGH = released.
        bool raw = (digitalRead(k->gpio_pin) == LOW);
        DebounceState& ds = s_state[i];

        if (raw != ds.raw_pressed) {
            // Pin state changed — record the time and wait.
            ds.raw_pressed     = raw;
            ds.last_change_ms  = now;
        }

        // Only accept the new state after it has been stable for the debounce window.
        if ((now - ds.last_change_ms) >= MX_DEBOUNCE_MS &&
             raw != ds.debounced_pressed) {
            ds.debounced_pressed = raw;
            if (raw) new_mask |=  (1u << i);
            else     new_mask &= ~(1u << i);
        }
    }

    g_mx_pressed_mask = new_mask;
}

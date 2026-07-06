#include "hid_keyboard.h"
#include "xpad_config.h"
#include "adc_keys.h"
#include "mx_keys.h"
#include <Adafruit_TinyUSB.h>

// =============================================================================
// hid_keyboard.cpp — USB HID keyboard output
//
// Learning notes:
//   A USB keyboard sends reports to the host. Each report contains:
//     • 1 byte  — modifier keys (Ctrl, Shift, Alt, Win/Meta)
//     • 1 byte  — reserved (always 0x00)
//     • 6 bytes — up to 6 simultaneous key HID Usage IDs (keycodes)
//   This is the "boot protocol" keyboard report format, compatible with
//   all operating systems without any driver.
//
//   We also include a Consumer Control report (media keys) for future use.
//   Consumer codes are 16-bit values defined in the HID Usage Tables §15.
// =============================================================================

#define REPORT_ID_KEYBOARD  1
#define REPORT_ID_CONSUMER  2

// HID report descriptor.
// TUD_HID_REPORT_DESC_KEYBOARD and TUD_HID_REPORT_DESC_CONSUMER are macros
// from TinyUSB that generate standard report descriptors for these two types.
static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
};

const uint8_t*  k_hid_report_descriptor     = s_hid_report_desc;
const uint16_t  k_hid_report_descriptor_len = sizeof(s_hid_report_desc);

// TinyUSB HID device instance.
// This object handles USB enumeration, descriptor serving, and report sending.
static Adafruit_USBD_HID s_usb_hid;

// Last sent report — we only send a new report when the state changes to
// avoid flooding the USB bus with redundant data.
static uint8_t s_last_modifier  = 0;
static uint8_t s_last_keys[6]   = {};

// ---------------------------------------------------------------------------
void hid_keyboard_setup() {
    s_usb_hid.setPollInterval(1);    // 1 ms polling interval (fastest allowed by USB HID)
    s_usb_hid.setReportDescriptor(k_hid_report_descriptor, k_hid_report_descriptor_len);
    s_usb_hid.begin();
}

// ---------------------------------------------------------------------------
void hid_keyboard_task() {
    if (!s_usb_hid.ready()) return;  // USB not connected or HID not ready

    const XpadConfig* cfg = config_get();

    // Collect pressed keycodes from MX and ADC key masks.
    // A standard HID keyboard report can hold at most 6 simultaneous keycodes.
    uint8_t keycodes[6] = {};
    uint8_t modifier    = 0;
    int     count       = 0;

    uint32_t combined_mask = g_mx_pressed_mask | g_adc_pressed_mask;

    for (int i = 0; i < MAX_KEYS && count < 6; i++) {
        if (!(combined_mask & (1u << i))) continue;

        const KeyMapping* k = &cfg->keys[i];
        if (k->type == KEY_TYPE_NONE) continue;
        if (k->hid_keycode == 0) continue;

        // OR in any modifier bits (Ctrl/Shift/Alt/Win) from this key's config.
        modifier |= k->modifiers;
        keycodes[count++] = k->hid_keycode;
    }

    // Only send a new USB report if something changed since the last report.
    // This reduces unnecessary USB traffic during idle.
    bool changed = (modifier != s_last_modifier);
    if (!changed) {
        for (int i = 0; i < 6; i++) {
            if (keycodes[i] != s_last_keys[i]) { changed = true; break; }
        }
    }

    if (changed) {
        s_usb_hid.keyboardReport(REPORT_ID_KEYBOARD, modifier, keycodes);
        s_last_modifier = modifier;
        for (int i = 0; i < 6; i++) s_last_keys[i] = keycodes[i];
    }
}

// ---------------------------------------------------------------------------
// Required TinyUSB callback — called when the host sends a SET_REPORT request.
// We don't use this (no LED output reports), but TinyUSB requires the symbol.
extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, const uint8_t* buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
    // If you later add keyboard LED feedback (Caps Lock indicator etc.),
    // handle it here. report_id == 1 for keyboard, buffer[0] = LED bitmask.
}

// Required TinyUSB callback — called when the host requests a GET_REPORT.
extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
    hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

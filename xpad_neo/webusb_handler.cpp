#include "webusb_handler.h"
#include "xpad_config.h"
#include "flash_storage.h"
#include "adc_keys.h"
#include "mx_keys.h"
#include "config.h"
#include <Adafruit_TinyUSB.h>
#include <string.h>

// =============================================================================
// webusb_handler.cpp — USB config protocol (mirrors XPAD firmware webusb.cpp)
//
// Learning notes:
//   Dispatch table pattern: instead of a long if/else chain, we store
//   {command byte, handler function} pairs in an array. The dispatcher loop
//   walks the array until it finds a matching command, then calls the handler.
//   Adding a new command requires only two lines (handler + table entry).
//
//   All multi-byte integers in USB packets are little-endian (low byte first).
//   Helper put_u16le / get_u16le handle the byte ordering.
// =============================================================================

// USB Vendor class — provides raw bulk IN/OUT endpoints.
// The web UI accesses this via navigator.usb (WebUSB API).
static Adafruit_USBD_Vendor s_vendor;

// ---------------------------------------------------------------------------
// Command codes — must match the values expected by the XTIA web UI.
// ---------------------------------------------------------------------------
#define CMD_READ_STATUS       0x01
#define CMD_CAPTURE_ACTIVE    0x12
#define CMD_WRITE_PARAMS      0x20
#define CMD_WRITE_KEYMAP      0x30
#define CMD_WRITE_LAYOUT      0x31
#define CMD_READ_LAYOUT       0x32
#define CMD_READ_KEYS         0x33
#define CMD_READ_INPUT        0x34
// Reserved for future modules — define new commands here:
// #define CMD_WRITE_MACRO    0x35  (FEATURE_MACRO)
// #define CMD_READ_MACRO     0x36  (FEATURE_MACRO)
// #define CMD_WRITE_ENC      0x37  (FEATURE_ENCODER)
// #define CMD_READ_ENC       0x38  (FEATURE_ENCODER)
// #define CMD_RUMBLE_TEST    0x39  (FEATURE_RUMBLE)
// #define CMD_READ_AUDIO     0x40  (FEATURE_MIC)

// ---------------------------------------------------------------------------
// Byte-order helpers
// ---------------------------------------------------------------------------
static void put_u16le(uint8_t* buf, uint16_t v) {
    buf[0] = (uint8_t)(v & 0xFF);
    buf[1] = (uint8_t)(v >> 8);
}

static uint16_t get_u16le(const uint8_t* buf) {
    return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

static void send(const uint8_t* data, size_t len) {
    s_vendor.write(data, len);
    s_vendor.flush();
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_STATUS (0x01)
// Returns live ADC readings + full calibration snapshot (43 bytes).
// The ADC calibration page calls this in a polling loop to animate the bars.
//
// Response layout (43 bytes):
//   [0]     0x01   echo of command byte
//   [1]     0x00   status (0 = OK)
//   [2..9]  raw ADC values, 4×u16le
//   [10..13] travel %, 4×u8
//   [14..21] adc_rest, 4×u16le
//   [22..29] adc_active, 4×u16le
//   [30..37] adc_trigger, 4×u16le
//   [38]    adc_smoothing
//   [39]    adc_polarity
//   [40]    rt_enabled
//   [41..42] rt_sensitivity, u16le
// ---------------------------------------------------------------------------
static void handle_read_status(const uint8_t* /*buf*/, uint8_t /*len*/) {
    const XpadConfig* cfg = config_get();
    uint8_t resp[43] = {};
    int o = 0;

    resp[o++] = CMD_READ_STATUS;
    resp[o++] = 0x00; // status OK

    for (int i = 0; i < 4; i++) { put_u16le(resp + o, g_adc_raw[i]); o += 2; }
    for (int i = 0; i < 4; i++) { resp[o++] = g_adc_pct[i]; }
    for (int i = 0; i < 4; i++) { put_u16le(resp + o, cfg->adc_rest[i]);    o += 2; }
    for (int i = 0; i < 4; i++) { put_u16le(resp + o, cfg->adc_active[i]);  o += 2; }
    for (int i = 0; i < 4; i++) { put_u16le(resp + o, cfg->adc_trigger[i]); o += 2; }
    resp[o++] = cfg->adc_smoothing;
    resp[o++] = cfg->adc_polarity;
    resp[o++] = cfg->rt_enabled;
    put_u16le(resp + o, cfg->rt_sensitivity); o += 2;

    send(resp, o);
}

// ---------------------------------------------------------------------------
// Handler: CMD_CAPTURE_ACTIVE (0x12)
// Sample the current ADC value for one channel and record it as adc_active[ch].
// Used by the calibration page: user presses a key fully, clicks "Capture".
//
// Packet: [0x12, channel]
// Response: [0x12, channel, val_lo, val_hi]
// ---------------------------------------------------------------------------
static void handle_capture_active(const uint8_t* buf, uint8_t len) {
    if (len < 2) return;
    uint8_t ch = buf[1];
    if (ch >= 4) return;

    XpadConfig* cfg = config_get();
    cfg->adc_active[ch] = g_adc_raw[ch];
    adc_keys_update_params();

    uint8_t resp[4] = { CMD_CAPTURE_ACTIVE, ch };
    put_u16le(resp + 2, g_adc_raw[ch]);
    send(resp, 4);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_PARAMS (0x20)
// Save a new set of calibration and Rapid Trigger parameters to flash.
//
// Packet (22 bytes):
//   [0]     0x20
//   [1]     adc_smoothing
//   [2]     adc_polarity
//   [3..10] adc_active[0..3], 4×u16le
//   [11..18] adc_trigger[0..3], 4×u16le
//   [19]    rt_enabled
//   [20..21] rt_sensitivity, u16le
// ---------------------------------------------------------------------------
static void handle_write_params(const uint8_t* buf, uint8_t len) {
    if (len < 22) return;
    XpadConfig* cfg = config_get();

    cfg->adc_smoothing = buf[1];
    cfg->adc_polarity  = buf[2];
    for (int i = 0; i < 4; i++) cfg->adc_active[i]  = get_u16le(buf + 3  + i * 2);
    for (int i = 0; i < 4; i++) cfg->adc_trigger[i] = get_u16le(buf + 11 + i * 2);
    cfg->rt_enabled    = buf[19];
    cfg->rt_sensitivity = get_u16le(buf + 20);

    adc_keys_update_params();
    config_save();

    uint8_t resp[2] = { CMD_WRITE_PARAMS, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_KEYMAP (0x30)
// Update key→HID mappings for a batch of keys and save to flash.
//
// Packet: [0x30, count, gpio, hid, mod, gpio, hid, mod, ...]
//   Each entry is 3 bytes: GPIO pin, HID keycode, modifier byte.
//   The handler finds the matching key in keys[] by GPIO pin and updates it.
// ---------------------------------------------------------------------------
static void handle_write_keymap(const uint8_t* buf, uint8_t len) {
    if (len < 2) return;
    uint8_t count = buf[1];
    if (len < (uint8_t)(2 + count * 3)) return;

    XpadConfig* cfg = config_get();

    for (int e = 0; e < count; e++) {
        uint8_t gpio = buf[2 + e * 3];
        uint8_t hid  = buf[3 + e * 3];
        uint8_t mod  = buf[4 + e * 3];

        // Find the key entry for this GPIO pin and update it.
        for (int i = 0; i < MAX_KEYS; i++) {
            if (cfg->keys[i].gpio_pin == gpio && cfg->keys[i].type != KEY_TYPE_NONE) {
                cfg->keys[i].hid_keycode = hid;
                cfg->keys[i].modifiers   = mod;
                break;
            }
        }
    }

    config_save();

    uint8_t resp[2] = { CMD_WRITE_KEYMAP, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_LAYOUT (0x31)
// Save the visual matrix layout (rows, cols, cell→gpio mapping) to flash.
// This is what the web UI's "Write Layout" button sends.
//
// Packet: [0x31, rows, cols, matrix[rows][cols] ...]
//   matrix cells contain gpio_pin values; 0xFF = empty cell
// ---------------------------------------------------------------------------
static void handle_write_layout(const uint8_t* buf, uint8_t len) {
    if (len < 3) return;
    XpadConfig* cfg = config_get();

    cfg->layout_rows = buf[1];
    cfg->layout_cols = buf[2];

    uint8_t rows = cfg->layout_rows;
    uint8_t cols = cfg->layout_cols;
    if (rows > LAYOUT_MAX_ROWS) rows = LAYOUT_MAX_ROWS;
    if (cols > LAYOUT_MAX_COLS) cols = LAYOUT_MAX_COLS;

    size_t expected = 3 + rows * cols;
    if (len < expected) return;

    memset(cfg->layout_matrix, 0xFF, sizeof(cfg->layout_matrix));
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            cfg->layout_matrix[r][c] = buf[3 + r * cols + c];
        }
    }

    config_save();

    uint8_t resp[2] = { CMD_WRITE_LAYOUT, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_LAYOUT (0x32)
// Return the stored visual matrix layout.
//
// Response: [0x32, 0x00, rows, cols, matrix[rows][cols]]
// ---------------------------------------------------------------------------
static void handle_read_layout(const uint8_t* /*buf*/, uint8_t /*len*/) {
    const XpadConfig* cfg = config_get();
    uint8_t resp[3 + LAYOUT_MAX_ROWS * LAYOUT_MAX_COLS];
    resp[0] = CMD_READ_LAYOUT;
    resp[1] = 0x00;
    resp[2] = cfg->layout_rows;
    resp[3] = cfg->layout_cols;

    uint8_t rows = cfg->layout_rows;
    uint8_t cols = cfg->layout_cols;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            resp[4 + r * cols + c] = cfg->layout_matrix[r][c];
        }
    }
    send(resp, 4 + rows * cols);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_KEYS (0x33)
// Return all configured key→HID mappings.
//
// Response: [0x33, 0x00, count, gpio, hid, mod, type, ...]
//   Each entry is 4 bytes: gpio_pin, hid_keycode, modifiers, type
// ---------------------------------------------------------------------------
static void handle_read_keys(const uint8_t* /*buf*/, uint8_t /*len*/) {
    const XpadConfig* cfg = config_get();
    uint8_t resp[2 + 1 + MAX_KEYS * 4];
    resp[0] = CMD_READ_KEYS;
    resp[1] = 0x00;

    uint8_t count = 0;
    for (int i = 0; i < MAX_KEYS; i++) {
        if (cfg->keys[i].type == KEY_TYPE_NONE) continue;
        resp[3 + count * 4 + 0] = cfg->keys[i].gpio_pin;
        resp[3 + count * 4 + 1] = cfg->keys[i].hid_keycode;
        resp[3 + count * 4 + 2] = cfg->keys[i].modifiers;
        resp[3 + count * 4 + 3] = cfg->keys[i].type;
        count++;
    }
    resp[2] = count;
    send(resp, 3 + count * 4);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_INPUT (0x34)
// Return the current raw input state: which GPIO pins are pressed and
// the travel percentage of each ADC channel.
// Used by the web UI's live key-test mode.
//
// Response: [0x34, 0x00, gpio_lo, gpio_mid, gpio_hi, gpio_top, adc0%, adc1%, adc2%, adc3%]
//   gpio bytes: 32-bit bitmask of pressed MX GPIO pins, little-endian
//   adc%: travel percentage (0-100) per channel
// ---------------------------------------------------------------------------
static void handle_read_input(const uint8_t* /*buf*/, uint8_t /*len*/) {
    uint32_t gpio_mask = 0;
    const XpadConfig* cfg = config_get();

    // Rebuild a GPIO-indexed bitmask from the key-indexed pressed mask.
    for (int i = 0; i < MAX_KEYS; i++) {
        if (!(g_mx_pressed_mask & (1u << i))) continue;
        if (cfg->keys[i].gpio_pin <= 29) {
            gpio_mask |= (1u << cfg->keys[i].gpio_pin);
        }
    }

    uint8_t resp[10];
    resp[0] = CMD_READ_INPUT;
    resp[1] = 0x00;
    resp[2] = (uint8_t)(gpio_mask & 0xFF);
    resp[3] = (uint8_t)((gpio_mask >> 8)  & 0xFF);
    resp[4] = (uint8_t)((gpio_mask >> 16) & 0xFF);
    resp[5] = (uint8_t)((gpio_mask >> 24) & 0xFF);
    resp[6] = g_adc_pct[0];
    resp[7] = g_adc_pct[1];
    resp[8] = g_adc_pct[2];
    resp[9] = g_adc_pct[3];
    send(resp, 10);
}

// ---------------------------------------------------------------------------
// Dispatch table — maps command bytes to handler functions.
// To add a new command: write a handler, then add one entry here.
// ---------------------------------------------------------------------------
typedef void (*CmdHandler)(const uint8_t* buf, uint8_t len);

struct HandlerEntry {
    uint8_t    cmd;
    CmdHandler fn;
};

static const HandlerEntry kHandlers[] = {
    { CMD_READ_STATUS,    handle_read_status    },
    { CMD_CAPTURE_ACTIVE, handle_capture_active },
    { CMD_WRITE_PARAMS,   handle_write_params   },
    { CMD_WRITE_KEYMAP,   handle_write_keymap   },
    { CMD_WRITE_LAYOUT,   handle_write_layout   },
    { CMD_READ_LAYOUT,    handle_read_layout    },
    { CMD_READ_KEYS,      handle_read_keys      },
    { CMD_READ_INPUT,     handle_read_input     },
    // Add new handlers here — one line per command.
};

static const size_t kHandlerCount = sizeof(kHandlers) / sizeof(kHandlers[0]);

// ---------------------------------------------------------------------------
void webusb_handler_setup() {
    s_vendor.begin();
}

// ---------------------------------------------------------------------------
void webusb_handler_task() {
    if (!s_vendor.available()) return;

    uint8_t buf[64];
    int n = s_vendor.read(buf, sizeof(buf));
    if (n <= 0) return;

    uint8_t cmd = buf[0];

    // Walk the dispatch table — O(N) but N is tiny (< 20 commands).
    for (size_t i = 0; i < kHandlerCount; i++) {
        if (kHandlers[i].cmd == cmd) {
            kHandlers[i].fn(buf, (uint8_t)n);
            return;
        }
    }
    // Unknown command — silently ignore. The web UI retries on timeout.
}

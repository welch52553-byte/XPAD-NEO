#include "webusb_handler.h"
#include "xpad_config.h"
#include "flash_storage.h"
#include "adc_keys.h"
#include "mx_keys.h"
#include "config.h"
#include <Arduino.h>
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
#define CMD_GET_VERSION       0x02   // [] → [0x02, 0x00, major, minor, patch]
#define CMD_CAPTURE_ACTIVE    0x12
#define CMD_WRITE_PARAMS      0x20
#define CMD_WRITE_KEYMAP      0x30
#define CMD_WRITE_LAYOUT      0x31
#define CMD_READ_LAYOUT       0x32
#define CMD_READ_KEYS         0x33
#define CMD_READ_INPUT        0x34
#define CMD_WRITE_MACRO       0x35   // gpio, len, text[len] → ACK [0x35, 0x00]
#define CMD_READ_MACRO        0x36   // slot → [0x36, 0x00, gpio, len, text[len]]
#define CMD_WRITE_ENC         0x37   // (type,code,mod) × ccw,cw,sw → ACK
#define CMD_READ_ENC          0x38   // [] → [0x38, 0x00, (type,code,mod) × ccw,cw,sw]
// Reserved for future modules — define new commands here:
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
    // Responses can exceed one 64-byte USB packet (READ_LAYOUT is 84 bytes),
    // so loop until the whole buffer has been handed to the TX FIFO.
    size_t written = 0;
    while (written < len) {
        size_t n = s_vendor.write(data + written, len - written);
        s_vendor.flush();
        written += n;
        if (n == 0) break; // host stopped reading — give up rather than spin
    }
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
// Handler: CMD_GET_VERSION (0x02)
// Report the firmware version so the web UI can gate features by protocol
// level (e.g. 8-row layouts require >= 1.2).
//
// Response: [0x02, 0x00, major, minor, patch]
// ---------------------------------------------------------------------------
static void handle_get_version(const uint8_t* /*buf*/, uint8_t /*len*/) {
    uint8_t resp[5] = { CMD_GET_VERSION, 0x00,
                        XPAD_FW_VER_MAJOR, XPAD_FW_VER_MINOR, XPAD_FW_VER_PATCH };
    send(resp, 5);
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
//   Updates an existing keys[] entry whose gpio_pin matches, or creates a new
//   one in the first free slot — CMD_WRITE_LAYOUT clears keys[] first, so a
//   full layout write from the web UI always lands in fresh slots.
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

        // Whitelist: only physical key pins may enter keys[] — GPIO 0-7 (MX)
        // and 26-29 (ADC). Anything else (virtual layout slots >= 0xC1,
        // reserved peripheral pins) must never be scanned as a key.
        if (gpio > 7 && !(gpio >= 26 && gpio <= 29)) continue;

        // Find the key entry for this GPIO pin and update it.
        bool found = false;
        for (int i = 0; i < MAX_KEYS; i++) {
            if (cfg->keys[i].type != KEY_TYPE_NONE && cfg->keys[i].gpio_pin == gpio) {
                cfg->keys[i].hid_keycode = hid;
                cfg->keys[i].modifiers   = mod;
                found = true;
                break;
            }
        }

        // No existing entry — create a new key in the first free slot.
        // GPIO 26-29 are the RP2040 ADC channels; all others are digital MX.
        if (!found) {
            for (int i = 0; i < MAX_KEYS; i++) {
                if (cfg->keys[i].type == KEY_TYPE_NONE) {
                    cfg->keys[i].gpio_pin    = gpio;
                    cfg->keys[i].hid_keycode = hid;
                    cfg->keys[i].modifiers   = mod;
                    cfg->keys[i].type        = (gpio >= 26 && gpio <= 29)
                                               ? KEY_TYPE_ADC : KEY_TYPE_MX;
                    break;
                }
            }
        }
    }

    config_save();
    mx_keys_reinit();  // rebuild the GPIO scan list so new keys work immediately

    uint8_t resp[2] = { CMD_WRITE_KEYMAP, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_LAYOUT (0x31)
// Save the visual matrix layout (rows, cols, cell→gpio mapping) to flash.
// This is what the web UI's "Write Layout" button sends — always the full
// fixed-size matrix: 3 + 8×10 = 83 bytes (spans two 64-byte USB packets; the
// dispatcher accumulates them before calling this handler).
//
// Clears all key mappings and macros before writing the new layout, so stale
// entries from a previous layout can never bleed through. The web UI sends
// WRITE_KEYMAP and WRITE_MACRO right after to repopulate them.
// ADC calibration fields are preserved unchanged.
//
// Packet: [0x31, rows, cols, matrix[LAYOUT_MAX_ROWS][LAYOUT_MAX_COLS]]
//   matrix cells contain gpio_pin values; 0xFF = empty cell
// ---------------------------------------------------------------------------
static void handle_write_layout(const uint8_t* buf, uint8_t len) {
    const uint8_t matrix_bytes = LAYOUT_MAX_ROWS * LAYOUT_MAX_COLS;
    if (len < (uint8_t)(3 + matrix_bytes)) return;

    uint8_t rows = buf[1];
    uint8_t cols = buf[2];
    if (rows > LAYOUT_MAX_ROWS || cols > LAYOUT_MAX_COLS) return;

    XpadConfig* cfg = config_get();

    // Blank slate for the WRITE_KEYMAP / WRITE_MACRO commands that follow.
    memset(cfg->keys, 0, sizeof(cfg->keys));
    memset(cfg->macro_gpio, 0xFF, sizeof(cfg->macro_gpio));
    memset(cfg->macro_text, 0, sizeof(cfg->macro_text));

    cfg->layout_rows = rows;
    cfg->layout_cols = cols;
    memcpy(cfg->layout_matrix, buf + 3, matrix_bytes);

    config_save();

    uint8_t resp[2] = { CMD_WRITE_LAYOUT, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_LAYOUT (0x32)
// Return the stored visual matrix layout — always the full fixed-size matrix,
// 4 + 80 = 84 bytes (64-byte packet + 20-byte short packet on the wire).
//
// Response: [0x32, 0x00, rows, cols, matrix[LAYOUT_MAX_ROWS][LAYOUT_MAX_COLS]]
// ---------------------------------------------------------------------------
static void handle_read_layout(const uint8_t* /*buf*/, uint8_t /*len*/) {
    const XpadConfig* cfg = config_get();
    uint8_t resp[4 + LAYOUT_MAX_ROWS * LAYOUT_MAX_COLS] = {};
    resp[0] = CMD_READ_LAYOUT;
    resp[1] = 0x00;
    resp[2] = cfg->layout_rows;
    resp[3] = cfg->layout_cols;
    memcpy(resp + 4, cfg->layout_matrix, LAYOUT_MAX_ROWS * LAYOUT_MAX_COLS);
    send(resp, sizeof(resp));
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
// Response (13 bytes, matches XPAD firmware 1.2):
//   [0x34, 0x00, gpio_pressed u32LE, adc0-3 travel %, enc_cw, enc_ccw, enc_sw]
//   gpio bytes: 32-bit bitmask of pressed MX GPIO pins, little-endian
//   adc%: travel percentage (0-100) per channel
//   enc_*: rotation ticks since last poll / button state — always 0 unless
//          FEATURE_ENCODER is enabled (no encoder on base XPAD-NEO hardware)
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

    uint8_t resp[13] = {};
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
    resp[10] = 0;  // enc_cw  — reserved for FEATURE_ENCODER
    resp[11] = 0;  // enc_ccw — reserved for FEATURE_ENCODER
    resp[12] = 0;  // enc_sw  — reserved for FEATURE_ENCODER
    send(resp, 13);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_MACRO (0x35)
// Store macro text for one GPIO. Finds the existing slot for that GPIO or
// claims a free one; len = 0 clears the slot. The macro fields are always
// stored so the web UI round-trips them — playback itself requires
// FEATURE_MACRO.
//
// Packet: [0x35, gpio, len, char0..charN]
// Response: [0x35, 0x00] on success, [0x35, 0x01] when no free slot
// ---------------------------------------------------------------------------
static void handle_write_macro(const uint8_t* buf, uint8_t len) {
    if (len < 3) return;
    uint8_t gpio     = buf[1];
    uint8_t text_len = buf[2];
    // Reserve one byte for the NUL terminator.
    if (text_len >= MACRO_TEXT_LEN) text_len = MACRO_TEXT_LEN - 1;
    if (len < (uint8_t)(3 + text_len)) return;

    XpadConfig* cfg = config_get();

    // Find the existing slot for this GPIO, or a free slot.
    int slot = -1;
    for (int i = 0; i < MACRO_KEYS_MAX; i++) {
        if (cfg->macro_gpio[i] == gpio) { slot = i; break; }
    }
    if (slot == -1 && text_len > 0) {
        for (int i = 0; i < MACRO_KEYS_MAX; i++) {
            if (cfg->macro_gpio[i] == 0xFF) { slot = i; break; }
        }
    }
    if (slot == -1) {
        uint8_t resp[2] = { CMD_WRITE_MACRO, 0x01 }; // no free slot
        send(resp, 2);
        return;
    }

    if (text_len == 0) {
        cfg->macro_gpio[slot] = 0xFF;
        cfg->macro_text[slot][0] = '\0';
    } else {
        cfg->macro_gpio[slot] = gpio;
        memcpy(cfg->macro_text[slot], buf + 3, text_len);
        cfg->macro_text[slot][text_len] = '\0';
    }
    config_save();

    uint8_t resp[2] = { CMD_WRITE_MACRO, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_MACRO (0x36)
// Return one macro slot's GPIO and text.
//
// Packet: [0x36, slot]
// Response: [0x36, 0x00, gpio, len, char0..charN]  (gpio = 0xFF if empty)
// ---------------------------------------------------------------------------
static void handle_read_macro(const uint8_t* buf, uint8_t len) {
    if (len < 2) return;
    uint8_t slot = buf[1];
    if (slot >= MACRO_KEYS_MAX) return;

    const XpadConfig* cfg = config_get();
    uint8_t text_len = (uint8_t)strnlen(cfg->macro_text[slot], MACRO_TEXT_LEN);

    uint8_t resp[4 + MACRO_TEXT_LEN] = {};
    resp[0] = CMD_READ_MACRO;
    resp[1] = 0x00;
    resp[2] = cfg->macro_gpio[slot];   // 0xFF if empty
    resp[3] = text_len;
    if (text_len > 0) memcpy(resp + 4, cfg->macro_text[slot], text_len);
    send(resp, 4 + text_len);
}

// ---------------------------------------------------------------------------
// Handler: CMD_WRITE_ENC (0x37)
// Store the rotary-encoder sub-action mappings. Always stored so the web UI
// round-trips them — the encoder itself runs only with FEATURE_ENCODER.
//
// Packet: [0x37, ccw_type, ccw_code, ccw_mod,
//                cw_type,  cw_code,  cw_mod,
//                sw_type,  sw_code,  sw_mod]
// Response: [0x37, 0x00]
// ---------------------------------------------------------------------------
static void handle_write_enc(const uint8_t* buf, uint8_t len) {
    if (len < 10) return;
    XpadConfig* cfg = config_get();

    cfg->enc_ccw_type = buf[1]; cfg->enc_ccw_code = buf[2]; cfg->enc_ccw_mod = buf[3];
    cfg->enc_cw_type  = buf[4]; cfg->enc_cw_code  = buf[5]; cfg->enc_cw_mod  = buf[6];
    cfg->enc_sw_type  = buf[7]; cfg->enc_sw_code  = buf[8]; cfg->enc_sw_mod  = buf[9];
    config_save();

    uint8_t resp[2] = { CMD_WRITE_ENC, 0x00 };
    send(resp, 2);
}

// ---------------------------------------------------------------------------
// Handler: CMD_READ_ENC (0x38)
// Return the stored rotary-encoder sub-action mappings.
//
// Response: [0x38, 0x00, (type, code, mod) × ccw, cw, sw]
// ---------------------------------------------------------------------------
static void handle_read_enc(const uint8_t* /*buf*/, uint8_t /*len*/) {
    const XpadConfig* cfg = config_get();
    uint8_t resp[11];
    resp[0] = CMD_READ_ENC;
    resp[1] = 0x00;
    resp[2] = cfg->enc_ccw_type; resp[3] = cfg->enc_ccw_code; resp[4]  = cfg->enc_ccw_mod;
    resp[5] = cfg->enc_cw_type;  resp[6] = cfg->enc_cw_code;  resp[7]  = cfg->enc_cw_mod;
    resp[8] = cfg->enc_sw_type;  resp[9] = cfg->enc_sw_code;  resp[10] = cfg->enc_sw_mod;
    send(resp, 11);
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
    { CMD_GET_VERSION,    handle_get_version    },
    { CMD_CAPTURE_ACTIVE, handle_capture_active },
    { CMD_WRITE_PARAMS,   handle_write_params   },
    { CMD_WRITE_KEYMAP,   handle_write_keymap   },
    { CMD_WRITE_LAYOUT,   handle_write_layout   },
    { CMD_READ_LAYOUT,    handle_read_layout    },
    { CMD_READ_KEYS,      handle_read_keys      },
    { CMD_READ_INPUT,     handle_read_input     },
    { CMD_WRITE_MACRO,    handle_write_macro    },
    { CMD_READ_MACRO,     handle_read_macro     },
    { CMD_WRITE_ENC,      handle_write_enc      },
    { CMD_READ_ENC,       handle_read_enc       },
    // Add new handlers here — one line per command.
};

static const size_t kHandlerCount = sizeof(kHandlers) / sizeof(kHandlers[0]);

// ---------------------------------------------------------------------------
void webusb_handler_setup() {
    s_vendor.begin();
}

// ---------------------------------------------------------------------------
void webusb_handler_task() {
    // WRITE_LAYOUT (3 + 8×10 = 83 bytes) is the only host→device command
    // longer than one 64-byte USB packet, so it can arrive split across two
    // FIFO reads. Accumulate until the full command is in; every other
    // command fits one packet and dispatches immediately.
    static uint8_t  buf[128];
    static uint8_t  fill = 0;
    static uint32_t fill_start_ms = 0;

    if (!s_vendor.available()) return;

    if (fill == 0) fill_start_ms = millis();
    int n = s_vendor.read(buf + fill, sizeof(buf) - fill);
    if (n > 0) fill += (uint8_t)n;
    if (fill == 0) return;

    const uint8_t layout_len = 3 + LAYOUT_MAX_ROWS * LAYOUT_MAX_COLS;
    if (buf[0] == CMD_WRITE_LAYOUT && fill < layout_len) {
        // Drop a stale partial so a truncated packet can't wedge the parser.
        if (millis() - fill_start_ms > 100) fill = 0;
        return;
    }

    uint8_t cmd = buf[0];
    uint8_t len = fill;
    fill = 0;

    // Walk the dispatch table — O(N) but N is tiny (< 20 commands).
    for (size_t i = 0; i < kHandlerCount; i++) {
        if (kHandlers[i].cmd == cmd) {
            kHandlers[i].fn(buf, len);
            return;
        }
    }
    // Unknown command — silently ignore. The web UI retries on timeout.
}

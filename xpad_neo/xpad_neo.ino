// XPAD-NEO
// MX-only Arduino keyboard firmware.
//
// This main file contains the beginner-facing path:
//   MX switch -> GPIO scan -> debounce -> USB HID keyboard report
//
// Layout Generator WebUSB and Flash preset code lives in
// layout_generator.ino so it does not hide the core Arduino flow.

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <string.h>

// =============================================================================
// User-editable settings
// =============================================================================

// USB identity shown to the host computer.
static const uint16_t USB_VID = 0x1209;
static const uint16_t USB_PID = 0x0002;

// Mechanical switches are active-low:
//   released = HIGH because INPUT_PULLUP is enabled
//   pressed  = LOW because the switch connects the pin to GND
static const uint8_t MX_DEBOUNCE_MS = 5;

// One MX switch mapping entry:
//   gpio       = RP2040 GPIO number connected to the MX switch.
//   hidKeycode = USB HID keyboard keycode sent when the switch is pressed.
//   modifiers  = USB keyboard modifier bits, such as Ctrl/Shift/Alt.
struct MxKey {
    uint8_t gpio;
    uint8_t hidKeycode;
    uint8_t modifiers;
};

// Default fallback preset source.
//
// Layout selection rule:
//   1. If a valid XPAD-NEO Flash preset exists, use it.
//   2. Otherwise use this hardcoded fallback preset.
//
// XPAD-NEO currently supports up to eight MX switch positions. This fallback
// preset uses GP0-GP7, but future board variants or teaching experiments may
// use fewer switches or different GPIO pins. F13-F20 work well as conflict-free
// defaults for Small Deck and other shortcut applications.
static const MxKey DEFAULT_MX_KEYS[] = {
    { 0, 0x68, 0 }, // F13
    { 1, 0x69, 0 }, // F14
    { 2, 0x6A, 0 }, // F15
    { 3, 0x6B, 0 }, // F16
    { 4, 0x6C, 0 }, // F17
    { 5, 0x6D, 0 }, // F18
    { 6, 0x6E, 0 }, // F19
    { 7, 0x6F, 0 }, // F20
};

static const uint8_t DEFAULT_MX_KEY_COUNT =
    sizeof(DEFAULT_MX_KEYS) / sizeof(DEFAULT_MX_KEYS[0]);
static const uint8_t MX_KEY_CAPACITY = 8;

static_assert(DEFAULT_MX_KEY_COUNT == MX_KEY_CAPACITY,
              "The default preset must fill the eight-key fallback capacity.");

// Layout Generator uses a fixed 8x10 visual matrix. These fields remain in the
// shared Preset because both Arduino tabs use the same active configuration.
static const uint8_t WEB_LAYOUT_MAX_ROWS = 8;
static const uint8_t WEB_LAYOUT_MAX_COLS = 10;
static const uint8_t WEB_LAYOUT_MATRIX_SIZE =
    WEB_LAYOUT_MAX_ROWS * WEB_LAYOUT_MAX_COLS;

struct Preset {
    uint8_t rows;
    uint8_t cols;
    uint8_t layoutMatrix[WEB_LAYOUT_MATRIX_SIZE];
    MxKey keymap[MX_KEY_CAPACITY];
    uint8_t keymapCount;
};

static Preset defaultPreset = {};
static Preset activePreset = {};

// =============================================================================
// USB HID keyboard state
// =============================================================================

static const uint8_t REPORT_ID_KEYBOARD = 1;

static const uint8_t HID_REPORT_DESC[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
};

static Adafruit_USBD_HID usbHid;
static uint8_t lastModifier = 0;
static uint8_t lastKeycodes[6] = {};
static bool lastKeyboardReportValid = false;

// =============================================================================
// MX debounce state
// =============================================================================

struct DebounceState {
    bool rawPressed;
    bool debouncedPressed;
    uint32_t lastChangeMs;
};

static DebounceState keyState[MX_KEY_CAPACITY] = {};
static uint32_t pressedMask = 0;

// Functions implemented in this main file.
static void setupUsbKeyboard();
static void buildDefaultPreset();
static void applyActivePreset();
static void resetScanState();
static void scanMxKeys();
static void sendKeyboardReport();
static bool sameKeys(const uint8_t a[6], const uint8_t b[6]);

// Functions implemented in layout_generator.ino.
static void rebuildUsbConfiguration();
static void setupWebUsb();
static void setupPresetStorage();
static void selectActivePreset();
static void handleWebUsb();

// =============================================================================
// Arduino entry points
// =============================================================================

void setup() {
    rebuildUsbConfiguration();

    TinyUSBDevice.setID(USB_VID, USB_PID);
    TinyUSBDevice.setManufacturerDescriptor("XTIA");
    TinyUSBDevice.setProductDescriptor("XPAD-NEO");

    setupUsbKeyboard();
    setupWebUsb();
    TinyUSBDevice.attach();
    setupPresetStorage();
    buildDefaultPreset();
    selectActivePreset();
}

void loop() {
    scanMxKeys();
    sendKeyboardReport();
    handleWebUsb();
}

// =============================================================================
// Setup and preset application
// =============================================================================

static void setupUsbKeyboard() {
    usbHid.setPollInterval(1);
    usbHid.enableOutEndpoint(true);
    usbHid.setReportDescriptor(HID_REPORT_DESC, sizeof(HID_REPORT_DESC));
    usbHid.begin();
}

static void buildDefaultPreset() {
    memset(&defaultPreset, 0, sizeof(defaultPreset));
    memset(defaultPreset.layoutMatrix, 0xFF, sizeof(defaultPreset.layoutMatrix));

    for (uint8_t i = 0; i < DEFAULT_MX_KEY_COUNT; i++) {
        defaultPreset.layoutMatrix[i] = DEFAULT_MX_KEYS[i].gpio;
        defaultPreset.keymap[i] = DEFAULT_MX_KEYS[i];
    }

    defaultPreset.rows = 1;
    defaultPreset.cols = DEFAULT_MX_KEY_COUNT;
    defaultPreset.keymapCount = DEFAULT_MX_KEY_COUNT;
}

static void applyActivePreset() {
    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY; i++) {
        pinMode(activePreset.keymap[i].gpio, INPUT_PULLUP);
    }

    resetScanState();
}

static void resetScanState() {
    memset(keyState, 0, sizeof(keyState));
    pressedMask = 0;
    lastModifier = 0;
    memset(lastKeycodes, 0, sizeof(lastKeycodes));

    // Force an empty HID report on the next ready loop. This releases any key
    // that the host still considers pressed when a new preset is applied.
    lastKeyboardReportValid = false;
}

// =============================================================================
// MX scanning
// =============================================================================

static void scanMxKeys() {
    const uint32_t now = millis();
    uint32_t newPressedMask = pressedMask;

    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY; i++) {
        const bool rawPressed = (digitalRead(activePreset.keymap[i].gpio) == LOW);
        DebounceState& state = keyState[i];

        if (rawPressed != state.rawPressed) {
            state.rawPressed = rawPressed;
            state.lastChangeMs = now;
        }

        if ((now - state.lastChangeMs) >= MX_DEBOUNCE_MS &&
            rawPressed != state.debouncedPressed) {
            state.debouncedPressed = rawPressed;

            if (rawPressed) {
                newPressedMask |= (1u << i);
            } else {
                newPressedMask &= ~(1u << i);
            }
        }
    }

    pressedMask = newPressedMask;
}

// =============================================================================
// USB keyboard report
// =============================================================================

static void sendKeyboardReport() {
    if (!usbHid.ready()) {
        return;
    }

    uint8_t keycodes[6] = {};
    uint8_t modifier = 0;
    uint8_t count = 0;

    for (uint8_t i = 0;
         i < activePreset.keymapCount && i < MX_KEY_CAPACITY && count < 6;
         i++) {
        if ((pressedMask & (1u << i)) == 0) {
            continue;
        }

        const MxKey& key = activePreset.keymap[i];

        if (key.hidKeycode == 0) {
            continue;
        }

        modifier |= key.modifiers;
        keycodes[count++] = key.hidKeycode;
    }

    if (lastKeyboardReportValid &&
        modifier == lastModifier && sameKeys(keycodes, lastKeycodes)) {
        return;
    }

    if (!usbHid.keyboardReport(REPORT_ID_KEYBOARD, modifier, keycodes)) {
        return;
    }

    lastModifier = modifier;
    memcpy(lastKeycodes, keycodes, sizeof(lastKeycodes));
    lastKeyboardReportValid = true;
}

static bool sameKeys(const uint8_t a[6], const uint8_t b[6]) {
    for (uint8_t i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

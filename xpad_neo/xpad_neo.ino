// XPAD-NEO
// Single-file MX-only Arduino keyboard sketch.
//
// What it does:
//   1. Read MX mechanical switches from GPIO pins.
//   2. Debounce the switch input in software.
//   3. Send normal USB HID keyboard reports.
//
// What it intentionally does not do:
//   - WebUSB / Layout Generator
//   - LittleFS config storage
//   - magnetic / ADC keys
//   - encoder, microphone, vibration, or macros

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

// GPIO pins and default HID keycodes.
// For easy Notepad testing, the default output is 1-8.
struct MxKey {
    uint8_t gpio;
    uint8_t hidKeycode;
    uint8_t modifiers;
};

static const MxKey MX_KEYS[] = {
    { 0, 0x1E, 0 }, // 1
    { 1, 0x1F, 0 }, // 2
    { 2, 0x20, 0 }, // 3
    { 3, 0x21, 0 }, // 4
    { 4, 0x22, 0 }, // 5
    { 5, 0x23, 0 }, // 6
    { 6, 0x24, 0 }, // 7
    { 7, 0x25, 0 }, // 8
};

static const uint8_t MX_KEY_COUNT = sizeof(MX_KEYS) / sizeof(MX_KEYS[0]);

// =============================================================================
// USB HID keyboard setup
// =============================================================================

static const uint8_t REPORT_ID_KEYBOARD = 1;

static const uint8_t HID_REPORT_DESC[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
};

static Adafruit_USBD_HID usbHid;

// Last report sent to the host. We only send a new report when something changes.
static uint8_t lastModifier = 0;
static uint8_t lastKeycodes[6] = {};

// =============================================================================
// MX debounce state
// =============================================================================

struct DebounceState {
    bool rawPressed;
    bool debouncedPressed;
    uint32_t lastChangeMs;
};

static DebounceState keyState[MX_KEY_COUNT] = {};
static uint32_t pressedMask = 0;

// =============================================================================
// Function declarations
// =============================================================================

static void setupUsbKeyboard();
static void setupMxPins();
static void scanMxKeys();
static void sendKeyboardReport();
static bool sameKeys(const uint8_t a[6], const uint8_t b[6]);

// =============================================================================
// Arduino entry points
// =============================================================================

void setup() {
    TinyUSBDevice.setID(USB_VID, USB_PID);
    TinyUSBDevice.setManufacturerDescriptor("XTIA");
    TinyUSBDevice.setProductDescriptor("XPAD-NEO");

    setupUsbKeyboard();

    while (!TinyUSBDevice.mounted()) {
        delay(1);
    }

    setupMxPins();
}

void loop() {
    scanMxKeys();
    sendKeyboardReport();
}

// =============================================================================
// Setup helpers
// =============================================================================

static void setupUsbKeyboard() {
    usbHid.setPollInterval(1);
    usbHid.setReportDescriptor(HID_REPORT_DESC, sizeof(HID_REPORT_DESC));
    usbHid.begin();
}

static void setupMxPins() {
    for (uint8_t i = 0; i < MX_KEY_COUNT; i++) {
        pinMode(MX_KEYS[i].gpio, INPUT_PULLUP);
    }
}

// =============================================================================
// MX scanning
// =============================================================================

static void scanMxKeys() {
    const uint32_t now = millis();
    uint32_t newPressedMask = pressedMask;

    for (uint8_t i = 0; i < MX_KEY_COUNT; i++) {
        const bool rawPressed = (digitalRead(MX_KEYS[i].gpio) == LOW);
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

    for (uint8_t i = 0; i < MX_KEY_COUNT && count < 6; i++) {
        if ((pressedMask & (1u << i)) == 0) {
            continue;
        }

        if (MX_KEYS[i].hidKeycode == 0) {
            continue;
        }

        modifier |= MX_KEYS[i].modifiers;
        keycodes[count++] = MX_KEYS[i].hidKeycode;
    }

    if (modifier == lastModifier && sameKeys(keycodes, lastKeycodes)) {
        return;
    }

    usbHid.keyboardReport(REPORT_ID_KEYBOARD, modifier, keycodes);
    lastModifier = modifier;
    memcpy(lastKeycodes, keycodes, sizeof(lastKeycodes));
}

static bool sameKeys(const uint8_t a[6], const uint8_t b[6]) {
    for (uint8_t i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

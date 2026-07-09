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

// One MX switch mapping entry:
//   gpio       = RP2040 GPIO number connected to the MX switch.
//   hidKeycode = USB HID keyboard keycode sent when the switch is pressed.
//   modifiers  = USB keyboard modifier bits, such as Ctrl/Shift/Alt.
struct MxKey {
    uint8_t gpio;
    uint8_t hidKeycode;
    uint8_t modifiers;
};

// Default fallback layout.
//
// This table is the firmware's built-in layout. It is intentionally placed near
// the top of the sketch so a beginner can change the GPIO pins or HID outputs
// without reading the rest of the code first.
//
// Layout selection rule:
//   1. If a valid Flash layout is available in a future supported format, use it.
//   2. If no valid Flash layout exists, use this hardcoded fallback layout.
//
// The current repository does not define the Flash layout format yet, so the
// firmware always uses this table today. The default output is F13-F20 because
// Small Deck can bind those keys without conflicting with normal typing.
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

// Active layout used by scanning and HID reporting. Today this points to the
// hardcoded fallback layout. Later, Flash loading can replace these pointers
// after validating a stored layout.
static const MxKey* activeMxKeys = DEFAULT_MX_KEYS;
static uint8_t activeMxKeyCount = DEFAULT_MX_KEY_COUNT;

// Keep the state arrays fixed-size so the sketch stays simple and predictable
// for Arduino beginners. A future Flash layout must fit within this capacity.
static const uint8_t MX_KEY_CAPACITY = DEFAULT_MX_KEY_COUNT;

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

static DebounceState keyState[MX_KEY_CAPACITY] = {};
static uint32_t pressedMask = 0;

// =============================================================================
// Function declarations
// =============================================================================

static void setupUsbKeyboard();
static void selectActiveLayout();
static bool loadFlashLayout();
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
    selectActiveLayout();

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

static void selectActiveLayout() {
    if (loadFlashLayout()) {
        return;
    }

    activeMxKeys = DEFAULT_MX_KEYS;
    activeMxKeyCount = DEFAULT_MX_KEY_COUNT;
}

static bool loadFlashLayout() {
    // Placeholder for a future, explicitly defined Flash layout format.
    //
    // When the stored layout format is known, this function should:
    //   1. Read the layout data from Flash.
    //   2. Validate its magic/version/checksum/key count.
    //   3. Reject layouts with more than MX_KEY_CAPACITY entries.
    //   4. Point activeMxKeys/activeMxKeyCount at the validated layout.
    //
    // Returning false keeps the firmware on the hardcoded fallback layout.
    return false;
}

static void setupMxPins() {
    for (uint8_t i = 0; i < activeMxKeyCount; i++) {
        pinMode(activeMxKeys[i].gpio, INPUT_PULLUP);
    }
}

// =============================================================================
// MX scanning
// =============================================================================

static void scanMxKeys() {
    const uint32_t now = millis();
    uint32_t newPressedMask = pressedMask;

    for (uint8_t i = 0; i < activeMxKeyCount; i++) {
        const bool rawPressed = (digitalRead(activeMxKeys[i].gpio) == LOW);
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

    for (uint8_t i = 0; i < activeMxKeyCount && count < 6; i++) {
        if ((pressedMask & (1u << i)) == 0) {
            continue;
        }

        if (activeMxKeys[i].hidKeycode == 0) {
            continue;
        }

        modifier |= activeMxKeys[i].modifiers;
        keycodes[count++] = activeMxKeys[i].hidKeycode;
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

// XPAD-NEO
// Single-file MX-only Arduino keyboard sketch.
//
// What it does:
//   1. Read MX mechanical switches from GPIO pins.
//   2. Debounce the switch input in software.
//   3. Send normal USB HID keyboard reports.
//   4. Expose a minimal WebUSB vendor interface for Layout Generator connection.
//
// What it intentionally does not do:
//   - LittleFS config storage
//   - magnetic / ADC keys
//   - encoder, microphone, vibration, or macros
//   - full XPAD protocol emulation

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
// This table is the firmware's built-in preset. It is intentionally placed near
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

// Keep the state arrays fixed-size so the sketch stays simple and predictable
// for Arduino beginners. A future Flash layout must fit within this capacity.
static const uint8_t MX_KEY_CAPACITY = DEFAULT_MX_KEY_COUNT;

// Layout Generator stores the visual matrix as 8 rows x 10 columns. XPAD-NEO
// only needs a tiny MX test preset today, but using the same matrix size keeps
// the WebUSB packets compatible with the online tool.
static const uint8_t WEB_LAYOUT_MAX_ROWS = 8;
static const uint8_t WEB_LAYOUT_MAX_COLS = 10;
static const uint8_t WEB_LAYOUT_MATRIX_SIZE =
    WEB_LAYOUT_MAX_ROWS * WEB_LAYOUT_MAX_COLS;
static const uint8_t WEB_RX_BUFFER_SIZE = 96;

// A preset is the single source of truth for both Layout Generator and HID.
//
//   rows / cols / layoutMatrix:
//     The visual matrix returned to xtiaconfiger. Each matrix cell stores the
//     GPIO number assigned to that position, or 0xFF for an empty cell.
//
//   keymap / keymapCount:
//     The actual key functions. HID scanning is generated from this list, so the
//     browser-visible preset and the real USB keyboard output cannot drift apart.
struct Preset {
    uint8_t rows;
    uint8_t cols;
    uint8_t layoutMatrix[WEB_LAYOUT_MATRIX_SIZE];
    MxKey keymap[MX_KEY_CAPACITY];
    uint8_t keymapCount;
};

static Preset defaultPreset = {};
static Preset activePreset = {};

static uint8_t webRxBuffer[WEB_RX_BUFFER_SIZE] = {};
static uint8_t webRxCount = 0;

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
// Minimal WebUSB setup
// =============================================================================

// xtiaconfiger currently expects an XPAD-like composite USB device:
//   Interface 0: HID keyboard
//   Interface 1: WebUSB/vendor interface
//
// Register HID before WebUSB so TinyUSB allocates the interfaces in that order.
// Enable the HID OUT endpoint so HID consumes endpoint 1 in both directions;
// WebUSB then receives endpoint 2 IN/OUT, matching xtiaconfiger's hardcoded
// transferIn(2) / transferOut(2) expectation more closely.
// This layer is intentionally small: it makes the browser connection possible
// but does not restore magnetic keys, encoders, microphones, vibration, macros,
// or the complete XPAD runtime.
static Adafruit_USBD_WebUSB usbWeb;

// Landing page descriptor used by WebUSB-capable browsers. Chrome currently
// hides automatic landing page notifications, but the descriptor is still part
// of the normal WebUSB setup.
WEBUSB_URL_DEF(webUsbLandingPage, 1 /* https */, "xtiaconfiger.com");

// =============================================================================
// MX debounce state
// =============================================================================

struct DebounceState {
    bool rawPressed;
    bool debouncedPressed;
    uint32_t lastChangeMs;
};

enum WebUsbPacketState {
    WEB_PACKET_WAIT,
    WEB_PACKET_READY,
    WEB_PACKET_INVALID,
};

static DebounceState keyState[MX_KEY_CAPACITY] = {};
static uint32_t pressedMask = 0;

// =============================================================================
// Function declarations
// =============================================================================

static void setupUsbKeyboard();
static void setupWebUsb();
static void rebuildUsbConfiguration();
static void buildDefaultPreset();
static void handleWebUsb();
static void appendWebUsbData(const uint8_t* buffer, int count);
static bool processNextWebUsbPacket();
static WebUsbPacketState inspectWebUsbPacket(uint8_t* expectedLength);
static void consumeWebUsbBytes(uint8_t count);
static void handleWebUsbPacket(const uint8_t* buffer, uint8_t count);
static void sendWebUsbResponse(const uint8_t* buffer, uint16_t count);
static void sendLayoutResponse();
static void sendKeymapResponse();
static void sendNoMacroResponse();
static void sendNoEncoderResponse();
static void sendInputStateResponse();
static void sendAck(uint8_t command, uint8_t status = 0x00);
static void storeLayoutPacket(const uint8_t* buffer, uint8_t count);
static void storeKeymapPacket(const uint8_t* buffer, uint8_t count);
static void applyActivePreset();
static void resetScanState();
static void selectActivePreset();
static bool loadFlashPreset();
static void scanMxKeys();
static void sendKeyboardReport();
static bool sameKeys(const uint8_t a[6], const uint8_t b[6]);

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
    buildDefaultPreset();
    selectActivePreset();

    while (!TinyUSBDevice.mounted()) {
        delay(1);
    }
}

void loop() {
    scanMxKeys();
    sendKeyboardReport();
    handleWebUsb();
}

// =============================================================================
// Setup helpers
// =============================================================================

static void setupUsbKeyboard() {
    usbHid.setPollInterval(1);
    usbHid.enableOutEndpoint(true);
    usbHid.setReportDescriptor(HID_REPORT_DESC, sizeof(HID_REPORT_DESC));
    usbHid.begin();
}

static void setupWebUsb() {
    usbWeb.setLandingPage(&webUsbLandingPage);
    usbWeb.begin();
}

static void rebuildUsbConfiguration() {
    // arduino-pico initializes TinyUSB before setup() and Adafruit's device
    // begin() adds a CDC Serial interface by default. xtiaconfiger expects
    // interface 1 to be the vendor/WebUSB interface, but the default CDC data
    // interface also lands at interface 1 and cannot be claimed as WebUSB.
    //
    // Clear the default descriptor and rebuild only the interfaces XPAD-NEO
    // needs: HID keyboard first, WebUSB vendor second.
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.clearConfiguration();
}

static void buildDefaultPreset() {
    memset(&defaultPreset, 0, sizeof(defaultPreset));
    memset(defaultPreset.layoutMatrix, 0xFF, sizeof(defaultPreset.layoutMatrix));

    for (uint8_t i = 0; i < DEFAULT_MX_KEY_COUNT; i++) {
        defaultPreset.layoutMatrix[i] = DEFAULT_MX_KEYS[i].gpio;
        defaultPreset.keymap[i].gpio = DEFAULT_MX_KEYS[i].gpio;
        defaultPreset.keymap[i].hidKeycode = DEFAULT_MX_KEYS[i].hidKeycode;
        defaultPreset.keymap[i].modifiers = DEFAULT_MX_KEYS[i].modifiers;
    }

    defaultPreset.rows = 1;
    defaultPreset.cols = DEFAULT_MX_KEY_COUNT;
    defaultPreset.keymapCount = DEFAULT_MX_KEY_COUNT;
}

static void handleWebUsb() {
    uint8_t buffer[64];

    while (usbWeb.available()) {
        const int count = usbWeb.read(buffer, sizeof(buffer));

        if (count <= 0) {
            return;
        }

        appendWebUsbData(buffer, count);

        while (processNextWebUsbPacket()) {
        }
    }
}

static void appendWebUsbData(const uint8_t* buffer, int count) {
    if (count <= 0) {
        return;
    }

    const uint8_t freeSpace = WEB_RX_BUFFER_SIZE - webRxCount;
    const uint8_t copyCount = (count > freeSpace) ? freeSpace : count;
    memcpy(webRxBuffer + webRxCount, buffer, copyCount);
    webRxCount += copyCount;

    if (copyCount < count) {
        webRxCount = 0;
    }
}

static bool processNextWebUsbPacket() {
    uint8_t expected = 0;
    const WebUsbPacketState packetState = inspectWebUsbPacket(&expected);

    if (packetState == WEB_PACKET_WAIT) {
        return false;
    }

    if (packetState == WEB_PACKET_INVALID) {
        consumeWebUsbBytes(1);
        return webRxCount > 0;
    }

    handleWebUsbPacket(webRxBuffer, expected);
    consumeWebUsbBytes(expected);
    return true;
}

static WebUsbPacketState inspectWebUsbPacket(uint8_t* expectedLength) {
    if (webRxCount == 0) {
        return WEB_PACKET_WAIT;
    }

    uint16_t expected = 0;

    switch (webRxBuffer[0]) {
        case 0x30: // Write key mappings: command + count + count * 3.
            if (webRxCount < 2) {
                return WEB_PACKET_WAIT;
            }
            expected = 2 + webRxBuffer[1] * 3;
            break;

        case 0x31: // Write layout: command + rows + cols + 8x10 matrix.
            expected = 3 + WEB_LAYOUT_MATRIX_SIZE;
            break;

        case 0x35: // Write one macro slot: command + gpio + length + text.
            if (webRxCount < 3) {
                return WEB_PACKET_WAIT;
            }
            expected = 3 + webRxBuffer[2];
            break;

        case 0x36: // Read one macro slot: command + slot index.
            expected = 2;
            break;

        case 0x37: // Write encoder config.
            expected = 10;
            break;

        case 0x39: // Rumble test command from the advanced panel.
            expected = 5;
            break;

        default:
            expected = 1;
            break;
    }

    if (expected > WEB_RX_BUFFER_SIZE) {
        return WEB_PACKET_INVALID;
    }

    if (webRxCount < expected) {
        return WEB_PACKET_WAIT;
    }

    *expectedLength = expected;
    return WEB_PACKET_READY;
}

static void consumeWebUsbBytes(uint8_t count) {
    if (count >= webRxCount) {
        webRxCount = 0;
        return;
    }

    memmove(webRxBuffer, webRxBuffer + count, webRxCount - count);
    webRxCount -= count;
}

static void handleWebUsbPacket(const uint8_t* buffer, uint8_t count) {
    if (count <= 0) {
        return;
    }

    switch (buffer[0]) {
        case 0x30: // Write key mappings.
            storeKeymapPacket(buffer, count);
            sendAck(0x30);
            break;

        case 0x31: // Write layout.
            storeLayoutPacket(buffer, count);
            sendAck(0x31);
            break;

        case 0x32: // Read layout.
            sendLayoutResponse();
            break;

        case 0x33: // Read key mappings.
            sendKeymapResponse();
            break;

        case 0x34: // Read input state for Layout Generator test mode.
            sendInputStateResponse();
            break;

        case 0x35: // Write macro text. Macros are intentionally unsupported.
            sendAck(0x35);
            break;

        case 0x36: // Read macro slot. Always report an empty slot.
            sendNoMacroResponse();
            break;

        case 0x37: // Write encoder config. Encoder is intentionally unsupported.
            sendAck(0x37);
            break;

        case 0x38: // Read encoder config. Always report no encoder mappings.
            sendNoEncoderResponse();
            break;

        case 0x39: // Rumble test. Vibration is intentionally unsupported.
            sendAck(0x39);
            break;

        default:
            // Unknown or intentionally unsupported XPAD command. Do not send a
            // guessed response; later compatibility nodes should add explicit
            // handlers only for commands that xtiaconfiger actually needs.
            break;
    }
}

static void sendWebUsbResponse(const uint8_t* buffer, uint16_t count) {
    // Adafruit_USBD_WebUSB::write() only writes after a WebSerial line-state
    // request. xtiaconfiger uses raw WebUSB transferIn/transferOut, so write
    // directly through TinyUSB's vendor endpoint.
    uint16_t sent = 0;

    while (sent < count) {
        const uint32_t written = tud_vendor_write(buffer + sent, count - sent);

        if (written > 0) {
            sent += written;
            tud_vendor_flush();
            continue;
        }

        yield();
    }
}

static void sendLayoutResponse() {
    uint8_t response[4 + WEB_LAYOUT_MATRIX_SIZE] = {
        0x32, // command echo
        0x00, // status
        activePreset.rows,
        activePreset.cols,
    };

    memcpy(response + 4, activePreset.layoutMatrix, sizeof(activePreset.layoutMatrix));
    sendWebUsbResponse(response, sizeof(response));
}

static void sendKeymapResponse() {
    uint8_t response[3 + MX_KEY_CAPACITY * 4] = {};
    response[0] = 0x33;
    response[1] = 0x00;
    response[2] = activePreset.keymapCount;

    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY; i++) {
        const uint8_t offset = 3 + i * 4;
        response[offset] = activePreset.keymap[i].gpio;
        response[offset + 1] = activePreset.keymap[i].hidKeycode;
        response[offset + 2] = activePreset.keymap[i].modifiers;
        response[offset + 3] = 0x00;
    }

    sendWebUsbResponse(response, 3 + activePreset.keymapCount * 4);
}

static void sendNoMacroResponse() {
    uint8_t response[4] = {
        0x36, // command echo
        0x00, // status
        0xFF, // gpio = none
        0x00, // text length = 0
    };

    sendWebUsbResponse(response, sizeof(response));
}

static void sendNoEncoderResponse() {
    uint8_t response[11] = {
        0x38, 0x00, // command echo, status
        0x00, 0x00, 0x00, // ccw disabled
        0x00, 0x00, 0x00, // cw disabled
        0x00, 0x00, 0x00, // switch disabled
    };

    sendWebUsbResponse(response, sizeof(response));
}

static void sendInputStateResponse() {
    uint8_t response[13] = {
        0x34, 0x00, // command echo, status
    };
    uint32_t gpioMask = 0;

    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY; i++) {
        const MxKey& key = activePreset.keymap[i];

        if ((pressedMask & (1u << i)) != 0 && key.gpio < 32) {
            gpioMask |= (1u << key.gpio);
        }
    }

    response[2] = gpioMask & 0xFF;
    response[3] = (gpioMask >> 8) & 0xFF;
    response[4] = (gpioMask >> 16) & 0xFF;
    response[5] = (gpioMask >> 24) & 0xFF;
    sendWebUsbResponse(response, sizeof(response));
}

static void sendAck(uint8_t command, uint8_t status) {
    uint8_t response[2] = { command, status };
    sendWebUsbResponse(response, sizeof(response));
}

static void storeLayoutPacket(const uint8_t* buffer, uint8_t count) {
    if (count < (3 + WEB_LAYOUT_MATRIX_SIZE)) {
        return;
    }

    activePreset.rows = buffer[1];
    activePreset.cols = buffer[2];

    if (activePreset.rows > WEB_LAYOUT_MAX_ROWS) {
        activePreset.rows = WEB_LAYOUT_MAX_ROWS;
    }

    if (activePreset.cols > WEB_LAYOUT_MAX_COLS) {
        activePreset.cols = WEB_LAYOUT_MAX_COLS;
    }

    memcpy(activePreset.layoutMatrix, buffer + 3, sizeof(activePreset.layoutMatrix));
}

static void storeKeymapPacket(const uint8_t* buffer, uint8_t count) {
    if (count < 2) {
        return;
    }

    const uint8_t incomingCount = buffer[1];
    uint8_t storedCount = 0;

    for (uint8_t i = 0; i < incomingCount && storedCount < MX_KEY_CAPACITY; i++) {
        const uint8_t offset = 2 + i * 3;

        if ((offset + 2) >= count) {
            break;
        }

        activePreset.keymap[storedCount].gpio = buffer[offset];
        activePreset.keymap[storedCount].hidKeycode = buffer[offset + 1];
        activePreset.keymap[storedCount].modifiers = buffer[offset + 2];
        storedCount++;
    }

    activePreset.keymapCount = storedCount;
    applyActivePreset();
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
}

static void selectActivePreset() {
    if (loadFlashPreset()) {
        applyActivePreset();
        return;
    }

    activePreset = defaultPreset;
    applyActivePreset();
}

static bool loadFlashPreset() {
    // Placeholder for a future, explicitly defined Flash preset format.
    //
    // When the stored preset format is known, this function should:
    //   1. Read the preset data from Flash.
    //   2. Validate its magic/version/checksum/key count.
    //   3. Reject presets with more than MX_KEY_CAPACITY entries.
    //   4. Copy the validated preset into activePreset.
    //
    // Returning false keeps the firmware on the hardcoded fallback preset.
    return false;
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

    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY && count < 6; i++) {
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

// XPAD-NEO Layout Generator compatibility layer.
//
// This Arduino tab contains the advanced WebUSB protocol and Flash preset
// storage. The beginner-facing MX scan and HID flow remains in xpad_neo.ino.

#include <EEPROM.h>

// RP2040 exposes GPIO 0 through GPIO 29. Layout Generator data is external
// input, so validate every GPIO before passing it to Arduino pin functions.
static const uint8_t RP2040_GPIO_COUNT = 30;
static const uint8_t WEB_RX_BUFFER_SIZE = 96;
static const uint16_t WEB_WRITE_STALL_TIMEOUT_MS = 50;

enum WebUsbCommand : uint8_t {
    WEB_CMD_WRITE_KEYMAP = 0x30,
    WEB_CMD_WRITE_LAYOUT = 0x31,
    WEB_CMD_READ_LAYOUT = 0x32,
    WEB_CMD_READ_KEYMAP = 0x33,
    WEB_CMD_READ_INPUT = 0x34,
    WEB_CMD_WRITE_MACRO = 0x35,
    WEB_CMD_READ_MACRO = 0x36,
    WEB_CMD_WRITE_ENCODER = 0x37,
    WEB_CMD_READ_ENCODER = 0x38,
    WEB_CMD_TEST_RUMBLE = 0x39,
};

enum WebUsbStatus : uint8_t {
    WEB_STATUS_OK = 0x00,
    WEB_STATUS_INVALID_DATA = 0x01,
};

enum WebUsbPacketState {
    WEB_PACKET_WAIT,
    WEB_PACKET_READY,
    WEB_PACKET_INVALID,
};

// EEPROM on arduino-pico stores this small record in RP2040 Flash. Magic,
// version, size, and checksum let future firmware reject incompatible data.
static const uint32_t FLASH_PRESET_MAGIC = 0x4E454F31; // "NEO1"
static const uint16_t FLASH_PRESET_VERSION = 1;
static const uint16_t FLASH_STORAGE_SIZE = 256;

struct StoredPreset {
    uint32_t magic;
    uint16_t version;
    uint16_t presetSize;
    Preset preset;
    uint32_t checksum;
};

static_assert(sizeof(StoredPreset) <= FLASH_STORAGE_SIZE,
              "Stored preset exceeds the reserved Flash area.");

static bool presetStorageReady = false;
static uint8_t webRxBuffer[WEB_RX_BUFFER_SIZE] = {};
static uint8_t webRxCount = 0;

// xtiaconfiger expects HID interface 0, WebUSB interface 1, and endpoint 2 in
// both directions. HID is registered first in xpad_neo.ino to preserve this.
static Adafruit_USBD_WebUSB usbWeb;
WEBUSB_URL_DEF(webUsbLandingPage, 1 /* https */, "xtiaconfiger.com");

static void appendWebUsbData(const uint8_t* buffer, int count);
static bool processNextWebUsbPacket();
static WebUsbPacketState inspectWebUsbPacket(uint8_t* expectedLength);
static void consumeWebUsbBytes(uint8_t count);
static void handleWebUsbPacket(const uint8_t* buffer, uint8_t count);
static bool sendWebUsbResponse(const uint8_t* buffer, uint16_t count);
static void sendLayoutResponse();
static void sendKeymapResponse();
static void sendNoMacroResponse();
static void sendNoEncoderResponse();
static void sendInputStateResponse();
static void sendAck(uint8_t command, uint8_t status = WEB_STATUS_OK);
static bool storeLayoutPacket(const uint8_t* buffer, uint8_t count);
static bool storeKeymapPacket(const uint8_t* buffer, uint8_t count);
static bool isValidMxGpio(uint8_t gpio);
static bool isValidLayout(const Preset& preset);
static bool isValidKeymap(const Preset& preset);
static uint32_t calculatePresetChecksum(const Preset& preset);
static bool savePresetToFlash(const Preset& preset);
static bool loadFlashPreset();

// =============================================================================
// USB and Flash setup
// =============================================================================

static void rebuildUsbConfiguration() {
    // arduino-pico adds CDC interfaces before setup(). Clear that default and
    // rebuild only HID keyboard first and WebUSB vendor second.
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.clearConfiguration();
}

static void setupWebUsb() {
    usbWeb.setLandingPage(&webUsbLandingPage);
    usbWeb.begin();
}

static void setupPresetStorage() {
    EEPROM.begin(FLASH_STORAGE_SIZE);
    presetStorageReady = (EEPROM.length() >= sizeof(StoredPreset));
}

static void selectActivePreset() {
    if (!loadFlashPreset()) {
        activePreset = defaultPreset;
    }

    applyActivePreset();
}

// =============================================================================
// WebUSB receive and packet parsing
// =============================================================================

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
        // There is no packet delimiter to recover from an impossible length.
        // Clear the current batch instead of treating payload bytes as commands.
        webRxCount = 0;
        return false;
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
        case WEB_CMD_WRITE_KEYMAP: // Command + count + count * 3.
            if (webRxCount < 2) {
                return WEB_PACKET_WAIT;
            }
            expected = 2 + webRxBuffer[1] * 3;
            break;

        case WEB_CMD_WRITE_LAYOUT: // Command + rows + cols + 8x10 matrix.
            expected = 3 + WEB_LAYOUT_MATRIX_SIZE;
            break;

        case WEB_CMD_WRITE_MACRO: // Command + gpio + length + text.
            if (webRxCount < 3) {
                return WEB_PACKET_WAIT;
            }
            expected = 3 + webRxBuffer[2];
            break;

        case WEB_CMD_READ_MACRO: // Command + slot index.
            expected = 2;
            break;

        case WEB_CMD_WRITE_ENCODER:
            expected = 10;
            break;

        case WEB_CMD_TEST_RUMBLE:
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

// =============================================================================
// Layout Generator commands
// =============================================================================

static void handleWebUsbPacket(const uint8_t* buffer, uint8_t count) {
    if (count == 0) {
        return;
    }

    switch (buffer[0]) {
        case WEB_CMD_WRITE_KEYMAP:
            sendAck(WEB_CMD_WRITE_KEYMAP,
                    storeKeymapPacket(buffer, count)
                        ? WEB_STATUS_OK
                        : WEB_STATUS_INVALID_DATA);
            break;

        case WEB_CMD_WRITE_LAYOUT:
            sendAck(WEB_CMD_WRITE_LAYOUT,
                    storeLayoutPacket(buffer, count)
                        ? WEB_STATUS_OK
                        : WEB_STATUS_INVALID_DATA);
            break;

        case WEB_CMD_READ_LAYOUT:
            sendLayoutResponse();
            break;

        case WEB_CMD_READ_KEYMAP:
            sendKeymapResponse();
            break;

        case WEB_CMD_READ_INPUT:
            sendInputStateResponse();
            break;

        // Layout Generator probes these XPAD capabilities. Return empty values
        // or acknowledgements without adding the unsupported hardware logic.
        case WEB_CMD_WRITE_MACRO:
            sendAck(WEB_CMD_WRITE_MACRO);
            break;

        case WEB_CMD_READ_MACRO:
            sendNoMacroResponse();
            break;

        case WEB_CMD_WRITE_ENCODER:
            sendAck(WEB_CMD_WRITE_ENCODER);
            break;

        case WEB_CMD_READ_ENCODER:
            sendNoEncoderResponse();
            break;

        case WEB_CMD_TEST_RUMBLE:
            sendAck(WEB_CMD_TEST_RUMBLE);
            break;

        default:
            break;
    }
}

static bool sendWebUsbResponse(const uint8_t* buffer, uint16_t count) {
    // Raw WebUSB does not set Adafruit WebSerial line state, so write directly
    // through TinyUSB's vendor endpoint. Stop if the endpoint stalls.
    uint16_t sent = 0;
    uint32_t lastProgressMs = millis();

    while (sent < count) {
        if (!TinyUSBDevice.mounted() ||
            (millis() - lastProgressMs) >= WEB_WRITE_STALL_TIMEOUT_MS) {
            return false;
        }

        const uint32_t written = tud_vendor_write(buffer + sent, count - sent);

        if (written > 0) {
            sent += written;
            lastProgressMs = millis();
            tud_vendor_flush();
            continue;
        }

        yield();
    }

    return true;
}

static void sendLayoutResponse() {
    uint8_t response[4 + WEB_LAYOUT_MATRIX_SIZE] = {
        WEB_CMD_READ_LAYOUT,
        WEB_STATUS_OK,
        activePreset.rows,
        activePreset.cols,
    };

    memcpy(response + 4, activePreset.layoutMatrix, sizeof(activePreset.layoutMatrix));
    sendWebUsbResponse(response, sizeof(response));
}

static void sendKeymapResponse() {
    uint8_t response[3 + MX_KEY_CAPACITY * 4] = {};
    response[0] = WEB_CMD_READ_KEYMAP;
    response[1] = WEB_STATUS_OK;
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
        WEB_CMD_READ_MACRO,
        WEB_STATUS_OK,
        0xFF, // gpio = none
        0x00, // text length = 0
    };

    sendWebUsbResponse(response, sizeof(response));
}

static void sendNoEncoderResponse() {
    uint8_t response[11] = {
        WEB_CMD_READ_ENCODER, WEB_STATUS_OK,
        0x00, 0x00, 0x00, // ccw disabled
        0x00, 0x00, 0x00, // cw disabled
        0x00, 0x00, 0x00, // switch disabled
    };

    sendWebUsbResponse(response, sizeof(response));
}

static void sendInputStateResponse() {
    uint8_t response[13] = {
        WEB_CMD_READ_INPUT, WEB_STATUS_OK,
    };
    uint32_t gpioMask = 0;

    for (uint8_t i = 0; i < activePreset.keymapCount && i < MX_KEY_CAPACITY; i++) {
        const MxKey& key = activePreset.keymap[i];

        if ((pressedMask & (1u << i)) != 0 && isValidMxGpio(key.gpio)) {
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

// =============================================================================
// Preset validation and Flash storage
// =============================================================================

static bool storeLayoutPacket(const uint8_t* buffer, uint8_t count) {
    if (count != (3 + WEB_LAYOUT_MATRIX_SIZE)) {
        return false;
    }

    const uint8_t rows = buffer[1];
    const uint8_t cols = buffer[2];

    if (rows == 0 || rows > WEB_LAYOUT_MAX_ROWS ||
        cols == 0 || cols > WEB_LAYOUT_MAX_COLS) {
        return false;
    }

    Preset candidate = activePreset;
    candidate.rows = rows;
    candidate.cols = cols;
    memcpy(candidate.layoutMatrix, buffer + 3, sizeof(candidate.layoutMatrix));

    if (!savePresetToFlash(candidate)) {
        return false;
    }

    activePreset = candidate;
    return true;
}

// Layout matrix is the visual arrangement used by Layout Generator.
// Keymap is the HID scan/output source of truth.
//
// Layout Generator normally keeps these two views in sync and writes them as
// part of the same user operation, even though the protocol sends them as
// separate commands. XPAD-NEO stores each command as it arrives so a normal
// read/edit/write flow remains simple and compatible.
//
// Cross-validation is intentionally not enforced here. A mismatch would only
// happen after partial writes, interrupted USB sessions, or custom tools that
// update layout/keymap independently. In that case, keymap remains the runtime
// source of truth.
static bool storeKeymapPacket(const uint8_t* buffer, uint8_t count) {
    if (count < 2) {
        return false;
    }

    const uint8_t incomingCount = buffer[1];
    const uint16_t expectedCount = 2 + incomingCount * 3;

    if (incomingCount > MX_KEY_CAPACITY || count != expectedCount) {
        return false;
    }

    Preset candidate = activePreset;
    memset(candidate.keymap, 0, sizeof(candidate.keymap));
    candidate.keymapCount = incomingCount;

    for (uint8_t i = 0; i < incomingCount; i++) {
        const uint8_t offset = 2 + i * 3;
        candidate.keymap[i].gpio = buffer[offset];
        candidate.keymap[i].hidKeycode = buffer[offset + 1];
        candidate.keymap[i].modifiers = buffer[offset + 2];
    }

    if (!savePresetToFlash(candidate)) {
        return false;
    }

    activePreset = candidate;
    applyActivePreset();
    return true;
}

static bool isValidMxGpio(uint8_t gpio) {
    // This firmware intentionally accepts any RP2040 GPIO number. XPAD-NEO is
    // an entry-level "up to eight MX keys" project, so users can rewire keys,
    // try smaller builds, or adapt the sketch to future board variants without
    // changing the validation code first.
    return gpio < RP2040_GPIO_COUNT;
}

static bool isValidLayout(const Preset& preset) {
    if (preset.rows == 0 || preset.rows > WEB_LAYOUT_MAX_ROWS ||
        preset.cols == 0 || preset.cols > WEB_LAYOUT_MAX_COLS) {
        return false;
    }

    bool gpioSeen[RP2040_GPIO_COUNT] = {};
    uint8_t keyCount = 0;

    for (uint8_t i = 0; i < WEB_LAYOUT_MATRIX_SIZE; i++) {
        const uint8_t gpio = preset.layoutMatrix[i];

        if (gpio == 0xFF) {
            continue;
        }

        if (!isValidMxGpio(gpio) || gpioSeen[gpio] ||
            keyCount >= MX_KEY_CAPACITY) {
            return false;
        }

        gpioSeen[gpio] = true;
        keyCount++;
    }

    return true;
}

static bool isValidKeymap(const Preset& preset) {
    if (preset.keymapCount > MX_KEY_CAPACITY) {
        return false;
    }

    bool gpioSeen[RP2040_GPIO_COUNT] = {};

    for (uint8_t i = 0; i < preset.keymapCount; i++) {
        const uint8_t gpio = preset.keymap[i].gpio;

        if (!isValidMxGpio(gpio) || gpioSeen[gpio]) {
            return false;
        }

        gpioSeen[gpio] = true;
    }

    return true;
}

static uint32_t calculatePresetChecksum(const Preset& preset) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&preset);
    uint32_t checksum = 2166136261u;

    for (size_t i = 0; i < sizeof(Preset); i++) {
        checksum ^= bytes[i];
        checksum *= 16777619u;
    }

    return checksum;
}

static bool savePresetToFlash(const Preset& preset) {
    if (!presetStorageReady ||
        !isValidLayout(preset) || !isValidKeymap(preset)) {
        return false;
    }

    StoredPreset stored = {};
    stored.magic = FLASH_PRESET_MAGIC;
    stored.version = FLASH_PRESET_VERSION;
    stored.presetSize = sizeof(Preset);
    stored.preset = preset;
    stored.checksum = calculatePresetChecksum(stored.preset);

    EEPROM.put(0, stored);
    return EEPROM.commit();
}

static bool loadFlashPreset() {
    if (!presetStorageReady) {
        return false;
    }

    StoredPreset stored = {};
    EEPROM.get(0, stored);

    if (stored.magic != FLASH_PRESET_MAGIC ||
        stored.version != FLASH_PRESET_VERSION ||
        stored.presetSize != sizeof(Preset) ||
        stored.checksum != calculatePresetChecksum(stored.preset) ||
        !isValidLayout(stored.preset) ||
        !isValidKeymap(stored.preset)) {
        return false;
    }

    activePreset = stored.preset;
    return true;
}

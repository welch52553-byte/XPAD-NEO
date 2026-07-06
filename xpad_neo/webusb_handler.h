#pragma once
#include <stdint.h>

// =============================================================================
// webusb_handler.h — USB vendor/config protocol handler
//
// The XTIA web UI communicates with the device over a USB Vendor class
// endpoint (bulk transfer, 64 bytes max per packet). The same protocol is used
// by the XPAD firmware so the web UI works with both without modification.
//
// Command byte overview (first byte of every host→device packet):
//   0x01  CMD_READ_STATUS       Stream live ADC values + calibration params
//   0x12  CMD_CAPTURE_ACTIVE    Sample one ADC channel at full-press
//   0x20  CMD_WRITE_PARAMS      Save new calibration + RT settings to flash
//   0x30  CMD_WRITE_KEYMAP      Save new key→HID mappings to flash
//   0x31  CMD_WRITE_LAYOUT      Save visual matrix layout to flash
//   0x32  CMD_READ_LAYOUT       Return stored visual layout
//   0x33  CMD_READ_KEYS         Return all key→HID mappings
//   0x34  CMD_READ_INPUT        Return current raw GPIO + ADC pressed state
//
// To add a new command (e.g. for encoder, macro, rumble):
//   1. Define CMD_xxx in webusb_handler.cpp
//   2. Write a handler function  void handle_xxx(const uint8_t*, uint8_t)
//   3. Add one line to kHandlers[] — nothing else needs to change.
// =============================================================================

// Initialise the TinyUSB vendor interface. Call in setup().
void webusb_handler_setup();

// Poll for incoming packets and dispatch to the appropriate handler.
// Call every loop iteration.
void webusb_handler_task();

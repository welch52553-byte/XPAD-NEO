#pragma once
#include <stdint.h>

// =============================================================================
// hid_keyboard.h — USB HID keyboard report generation
//
// HID (Human Interface Device) is a USB protocol class that lets devices
// present themselves as keyboards, mice, gamepads, etc. without custom drivers.
//
// We use two HID report types:
//   Report ID 1 — Standard keyboard: up to 6 simultaneous keycodes + modifiers
//   Report ID 2 — Consumer Control:  media keys (play/pause, volume, etc.)
//                 Reserved for FEATURE_MACRO / future use.
//
// The report descriptor below tells the host OS exactly what format each
// report uses. It must match the byte layout of the structs we send.
// =============================================================================

// Initialise the TinyUSB HID device. Call in setup() after USB stack starts.
void hid_keyboard_setup();

// Build and send a keyboard report from the current pressed key masks.
// Call this every loop iteration after scanning MX and ADC keys.
// Internally rate-limits to the USB polling interval (1 ms by default).
void hid_keyboard_task();

// HID report descriptor — exported so usb_setup can embed it in the
// USB configuration descriptor.
extern const uint8_t k_hid_report_descriptor[];
extern const uint16_t k_hid_report_descriptor_len;

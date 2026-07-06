#pragma once

// =============================================================================
// flash_storage.h — Persistent configuration via LittleFS
//
// LittleFS is a small filesystem designed for microcontrollers. On RP2040 with
// the arduino-pico core it lives in the unused portion of the 2 MB flash chip.
//
// Why LittleFS instead of raw flash writes?
//   • Wear levelling — LittleFS spreads writes to avoid burning out one sector
//   • Power-loss safe — partial writes do not corrupt the stored config
//   • Simple API — read/write files just like on a regular filesystem
//
// Usage:
//   Call config_load() once in setup() before anything else reads the config.
//   Call config_save() after every USB write command that modifies the config.
// =============================================================================

// Initialises LittleFS and loads config from disk into RAM.
// Falls back to factory defaults if the stored magic does not match.
void config_load();

// Writes the current in-RAM XpadConfig to disk.
void config_save();

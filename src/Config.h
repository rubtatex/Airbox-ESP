#pragma once

// =============================================================================
// Airbox-ESP — hardware configuration and global constants
//
// WARNING: this firmware drives the valves/pump of a breath-control device.
// Read .claude/CLAUDE.md before changing anything here,
// especially the section "Safety model — the heart of the firmware".
// =============================================================================

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Pinout (XIAO ESP32-C3)
// ---------------------------------------------------------------------------
// I2C bus shared by the 1.3" OLED screen (SH1106).
static constexpr uint8_t PIN_I2C_SDA = 6;  // D4
static constexpr uint8_t PIN_I2C_SCL = 7;  // D5

// Relays — pins deliberately chosen outside the strapping pins (GPIO2/8/9).
static constexpr uint8_t PIN_RELAY_AIR = 3;      // D1
static constexpr uint8_t PIN_RELAY_REBREATH = 4; // D2
static constexpr uint8_t PIN_RELAY_AROMA = 5;    // D3

// On-board BOOT button: read as INPUT ONLY (strapping pin, never driven as
// an output). Long press = E-STOP/re-arm, short press = temporary
// network/API key "peek".
static constexpr uint8_t PIN_BOOT_BUTTON = 9;

// ---------------------------------------------------------------------------
// Relays — polarity of the control signal sent to the relay board.
// Assumption: cheap opto-coupled relay modules, triggered on a LOW level
// (active LOW). Must be checked on the real board before final wiring —
// see .claude/CLAUDE.md, section "Hardware & pinout (Seeed XIAO ESP32-C3)".
// ---------------------------------------------------------------------------
static constexpr bool RELAY_ACTIVE_LOW = true;

// ---------------------------------------------------------------------------
// Safety
// ---------------------------------------------------------------------------
// Default maximum durations each valve may stay continuously in its "active"
// state (Re-Breath open = rebreathing in progress; Air closed = no normal air
// available), whatever the command source (REST, BLE). Two INDEPENDENT
// settings (Air closed is more critical and may need a different limit than
// Re-Breath). Configurable at runtime via PUT /api/system/safety-config
// (or BLE cmd "safety.config"), clamped by the MIN/MAX constants so the
// safeguard can never be disabled by mistake.
static constexpr uint32_t REBREATH_CAP_DEFAULT_MS = 120000;  // 120 s
static constexpr uint32_t REBREATH_CAP_MIN_MS = 5000;        // 5 s
static constexpr uint32_t REBREATH_CAP_MAX_MS = 300000;      // 5 min

static constexpr uint32_t AIR_CAP_DEFAULT_MS = 120000;  // 120 s
static constexpr uint32_t AIR_CAP_MIN_MS = 5000;        // 5 s
static constexpr uint32_t AIR_CAP_MAX_MS = 300000;      // 5 min

// Hardware watchdog: if loop() does not feed it within this delay, the ESP32
// reboots (and always comes back DISARMED, see SafetyManager).
static constexpr uint32_t WATCHDOG_TIMEOUT_S = 4;

// BOOT button: minimum press duration for a "long press".
static constexpr uint32_t BUTTON_LONG_PRESS_MS = 1000;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
static constexpr const char* MDNS_HOSTNAME = "airbox";
static constexpr const char* SOFTAP_SSID_PREFIX = "Airbox-";
static constexpr uint16_t HTTP_PORT = 80;

// ---------------------------------------------------------------------------
// BLE — UUIDs of the custom "Airbox Control" service
// ---------------------------------------------------------------------------
static constexpr const char* BLE_SERVICE_UUID          = "9b1a0001-8f1a-4a2b-9c3e-1a2b3c4d5e6f";
static constexpr const char* BLE_CHAR_CMD_UUID          = "9b1a0002-8f1a-4a2b-9c3e-1a2b3c4d5e6f"; // write: JSON commands
static constexpr const char* BLE_CHAR_STATE_UUID        = "9b1a0003-8f1a-4a2b-9c3e-1a2b3c4d5e6f"; // notify: JSON state
static constexpr const char* BLE_CHAR_ESTOP_UUID        = "9b1a0004-8f1a-4a2b-9c3e-1a2b3c4d5e6f"; // write: immediate emergency stop
static constexpr const char* BLE_CHAR_WIFI_CONFIG_UUID  = "9b1a0005-8f1a-4a2b-9c3e-1a2b3c4d5e6f"; // write: WiFi provisioning

// Note: no embedded automations/sequences — they live on the app side
// (phone), which drives the firmware one command at a time.
// See .claude/CLAUDE.md, section "Automation architecture: app-side, not embedded".

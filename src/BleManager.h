#pragma once

// =============================================================================
// BleManager — custom "Airbox Control" BLE service (NimBLE, light on RAM
// so it can coexist with WiFi/web server on the ESP32-C3).
//
// Characteristics:
//  - CMD (write)          : JSON commands {"key","cmd",...} — API key required.
//  - STATE (read/notify)  : full JSON state, notified on every change.
//  - ESTOP (write)        : IMMEDIATE emergency stop, deliberately WITHOUT
//                           API key (the safety brake must never be blocked
//                           by an auth issue — see CLAUDE.md).
//  - WIFI_CONFIG (write)  : WiFi provisioning {"key","ssid","password"}.
//
// RULE: like WebApi, BleManager NEVER calls RelayController
// directly — everything goes through SafetyManager.
// =============================================================================

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ArduinoJson.h>
#include "SafetyManager.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "DisplayManager.h"

class BleManager {
 public:
  void begin(SafetyManager& safety, StorageManager& storage, NetworkManager& network,
             DisplayManager& display);

  // Call on every loop(): pushes a STATE notification if the state has
  // changed since last time (throttled).
  void tick();

  void notifyStateChange();  // forces an immediate notification (e.g. on SafetyEvent)

  // Used by DisplayManager to pick the screen (status vs connection QR).
  bool hasConnectedClient() const { return server_ && server_->getConnectedCount() > 0; }

 private:
  SafetyManager* safety_ = nullptr;
  StorageManager* storage_ = nullptr;
  NetworkManager* network_ = nullptr;
  DisplayManager* display_ = nullptr;

  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* chrCmd_ = nullptr;
  NimBLECharacteristic* chrState_ = nullptr;
  NimBLECharacteristic* chrEstop_ = nullptr;
  NimBLECharacteristic* chrWifiConfig_ = nullptr;

  uint32_t lastNotifyMs_ = 0;
  String lastStateJson_;

  friend class AirboxCmdCallbacks;
  friend class AirboxEstopCallbacks;
  friend class AirboxWifiConfigCallbacks;

  bool checkKey(const JsonDocument& doc) const;
  String buildStateJson() const;
  void handleCmd(const String& raw);
  void handleWifiConfig(const String& raw);
};

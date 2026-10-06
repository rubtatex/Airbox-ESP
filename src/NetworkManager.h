#pragma once

// =============================================================================
// NetworkManager — WiFi connection (credentials in NVS via StorageManager),
// with fallback access point (SoftAP) if there are no credentials or the
// connection fails, + mDNS ("airbox.local").
//
// Nothing here blocks in tick(): the WiFi scan is done asynchronously
// (WiFi.scanNetworks(true)) so loop() never freezes — loop() is what
// feeds the safety watchdog (SafetyManager::tick()).
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <vector>
#include "StorageManager.h"

struct WifiNetwork {
  String ssid;
  int32_t rssi;   // dBm (e.g. -50 = excellent, -90 = very weak)
  bool secure;    // false = open network
};

class NetworkManager {
 public:
  void begin(StorageManager& storage);
  void tick();  // call on every loop(); non-blocking

  bool isConnected() const;
  bool isApMode() const { return apMode_; }
  // STA connection attempt in progress (neither connected, nor yet switched
  // to the fallback AP) — used by DisplayManager for the boot screen.
  bool isConnectingSta() const { return !apMode_ && WiFi.status() != WL_CONNECTED; }
  String getIp() const;         // station IP or AP IP (192.168.4.1)
  String getApSsid() const { return apSsid_; }  // SSID to join to configure WiFi
  String getConnectedSsid() const;  // SSID of the currently connected WiFi ("" if not connected)
  String getStatusSummary() const;

  // Applies new credentials (persisted via StorageManager) and attempts
  // to reconnect. Used by WebApi (/wifi/config) and BleManager
  // (WiFi provisioning over BLE).
  void applyCredentials(const String& ssid, const String& password);

  // Erases the WiFi credentials and switches back to fallback access point
  // mode — to switch back to 100% local use (BLE) without a full
  // factory-reset (which would also erase the API key). Used by
  // WebApi (/wifi/forget) and BleManager (cmd "wifi.forget").
  void forgetWifi();

  // Asynchronous scan. Results are cached (non-destructively) in tick()
  // as soon as they are ready, so that REST and BLE can both read them
  // independently without "stealing" them from each other. `scanGeneration`
  // increments on every completed scan: a REST caller can compare it with
  // the previously seen generation to know whether to request a new one
  // (see WebApi::setupRoutes, route /api/wifi/scan).
  void requestScan();
  bool isScanning() const { return scanRequested_; }
  bool isScanReady() const { return scanReady_; }
  std::vector<WifiNetwork> getLastScanResults() const { return lastScanResults_; }
  uint32_t getScanGeneration() const { return scanGeneration_; }

 private:
  StorageManager* storage_ = nullptr;
  bool apMode_ = false;
  bool scanRequested_ = false;
  bool scanReady_ = false;
  uint32_t scanGeneration_ = 0;
  std::vector<WifiNetwork> lastScanResults_;
  uint32_t connectStartMs_ = 0;
  static constexpr uint32_t kConnectTimeoutMs = 15000;

  DNSServer dnsServer_;
  String apSsid_;

  void startStationMode();
  void startApFallback();
};

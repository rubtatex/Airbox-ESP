#include "NetworkManager.h"
#include "Config.h"
#include <ESPmDNS.h>

void NetworkManager::begin(StorageManager& storage) {
  storage_ = &storage;

  uint64_t chipId = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", (uint32_t)(chipId & 0xFFFFFF));
  apSsid_ = String(SOFTAP_SSID_PREFIX) + suffix;

  if (storage_->hasWifiCredentials()) {
    startStationMode();
  } else {
    startApFallback();
  }
}

void NetworkManager::startStationMode() {
  apMode_ = false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.begin(storage_->getWifiSsid().c_str(), storage_->getWifiPassword().c_str());
  connectStartMs_ = millis();
}

void NetworkManager::startApFallback() {
  apMode_ = true;
  // AP_STA (not just AP): allows scanning surrounding WiFi (to find the
  // SSID to configure) while broadcasting the fallback access point
  // at the same time.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid_.c_str());
  IPAddress apIp = WiFi.softAPIP();
  dnsServer_.start(53, "*", apIp);
  MDNS.begin(MDNS_HOSTNAME);
}

void NetworkManager::tick() {
  // WiFi scan: handled first, regardless of AP/STA mode, so that it works
  // both during setup (AP mode) and once connected to the normal WiFi.
  if (scanRequested_ && WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
    int16_t n = WiFi.scanComplete();
    lastScanResults_.clear();
    if (n > 0) {
      for (int16_t i = 0; i < n; ++i) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;  // hidden networks (empty SSID) skipped
        int32_t rssi = WiFi.RSSI(i);
        bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;

        // The same SSID often appears several times (several access points):
        // keep only the strongest signal.
        bool merged = false;
        for (auto& existing : lastScanResults_) {
          if (existing.ssid == ssid) {
            if (rssi > existing.rssi) existing.rssi = rssi;
            merged = true;
            break;
          }
        }
        if (!merged) lastScanResults_.push_back({ssid, rssi, secure});
      }
      WiFi.scanDelete();
    }
    scanRequested_ = false;
    scanReady_ = true;
    scanGeneration_++;
  }

  if (apMode_) {
    dnsServer_.processNextRequest();
    return;
  }

  // In station mode: if the connection still hasn't succeeded after the
  // timeout, switch to the fallback AP rather than staying stuck
  // indefinitely with no way to administer the device.
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - connectStartMs_ > kConnectTimeoutMs) {
      startApFallback();
    }
    return;
  }

  static bool mdnsStarted = false;
  if (!mdnsStarted) {
    if (MDNS.begin(MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", HTTP_PORT);
    }
    mdnsStarted = true;
  }
}

bool NetworkManager::isConnected() const { return !apMode_ && WiFi.status() == WL_CONNECTED; }

String NetworkManager::getIp() const {
  if (apMode_) return WiFi.softAPIP().toString();
  return WiFi.localIP().toString();
}

String NetworkManager::getConnectedSsid() const {
  if (apMode_ || WiFi.status() != WL_CONNECTED) return "";
  return WiFi.SSID();
}

String NetworkManager::getStatusSummary() const {
  if (apMode_) return String("AP:") + apSsid_;
  if (isConnected()) return String("STA:") + getIp();
  return "connecting...";
}

void NetworkManager::applyCredentials(const String& ssid, const String& password) {
  storage_->setWifiCredentials(ssid, password);
  startStationMode();
}

void NetworkManager::forgetWifi() {
  storage_->clearWifiCredentials();
  WiFi.disconnect(true /* wifioff */);
  startApFallback();
}

void NetworkManager::requestScan() {
  if (scanRequested_) return;
  scanReady_ = false;
  scanRequested_ = true;
  WiFi.scanNetworks(true /* async */);
}

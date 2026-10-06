#include "StorageManager.h"
#include "Config.h"
#include <esp_system.h>

namespace {
static constexpr const char* kPrefsNamespace = "airbox";
static constexpr const char* kKeyWifiSsid = "wifi_ssid";
static constexpr const char* kKeyWifiPass = "wifi_pass";
static constexpr const char* kKeyApiKey = "api_key";
static constexpr const char* kKeyRebreathCap = "rb_cap_ms";
static constexpr const char* kKeyAirCap = "air_cap_ms";
static constexpr const char* kKeyLanguage = "language";

String randomHexKey(size_t bytes) {
  String out;
  out.reserve(bytes * 2);
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t b = static_cast<uint8_t>(esp_random() & 0xFF);
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", b);
    out += buf;
  }
  return out;
}
}  // namespace

void StorageManager::begin() { prefs_.begin(kPrefsNamespace, false); }

bool StorageManager::hasWifiCredentials() const {
  return prefs_.isKey(kKeyWifiSsid) && prefs_.getString(kKeyWifiSsid, "").length() > 0;
}

String StorageManager::getWifiSsid() const { return prefs_.getString(kKeyWifiSsid, ""); }

String StorageManager::getWifiPassword() const { return prefs_.getString(kKeyWifiPass, ""); }

void StorageManager::setWifiCredentials(const String& ssid, const String& password) {
  prefs_.putString(kKeyWifiSsid, ssid);
  prefs_.putString(kKeyWifiPass, password);
}

void StorageManager::clearWifiCredentials() {
  prefs_.remove(kKeyWifiSsid);
  prefs_.remove(kKeyWifiPass);
}

String StorageManager::getApiKey() {
  String key = prefs_.getString(kKeyApiKey, "");
  if (key.length() == 0) {
    key = regenerateApiKey();
  }
  return key;
}

String StorageManager::regenerateApiKey() {
  String key = randomHexKey(16);  // 32 hex characters
  prefs_.putString(kKeyApiKey, key);
  return key;
}

uint32_t StorageManager::getRebreathCapMs() const {
  return prefs_.getUInt(kKeyRebreathCap, REBREATH_CAP_DEFAULT_MS);
}

void StorageManager::setRebreathCapMs(uint32_t ms) { prefs_.putUInt(kKeyRebreathCap, ms); }

uint32_t StorageManager::getAirCapMs() const {
  return prefs_.getUInt(kKeyAirCap, AIR_CAP_DEFAULT_MS);
}

void StorageManager::setAirCapMs(uint32_t ms) { prefs_.putUInt(kKeyAirCap, ms); }

Language StorageManager::getLanguage() const {
  return static_cast<Language>(prefs_.getUChar(kKeyLanguage, static_cast<uint8_t>(LANGUAGE_DEFAULT)));
}

void StorageManager::setLanguage(Language lang) {
  prefs_.putUChar(kKeyLanguage, static_cast<uint8_t>(lang));
}

void StorageManager::factoryReset() { prefs_.clear(); }

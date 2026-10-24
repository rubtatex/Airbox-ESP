#pragma once

// =============================================================================
// StorageManager — non-volatile persistence (Preferences/NVS only).
//
// Sequences are kept in RAM only (see SequenceEngine, .claude/CLAUDE.md) — this
// module only stores small data: WiFi config, API key, safety config, language.
// =============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include "UiStrings.h"

class StorageManager {
 public:
  void begin();

  // --- WiFi ---------------------------------------------------------------
  bool hasWifiCredentials() const;
  String getWifiSsid() const;
  String getWifiPassword() const;
  void setWifiCredentials(const String& ssid, const String& password);
  void clearWifiCredentials();

  // --- API key (pairing) ---------------------------------------------------
  // Randomly generated on the very first boot if missing, then persisted.
  String getApiKey();
  String regenerateApiKey();

  // --- Safety config (persisted to survive a reboot) -----------------------
  uint32_t getRebreathCapMs() const;
  void setRebreathCapMs(uint32_t ms);
  uint32_t getAirCapMs() const;
  void setAirCapMs(uint32_t ms);

  // --- Screen language (English by default) --------------------------------
  Language getLanguage() const;
  void setLanguage(Language lang);

  // Full reset (WiFi config, API key). Does not affect relay state
  // (RelayController/SafetyManager handle that separately and always come
  // back disarmed/de-energized at boot anyway).
  void factoryReset();

 private:
  mutable Preferences prefs_;
};

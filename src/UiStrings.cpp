#include "UiStrings.h"

namespace {
// [Str][Language] — column order = Language enum order.
static const char* const kTable[static_cast<int>(Str::Count)][LANGUAGE_COUNT] = {
    /* EstopLine1         */ {"EMERGENCY", "ARRET"},
    /* EstopLine2         */ {"STOP", "D'URGENCE"},
    /* EstopHint          */ {"Hold long = REARM", "Appui long = REARM"},
    /* BootTitle          */ {"Airbox-ESP", "Airbox-ESP"},
    /* BootStarting       */ {"Starting...", "Demarrage..."},
    /* BootConnectingWifi */ {"Connecting WiFi...", "Connexion WiFi..."},
    /* QrScanTo           */ {"Scan to", "Scannez pour"},
    /* QrConfigureWifi    */ {"configure WiFi", "configurer le WiFi"},
    /* BleConnected       */ {"BLE: connected", "BLE: connecte"},
    /* BleFree            */ {"BLE: free", "BLE: libre"},
    /* PeekNetworkTitle   */ {"-- Network --", "-- Reseau --"},
    /* PeekApMode         */ {"Access point:", "Mode point d'acces:"},
    /* PeekWifiPrefix     */ {"WiFi: ", "WiFi: "},
    /* PeekIpPrefix       */ {"IP: ", "IP: "},
    /* PeekWifiConnecting */ {"WiFi: connecting...", "WiFi: connexion..."},
    /* PeekApiKeyTitle    */ {"-- API Key --", "-- Cle API --"},
};
}  // namespace

const char* tr(Str id, Language lang) {
  int row = static_cast<int>(id);
  int col = static_cast<int>(lang);
  if (row < 0 || row >= static_cast<int>(Str::Count)) return "";
  if (col < 0 || col >= LANGUAGE_COUNT) col = static_cast<int>(LANGUAGE_DEFAULT);
  const char* s = kTable[row][col];
  return s ? s : kTable[row][static_cast<int>(LANGUAGE_DEFAULT)];
}

const char* languageCode(Language lang) {
  switch (lang) {
    case Language::English: return "en";
    case Language::French: return "fr";
  }
  return "en";
}

bool languageFromCode(const String& code, Language& out) {
  if (code == "en") { out = Language::English; return true; }
  if (code == "fr") { out = Language::French; return true; }
  return false;
}

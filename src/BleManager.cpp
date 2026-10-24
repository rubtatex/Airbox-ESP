#include "BleManager.h"
#include "Config.h"
#include <mbedtls/base64.h>

namespace {
BleManager* g_manager = nullptr;

bool parseRelayName(const char* name, RelayId& out) {
  if (strcmp(name, "air") == 0) { out = RelayId::Air; return true; }
  if (strcmp(name, "rebreath") == 0) { out = RelayId::Rebreath; return true; }
  if (strcmp(name, "aroma") == 0) { out = RelayId::Aroma; return true; }
  return false;
}
}  // namespace

class AirboxCmdCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr) override {
    if (!g_manager) return;
    g_manager->handleCmd(String(chr->getValue().c_str()));
  }
};

class AirboxEstopCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* /*chr*/) override {
    if (!g_manager || !g_manager->safety_) return;
    // Deliberately WITHOUT API key check: the safety brake must remain
    // reachable even without valid pairing. See CLAUDE.md.
    g_manager->safety_->estop("BLE estop characteristic");
  }
};

class AirboxWifiConfigCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr) override {
    if (!g_manager) return;
    g_manager->handleWifiConfig(String(chr->getValue().c_str()));
  }
};

static AirboxCmdCallbacks g_cmdCallbacks;
static AirboxEstopCallbacks g_estopCallbacks;
static AirboxWifiConfigCallbacks g_wifiConfigCallbacks;

void BleManager::begin(SafetyManager& safety, StorageManager& storage, NetworkManager& network,
                        DisplayManager& display, SequenceEngine& sequences) {
  safety_ = &safety;
  storage_ = &storage;
  network_ = &network;
  display_ = &display;
  sequences_ = &sequences;
  g_manager = this;

  NimBLEDevice::init("Airbox");
  server_ = NimBLEDevice::createServer();

  NimBLEService* service = server_->createService(BLE_SERVICE_UUID);

  chrCmd_ = service->createCharacteristic(BLE_CHAR_CMD_UUID, NIMBLE_PROPERTY::WRITE);
  chrCmd_->setCallbacks(&g_cmdCallbacks);

  chrState_ = service->createCharacteristic(BLE_CHAR_STATE_UUID,
                                             NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  chrEstop_ = service->createCharacteristic(BLE_CHAR_ESTOP_UUID, NIMBLE_PROPERTY::WRITE);
  chrEstop_->setCallbacks(&g_estopCallbacks);

  chrWifiConfig_ =
      service->createCharacteristic(BLE_CHAR_WIFI_CONFIG_UUID, NIMBLE_PROPERTY::WRITE);
  chrWifiConfig_->setCallbacks(&g_wifiConfigCallbacks);

  // Initial value set BEFORE starting the service/advertising, so that
  // a client can never read an attribute that is still "empty" by default.
  lastStateJson_ = buildStateJson();
  // IMPORTANT: pass the String object directly, NOT .c_str(). With a plain
  // `const char*`, NimBLECharacteristic::setValue() resolves to its generic
  // template overload (no dedicated const char* overload), which copies
  // the pointer's ADDRESS (sizeof(T)=4 bytes on ESP32) instead of the
  // pointed-to text. String has c_str()/length(), which selects the right
  // overload (detected via Has_c_str_len in NimBLEAttValue). See
  // CLAUDE.md.
  chrState_->setValue(lastStateJson_);

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->start();

  // Reacts to safety changes by pushing the updated state right away
  // (armed/disarmed/estop/relays changed/cap reached).
  safety_->onEvent([this](const SafetyEvent&) { notifyStateChange(); });
  sequences_->onNotice([this](seq::Notice, const char*) { notifyStateChange(); });
}

bool BleManager::checkKey(const JsonDocument& doc) const {
  const char* key = doc["key"] | "";
  return storage_->getApiKey().equals(key);
}

String BleManager::buildStateJson() const {
  JsonDocument doc;
  doc["state"] = safety_->getStateName();
  doc["sequenceVersion"] = 1;  // lets the app know this firmware can play sequences (see API.md)
  doc["rebreathRemainingMs"] = safety_->getRebreathRemainingMs();
  doc["airRemainingMs"] = safety_->getAirRemainingMs();
  if (display_) doc["language"] = languageCode(display_->getLanguage());
  // The API key is deliberately included here, without authentication:
  // reading STATE over BLE already requires physical proximity (BLE range,
  // a few meters), comparable to reading the screen. This lets an app pair
  // automatically without manual re-entry. See CLAUDE.md.
  doc["apiKey"] = storage_->getApiKey();

  RelayController& relays = safety_->relays();
  JsonObject air = doc["air"].to<JsonObject>();
  RelayLogicalState a = relays.getLogicalState(RelayId::Air);
  air["open"] = a.logicalOn;

  JsonObject rb = doc["rebreath"].to<JsonObject>();
  RelayLogicalState r = relays.getLogicalState(RelayId::Rebreath);
  rb["open"] = r.logicalOn;

  JsonObject aroma = doc["aroma"].to<JsonObject>();
  RelayLogicalState ar = relays.getLogicalState(RelayId::Aroma);
  aroma["running"] = ar.logicalOn;

  // Present only while a sequence is loaded/running, or to report why the last one failed.
  JsonObject sequence = doc["sequence"].to<JsonObject>();
  if (!sequences_->describe(sequence)) doc.remove("sequence");

  doc["wifiConnected"] = network_->isConnected();
  doc["wifiScanning"] = network_->isScanning();
  if (network_->isScanReady()) {
    // ssid+rssi only (no "secure"/"connected", available via REST) and
    // capped at 4 networks: the STATE characteristic has a hard 512-byte
    // maximum in NimBLE (BLE_ATT_ATTR_MAX_LEN) — beyond that, setValue()
    // silently fails and keeps the old value (see CLAUDE.md). With an SSID
    // at the maximum allowed length (32 chars), 4 entries fit comfortably
    // in the remaining margin; the full list (up to 10, with security and
    // "connected" flag) remains available via REST (/api/wifi/scan) for
    // the test app's graphical interface.
    JsonArray nets = doc["wifiNetworks"].to<JsonArray>();
    auto results = network_->getLastScanResults();
    size_t limit = results.size() < 4 ? results.size() : 4;
    for (size_t i = 0; i < limit; ++i) {
      JsonObject o = nets.add<JsonObject>();
      o["ssid"] = results[i].ssid;
      o["rssi"] = results[i].rssi;
    }
  }

  String out;
  serializeJson(doc, out);
  // The STATE characteristic cannot hold more than 512 bytes (see CLAUDE.md).
  // The Wi-Fi scan list is the only part that is both large and optional:
  // drop it rather than lose the whole update.
  if (out.length() > 480 && !doc["wifiNetworks"].isNull()) {
    doc.remove("wifiNetworks");
    out = "";
    serializeJson(doc, out);
  }
  return out;
}

void BleManager::notifyStateChange() {
  if (!chrState_) return;
  String json = buildStateJson();
  if (json == lastStateJson_) return;
  lastStateJson_ = json;
  chrState_->setValue(json);  // String object, NOT .c_str() — see begin() for the explanation
  if (server_ && server_->getConnectedCount() > 0) {
    chrState_->notify();
  }
}

void BleManager::tick() {
  uint32_t now = millis();
  if (now - lastNotifyMs_ < 1000) return;
  lastNotifyMs_ = now;
  notifyStateChange();
}

void BleManager::handleCmd(const String& raw) {
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;
  if (!checkKey(doc)) return;
  sequences_->touch();  // any authenticated command shows the app is still there

  const char* cmd = doc["cmd"] | "";

  if (strcmp(cmd, "arm") == 0) {
    safety_->arm();
  } else if (strcmp(cmd, "disarm") == 0) {
    safety_->disarm();
  } else if (strcmp(cmd, "rearm") == 0) {
    safety_->reArm();
  } else if (strcmp(cmd, "relay.set") == 0) {
    RelayId id;
    if (!parseRelayName(doc["relay"] | "", id)) return;
    safety_->setRelay(id, doc["state"] | false);
  } else if (strcmp(cmd, "relay.toggle") == 0) {
    RelayId id;
    if (!parseRelayName(doc["relay"] | "", id)) return;
    safety_->toggleRelay(id);
  } else if (strcmp(cmd, "safety.config") == 0) {
    // Each field is optional/independent: send only the one to change,
    // the other stays unchanged.
    if (!doc["rebreathCapMs"].isNull()) {
      safety_->setRebreathCapMs(doc["rebreathCapMs"].as<uint32_t>());
      storage_->setRebreathCapMs(safety_->getRebreathCapMs());
    }
    if (!doc["airCapMs"].isNull()) {
      safety_->setAirCapMs(doc["airCapMs"].as<uint32_t>());
      storage_->setAirCapMs(safety_->getAirCapMs());
    }
  } else if (strcmp(cmd, "display.text") == 0) {
    if (display_) {
      String line1 = String((const char*)(doc["line1"] | ""));
      String line2 = String((const char*)(doc["line2"] | ""));
      uint32_t durationMs = doc["durationMs"] | 3000;
      display_->showTemporaryText(line1, line2, durationMs);
    }
  } else if (strcmp(cmd, "sequence.upload") == 0) {
    // {"offset": 0, "total": 24, "data": "<base64 piece>"} — pieces in order, at most ~290 bytes each.
    // The result (loaded / error) is reported in STATE.sequence.
    const char* b64 = doc["data"] | "";
    uint8_t piece[320];
    size_t pieceLen = 0;
    String error;
    if (mbedtls_base64_decode(piece, sizeof(piece), &pieceLen, reinterpret_cast<const unsigned char*>(b64),
                              strlen(b64)) == 0) {
      sequences_->upload(doc["offset"] | 0, doc["total"] | 0, piece, pieceLen, error);
    }
  } else if (strcmp(cmd, "sequence.start") == 0) {
    String error;
    sequences_->start(doc["holdMs"] | seq::kHoldDefaultMs, error);
  } else if (strcmp(cmd, "sequence.stop") == 0) {
    sequences_->stop("stopped by the app");
  } else if (strcmp(cmd, "sequence.keepalive") == 0) {
    // Nothing more to do: the authenticated command itself counted as contact.
  } else if (strcmp(cmd, "wifi.scan") == 0) {
    network_->requestScan();  // result visible in the next STATE reads
  } else if (strcmp(cmd, "wifi.forget") == 0) {
    network_->forgetWifi();
  } else if (strcmp(cmd, "system.language") == 0) {
    Language lang;
    if (display_ && languageFromCode(String((const char*)(doc["language"] | "")), lang)) {
      display_->setLanguage(lang);
      storage_->setLanguage(lang);
    }
  }

  notifyStateChange();
}

void BleManager::handleWifiConfig(const String& raw) {
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return;
  if (!checkKey(doc)) return;

  String ssid = String((const char*)(doc["ssid"] | ""));
  String password = String((const char*)(doc["password"] | ""));
  if (ssid.length() == 0) return;

  network_->applyCredentials(ssid, password);
}

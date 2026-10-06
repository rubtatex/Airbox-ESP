#include "WebApi.h"
#include "Config.h"
#include <ArduinoJson.h>
#include <Update.h>

namespace {
RelayId relayIdFromPathName(const String& name, bool& ok) {
  ok = true;
  if (name == "air") return RelayId::Air;
  if (name == "rebreath") return RelayId::Rebreath;
  if (name == "aroma") return RelayId::Aroma;
  ok = false;
  return RelayId::Air;
}

// State accumulated during an OTA upload (see /api/system/ota handler),
// attached to AsyncWebServerRequest::_tempObject like jsonBody() does for
// JSON bodies — except the whole image is never accumulated in RAM here,
// it is streamed to flash via Update.write().
struct OtaUploadState {
  bool authOk = false;
  bool refusedNotDisarmed = false;
  bool started = false;  // Update.begin() succeeded
  String error;          // non-empty = failure (begin/write/end), already resolved
};
}  // namespace

void WebApi::begin(SafetyManager& safety, StorageManager& storage, NetworkManager& network,
                    DisplayManager& display) {
  safety_ = &safety;
  storage_ = &storage;
  network_ = &network;
  display_ = &display;

  // CORS: only so that test tools (web page served from another origin,
  // e.g. test-app/) can call the API with fetch().
  // Does NOT affect any safety behavior: the X-Api-Key is still required
  // exactly as before, this only adds HTTP response headers.
  // See CLAUDE.md.
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                       "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, X-Api-Key");

  setupWebSocket();
  setupRoutes();

  server_.begin();

  safety_->onEvent([this](const SafetyEvent& evt) {
    switch (evt.type) {
      case SafetyEvent::Type::Armed: broadcastStatus("armed"); break;
      case SafetyEvent::Type::Disarmed: broadcastStatus("disarmed"); break;
      case SafetyEvent::Type::Estop: broadcastStatus("estop_triggered"); break;
      case SafetyEvent::Type::Rearmed: broadcastStatus("rearmed"); break;
      case SafetyEvent::Type::RelayChanged: broadcastStatus("relay_changed"); break;
      case SafetyEvent::Type::SafetyCapTriggered: broadcastStatus("safety_cap_triggered"); break;
    }
  });
}

// ---------------------------------------------------------------------------
// Auth / JSON helpers
// ---------------------------------------------------------------------------

bool WebApi::authOk(AsyncWebServerRequest* request) const {
  if (!request->hasHeader("X-Api-Key")) return false;
  String key = request->header("X-Api-Key");
  return storage_->getApiKey().equals(key);
}

void WebApi::sendError(AsyncWebServerRequest* request, int code, const String& message) const {
  JsonDocument doc;
  doc["error"] = message;
  String out;
  serializeJson(doc, out);
  request->send(code, "application/json", out);
}

void WebApi::sendJson(AsyncWebServerRequest* request, JsonDocument& doc) const {
  String out;
  serializeJson(doc, out);
  request->send(200, "application/json", out);
}

ArBodyHandlerFunction WebApi::jsonBody(JsonBodyHandler handler) {
  return [handler](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
                    size_t total) {
    String* buf = reinterpret_cast<String*>(request->_tempObject);
    if (index == 0) {
      buf = new String();
      buf->reserve(total);
      request->_tempObject = buf;
    }
    if (buf) {
      buf->concat(reinterpret_cast<const char*>(data), len);
    }
    if (index + len != total) return;  // not complete yet

    JsonDocument doc;
    DeserializationError err = buf ? deserializeJson(doc, *buf) : DeserializationError::EmptyInput;
    delete buf;
    request->_tempObject = nullptr;

    if (err) {
      JsonDocument errDoc;
      errDoc["error"] = String("invalid JSON: ") + err.c_str();
      String out;
      serializeJson(errDoc, out);
      request->send(400, "application/json", out);
      return;
    }
    handler(request, doc);
  };
}

// ---------------------------------------------------------------------------
// Building state payloads
// ---------------------------------------------------------------------------

String WebApi::relayToJson(RelayId id) const {
  RelayLogicalState st = safety_->relays().getLogicalState(id);
  JsonDocument doc;
  doc["name"] = st.name;
  doc["energized"] = st.energized;
  doc["logicalOn"] = st.logicalOn;
  doc["label"] = st.logicalOn ? st.logicalOnLabel : st.logicalOffLabel;
  String out;
  serializeJson(doc, out);
  return out;
}

String WebApi::buildRelaysJson() const {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (RelayId id : {RelayId::Air, RelayId::Rebreath, RelayId::Aroma}) {
    JsonDocument single;
    deserializeJson(single, relayToJson(id));
    arr.add(single);
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String WebApi::buildStatusJson() const {
  JsonDocument doc;
  doc["safetyState"] = safety_->getStateName();
  doc["rebreathRemainingMs"] = safety_->getRebreathRemainingMs();
  doc["rebreathCapMs"] = safety_->getRebreathCapMs();
  doc["airRemainingMs"] = safety_->getAirRemainingMs();
  doc["airCapMs"] = safety_->getAirCapMs();
  doc["wifiConnected"] = network_->isConnected();
  doc["apMode"] = network_->isApMode();
  doc["ip"] = network_->getIp();
  String out;
  serializeJson(doc, out);
  return out;
}

void WebApi::broadcastStatus(const char* eventType) {
  JsonDocument doc;
  deserializeJson(doc, buildStatusJson());
  doc["event"] = eventType;
  String out;
  serializeJson(doc, out);
  ws_.textAll(out);
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

void WebApi::setupWebSocket() {
  ws_.onEvent([this](AsyncWebSocket* /*server*/, AsyncWebSocketClient* client, AwsEventType type,
                      void* /*arg*/, uint8_t* /*data*/, size_t /*len*/) {
    if (type == WS_EVT_CONNECT) {
      // Initial state sent to the newly connected client.
      client->text(buildStatusJson());
    }
    // WS_EVT_DATA deliberately ignored: the WebSocket is read-only
    // (state push). Control goes through REST or BLE.
  });

  // Auth: the API key is passed as a query parameter (?key=...) because
  // browsers don't allow custom headers on the WebSocket upgrade.
  ws_.setFilter([this](AsyncWebServerRequest* request) {
    if (!request->hasParam("key")) return false;
    return storage_->getApiKey().equals(request->getParam("key")->value());
  });

  server_.addHandler(&ws_);
}

// ---------------------------------------------------------------------------
// REST routes
// ---------------------------------------------------------------------------

void WebApi::setupRoutes() {
  // --- System (unauthenticated info for discovery) -------------------------
  server_.on("/api/system/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["firmware"] = "airbox-esp";
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    // The API key is exposed without authentication ONLY during the physical
    // pairing window (the device broadcasts its own fallback access point,
    // separate from the normal WiFi) — never once on the usual WiFi
    // network, to limit the exposure window. See CLAUDE.md.
    if (network_->isApMode()) {
      doc["apiKey"] = storage_->getApiKey();
    }
    sendJson(request, doc);
  });

  server_.on("/api/system/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    JsonDocument doc;
    deserializeJson(doc, buildStatusJson());
    sendJson(request, doc);
  });

  server_.on("/api/system/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    request->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });

  server_.on("/api/system/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    storage_->factoryReset();
    request->send(200, "application/json", "{\"ok\":true}");
    delay(200);
    ESP.restart();
  });

  server_.on("/api/system/regenerate-key", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    JsonDocument doc;
    doc["apiKey"] = storage_->regenerateApiKey();
    sendJson(request, doc);
  });

  // OTA firmware update (WiFi). RAW body (Content-Type:
  // application/octet-stream), the .bin image as produced by
  // `pio run` — NO multipart/form-data. Auth + DISARMED precondition
  // checked from the first byte (before starting the flash write) so as
  // not to waste write cycles on a rejected upload: unlike the jsonBody()
  // handlers, which buffer the whole body before checking (cheap for
  // JSON, not for an image over 1 MB). Requires DISARMED (just as
  // energizing a relay requires ARMED, replacing the firmware requires
  // the opposite): no running sequence may overlap a flash.
  // See CLAUDE.md, section "OTA (over-the-network firmware update)".
  server_.on(
      "/api/system/ota", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        auto* st = reinterpret_cast<OtaUploadState*>(request->_tempObject);
        if (index == 0) {
          st = new OtaUploadState();
          request->_tempObject = st;
          st->authOk = authOk(request);
          st->refusedNotDisarmed = (safety_->getState() != SystemState::Disarmed);
          if (st->authOk && !st->refusedNotDisarmed) {
            if (Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) {
              st->started = true;
              display_->showTemporaryText("Updating firmware", "Do not power off", 60000);
            } else {
              st->error = Update.errorString();
            }
          }
        }
        if (st->started && st->error.isEmpty() && Update.write(data, len) != len) {
          st->error = Update.errorString();
        }

        if (index + len != total) return;  // not the last chunk yet

        if (!st->authOk) {
          sendError(request, 401, "invalid or missing X-Api-Key");
        } else if (st->refusedNotDisarmed) {
          sendError(request, 409, "refused: disarm the system before an OTA update");
        } else if (!st->started || !st->error.isEmpty() || !Update.end(true)) {
          if (st->error.isEmpty()) st->error = Update.errorString();
          sendError(request, 400, String("OTA failed: ") + st->error);
        } else {
          request->send(200, "application/json", "{\"ok\":true}");
          delete st;
          request->_tempObject = nullptr;
          delay(200);
          ESP.restart();
          return;
        }
        delete st;
        request->_tempObject = nullptr;
      });

  server_.on("/api/system/language", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    JsonDocument doc;
    doc["language"] = languageCode(display_->getLanguage());
    JsonArray avail = doc["available"].to<JsonArray>();
    for (uint8_t i = 0; i < LANGUAGE_COUNT; ++i) avail.add(languageCode(static_cast<Language>(i)));
    sendJson(request, doc);
  });

  server_.on(
      "/api/system/language", HTTP_PUT, [](AsyncWebServerRequest*) {}, nullptr,
      jsonBody([this](AsyncWebServerRequest* request, JsonDocument& doc) {
        if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
        Language lang;
        String code = String((const char*)(doc["language"] | ""));
        if (!languageFromCode(code, lang)) return sendError(request, 400, "unknown language code");
        display_->setLanguage(lang);
        storage_->setLanguage(lang);
        JsonDocument out;
        out["ok"] = true;
        out["language"] = languageCode(lang);
        sendJson(request, out);
      }));

  // --- Safety ---------------------------------------------------------------
  server_.on("/api/system/safety-status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    JsonDocument doc;
    doc["state"] = safety_->getStateName();
    doc["rebreathRemainingMs"] = safety_->getRebreathRemainingMs();
    doc["rebreathCapMs"] = safety_->getRebreathCapMs();
    doc["airRemainingMs"] = safety_->getAirRemainingMs();
    doc["airCapMs"] = safety_->getAirCapMs();
    sendJson(request, doc);
  });

  server_.on("/api/system/arm", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    bool ok = safety_->arm();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["state"] = safety_->getStateName();
    sendJson(request, doc);
  });

  server_.on("/api/system/disarm", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    bool ok = safety_->disarm();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["state"] = safety_->getStateName();
    sendJson(request, doc);
  });

  // Deliberately WITHOUT authOk(): the safety brake must always respond,
  // even without a valid API key. See CLAUDE.md.
  server_.on("/api/system/estop", HTTP_POST, [this](AsyncWebServerRequest* request) {
    safety_->estop("REST /api/system/estop");
    request->send(200, "application/json", "{\"ok\":true,\"state\":\"estop\"}");
  });

  server_.on("/api/system/rearm", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    bool ok = safety_->reArm();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["state"] = safety_->getStateName();
    sendJson(request, doc);
  });

  server_.on(
      "/api/system/safety-config", HTTP_PUT, [](AsyncWebServerRequest*) {}, nullptr,
      jsonBody([this](AsyncWebServerRequest* request, JsonDocument& doc) {
        if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
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
        JsonDocument out;
        out["ok"] = true;
        out["rebreathCapMs"] = safety_->getRebreathCapMs();
        out["airCapMs"] = safety_->getAirCapMs();
        sendJson(request, out);
      }));

  // --- WiFi -------------------------------------------------------------
  server_.on("/api/wifi/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    JsonDocument doc;
    doc["connected"] = network_->isConnected();
    doc["apMode"] = network_->isApMode();
    doc["ip"] = network_->getIp();
    doc["ssid"] = network_->getConnectedSsid();
    sendJson(request, doc);
  });

  server_.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    // "One fresh scan per call cycle" semantics: if a ready result has not
    // yet been delivered to THIS client (generation comparison), deliver
    // it. Otherwise, if nothing is running, start a new scan. The shared
    // generation counter lets REST and BLE each track their own progress
    // without stepping on each other.
    if (network_->isScanReady() && network_->getScanGeneration() != lastSeenWifiScanGen_) {
      lastSeenWifiScanGen_ = network_->getScanGeneration();
      JsonDocument doc;
      JsonArray arr = doc["networks"].to<JsonArray>();
      String connectedSsid = network_->getConnectedSsid();
      for (auto& net : network_->getLastScanResults()) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = net.ssid;
        o["rssi"] = net.rssi;
        o["secure"] = net.secure;
        o["connected"] = (net.ssid == connectedSsid);
      }
      sendJson(request, doc);
      return;
    }
    network_->requestScan();  // no-op if a scan is already running
    JsonDocument doc;
    doc["scanning"] = true;
    sendJson(request, doc);
  });

  server_.on(
      "/api/wifi/config", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      jsonBody([this](AsyncWebServerRequest* request, JsonDocument& doc) {
        if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
        String ssid = String((const char*)(doc["ssid"] | ""));
        String password = String((const char*)(doc["password"] | ""));
        if (ssid.length() == 0) return sendError(request, 400, "missing 'ssid'");
        network_->applyCredentials(ssid, password);
        request->send(200, "application/json", "{\"ok\":true}");
      }));

  server_.on("/api/wifi/forget", HTTP_POST, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    // Erases only the WiFi credentials (falls back to the fallback access
    // point) — to switch back to 100% local use (BLE) without losing
    // the API key/safety config, unlike factory-reset.
    network_->forgetWifi();
    request->send(200, "application/json", "{\"ok\":true}");
  });

  // --- Relays -------------------------------------------------------------
  server_.on("/api/relays", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    request->send(200, "application/json", buildRelaysJson());
  });

  server_.on("^\\/api\\/relays\\/([a-z]+)$", HTTP_GET, [this](AsyncWebServerRequest* request) {
    if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
    bool ok;
    RelayId id = relayIdFromPathName(request->pathArg(0), ok);
    if (!ok) return sendError(request, 404, "unknown relay");
    request->send(200, "application/json", relayToJson(id));
  });

  server_.on(
      "^\\/api\\/relays\\/([a-z]+)\\/set$", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      jsonBody([this](AsyncWebServerRequest* request, JsonDocument& doc) {
        if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
        bool ok;
        RelayId id = relayIdFromPathName(request->pathArg(0), ok);
        if (!ok) return sendError(request, 404, "unknown relay");
        bool state = doc["state"] | false;
        bool applied = safety_->setRelay(id, state);
        JsonDocument out;
        out["ok"] = applied;
        if (!applied) out["error"] = "refused: system not armed (arm the system first)";
        sendJson(request, out);
      }));

  server_.on("^\\/api\\/relays\\/([a-z]+)\\/toggle$", HTTP_POST,
             [this](AsyncWebServerRequest* request) {
               if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
               bool ok;
               RelayId id = relayIdFromPathName(request->pathArg(0), ok);
               if (!ok) return sendError(request, 404, "unknown relay");
               bool applied = safety_->toggleRelay(id);
               JsonDocument out;
               out["ok"] = applied;
               if (!applied) out["error"] = "refused: system not armed (arm the system first)";
               sendJson(request, out);
             });

  // --- Display --------------------------------------------------------------
  server_.on(
      "/api/display/text", HTTP_POST, [](AsyncWebServerRequest*) {}, nullptr,
      jsonBody([this](AsyncWebServerRequest* request, JsonDocument& doc) {
        if (!authOk(request)) return sendError(request, 401, "invalid or missing X-Api-Key");
        String line1 = String((const char*)(doc["line1"] | ""));
        String line2 = String((const char*)(doc["line2"] | ""));
        uint32_t durationMs = doc["durationMs"] | 3000;
        display_->showTemporaryText(line1, line2, durationMs);
        request->send(200, "application/json", "{\"ok\":true}");
      }));

  server_.onNotFound([this](AsyncWebServerRequest* request) {
    if (request->method() == HTTP_OPTIONS) {
      // CORS preflight request: the global headers (DefaultHeaders) are
      // enough, just reply 204 with no body.
      request->send(204);
      return;
    }
    sendError(request, 404, "not found");
  });
}

void WebApi::tick() {
  ws_.cleanupClients();

  uint32_t now = millis();
  if (now - lastBroadcastMs_ < 1000) return;
  lastBroadcastMs_ = now;

  String json = buildStatusJson();
  if (json == lastBroadcastJson_) return;  // nothing new (apart from events already broadcast)
  lastBroadcastJson_ = json;
}

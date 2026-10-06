#pragma once

// =============================================================================
// WebApi — REST API (JSON) + real-time WebSocket, on ESPAsyncWebServer.
//
// Auth: "X-Api-Key" header required on every route EXCEPT:
//   - GET  /api/system/info   (discovery, before pairing)
//   - POST /api/system/estop  (the safety brake must never depend on a
//                              valid API key — see CLAUDE.md)
// The /ws WebSocket expects the key as a query parameter (?key=...), since
// browsers don't allow custom headers on the WS upgrade.
//
// RULE: WebApi NEVER calls RelayController directly — every relay
// action goes through SafetyManager.
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "SafetyManager.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "DisplayManager.h"

class WebApi {
 public:
  void begin(SafetyManager& safety, StorageManager& storage, NetworkManager& network,
             DisplayManager& display);

  // Call on every loop(): cleans up dead WS clients, pushes periodic
  // updates (Re-Breath countdown, etc.).
  void tick();

 private:
  SafetyManager* safety_ = nullptr;
  StorageManager* storage_ = nullptr;
  NetworkManager* network_ = nullptr;
  uint32_t lastSeenWifiScanGen_ = 0;  // generation tracking for /api/wifi/scan
  DisplayManager* display_ = nullptr;

  AsyncWebServer server_{80};
  AsyncWebSocket ws_{"/ws"};

  uint32_t lastBroadcastMs_ = 0;
  String lastBroadcastJson_;

  bool authOk(AsyncWebServerRequest* request) const;
  void sendError(AsyncWebServerRequest* request, int code, const String& message) const;
  void sendJson(AsyncWebServerRequest* request, JsonDocument& doc) const;

  String buildStatusJson() const;
  String buildRelaysJson() const;
  String relayToJson(RelayId id) const;

  void broadcastStatus(const char* eventType);

  void setupRoutes();
  void setupWebSocket();

  // "JSON body" handlers — common signature used via an adapter stored in
  // AsyncWebServerRequest::_tempObject (body accumulation).
  using JsonBodyHandler = std::function<void(AsyncWebServerRequest*, JsonDocument&)>;
  ArBodyHandlerFunction jsonBody(JsonBodyHandler handler);
};

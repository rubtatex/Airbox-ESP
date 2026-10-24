#include <Arduino.h>
#include <esp_ota_ops.h>
#include "Config.h"
#include "RelayController.h"
#include "SafetyManager.h"
#include "DisplayManager.h"
#include "StorageManager.h"
#include "NetworkManager.h"
#include "BleManager.h"
#include "SequenceEngine.h"
#include "WebApi.h"

RelayController relayController;
SafetyManager safety;
DisplayManager display;
StorageManager storage;
NetworkManager network;
BleManager ble;
SequenceEngine sequences;
WebApi webApi;

void setup() {
  Serial.begin(115200);

  // Confirms the image that just booted is valid, to cancel any pending
  // automatic rollback after an OTA (see .claude/CLAUDE.md, "OTA
  // (over-the-network firmware update)"). Harmless no-op if the bootloader
  // does not have rollback enabled.
  esp_ota_mark_app_valid_cancel_rollback();

  relayController.begin();  // immediate safe state on all 3 relays
  safety.begin(relayController);
  storage.begin();
  display.setLanguage(storage.getLanguage());  // before begin() so the initial splash is already in the right language
  display.begin();
  sequences.begin(safety);
  safety.onShortPress([]() { display.onShortPress(); });
  safety.setRebreathCapMs(storage.getRebreathCapMs());
  safety.setAirCapMs(storage.getAirCapMs());
  network.begin(storage);
  ble.begin(safety, storage, network, display, sequences);
  webApi.begin(safety, storage, network, display, sequences);

  display.setApiKey(storage.getApiKey());

  Serial.print("[Airbox] API key: ");
  Serial.println(storage.getApiKey());
}

void loop() {
  safety.tick();
  sequences.tick();
  network.tick();
  ble.tick();
  webApi.tick();

  display.setSafetyState(safety.getState(), safety.getRebreathRemainingMs(), safety.getRebreathCapMs(),
                          safety.getAirRemainingMs(), safety.getAirCapMs());
  display.setRelayStates(relayController.getLogicalState(RelayId::Air),
                          relayController.getLogicalState(RelayId::Rebreath),
                          relayController.getLogicalState(RelayId::Aroma));
  display.setNetworkInfo(network.isConnected(), network.isApMode(), network.isConnectingSta(),
                          network.getConnectedSsid(), network.getIp(), network.getApSsid());
  display.setBleConnected(ble.hasConnectedClient());
  display.tick();
}

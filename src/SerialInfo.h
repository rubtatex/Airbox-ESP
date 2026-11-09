#pragma once

#include <Arduino.h>
#include "NetworkManager.h"
#include "BleManager.h"

// Read-only status over the USB serial port, for the USB installer page (web/index.html).
// Send the line "airbox.info"; the Airbox answers one line: "AIRBOX " followed by a JSON object
// (see API.md, "USB diagnostic"). It never changes anything and never touches a relay. The API key is never
// part of the answer.
class SerialInfo {
 public:
  void begin(NetworkManager& network, BleManager& ble);
  void tick();  // call on every loop(); non-blocking, reads at most a few bytes per call

 private:
  NetworkManager* network_ = nullptr;
  BleManager* ble_ = nullptr;
  char line_[48];
  uint8_t len_ = 0;

  void handleLine();
  void printInfo();
};

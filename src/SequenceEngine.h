#pragma once

// =============================================================================
// SequenceEngine — plays a sequence sent by the app, on the Airbox itself, so a
// brief loss of the phone's connection does not interrupt it.
//
// The playing logic is in SequenceCore.h (plain C++, unit-tested on a computer,
// see test/host/). This class wires it to millis() and to SafetyManager:
// every relay change goes through SafetyManager::setRelay(), like any other
// command source, so the ARMED requirement and the Re-Breath / Air caps keep
// applying. See .claude/CLAUDE.md, "Sequences: played on the Airbox".
//
// Thread safety: REST (async tasks), BLE callbacks and loop() all reach this
// object, so its public methods take a recursive mutex.
// =============================================================================

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>
#include <vector>
#include "SafetyManager.h"
#include "SequenceCore.h"

class SequenceEngine {
 public:
  using Listener = std::function<void(seq::Notice, const char* detail)>;

  void begin(SafetyManager& safety);

  // Call on every loop(), after safety.tick(). Never blocks.
  void tick();

  // Receives the sequence in order (offset 0 starts a new upload). The last
  // piece validates it. Returns false and fills `error` on failure.
  bool upload(size_t offset, size_t total, const uint8_t* data, size_t length, String& error);

  // Starts the loaded sequence. Requires ARMED. `holdMs`: how long the app may
  // stay silent before the sequence is aborted (clamped).
  bool start(uint32_t holdMs, String& error);

  // Stops the sequence and de-energizes every relay. Always allowed.
  void stop(const char* reason);

  // Called on every authenticated request: the app is still there.
  void touch();

  // Fires after loading, starting, completing or aborting a sequence.
  void onNotice(Listener listener);

  seq::State state();
  // {"state","events","totalMs","elapsedMs","holdMs","error"} — fields that
  // do not apply are left out. Returns false (writes nothing) when there is
  // nothing worth reporting.
  bool describe(JsonObject out);

 private:
  SafetyManager* safety_ = nullptr;
  seq::Core core_;
  std::vector<Listener> listeners_;
  SemaphoreHandle_t mutex_ = nullptr;
};

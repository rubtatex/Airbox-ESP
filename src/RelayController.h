#pragma once

// =============================================================================
// RelayController — low-level GPIO abstraction for the 3 relays.
//
// ABSOLUTE RULE (see .claude/CLAUDE.md): this module is NEVER called directly
// outside SafetyManager. Every other part of the firmware (WebApi,
// BleManager, DisplayManager) goes through SafetyManager to
// change a relay's state.
//
// RelayController only knows the physical state ("energized" = coil powered)
// and the valve type (NO/NC/pump), and computes the matching LOGICAL state
// (valve open/closed, pump running) — this logical state is what must be
// shown to the user (screen, API), never the raw physical boolean, to
// avoid any dangerous confusion.
// =============================================================================

#include <Arduino.h>

enum class RelayId : uint8_t {
  Air = 0,      // Normally Open (NO) valve — de-energized = air open = SAFE
  Rebreath = 1, // Normally Closed (NC) valve — de-energized = closed = SAFE
  Aroma = 2,    // Pump — de-energized = stopped = default
  Count = 3
};

enum class ValveType : uint8_t {
  NormallyOpen,   // NO: de-energized = open
  NormallyClosed, // NC: de-energized = closed
  Pump            // no NO/NC semantics, just on/off
};

struct RelayLogicalState {
  bool energized;   // raw physical state (coil powered or not)
  bool logicalOn;   // Air/Rebreath: valve OPEN; Aroma: pump RUNNING
  const char* name;
  const char* logicalOnLabel;  // ex: "OPEN" / "RUNNING"
  const char* logicalOffLabel; // ex: "CLOSED" / "STOPPED"
};

class RelayController {
 public:
  // Configures the GPIOs as outputs and IMMEDIATELY forces the de-energized
  // (safe) state on all 3 relays, before any other initialization.
  void begin();

  // Writes the raw physical state. DO NOT CALL outside SafetyManager.
  void setEnergized(RelayId id, bool energized);

  bool isEnergized(RelayId id) const;

  // Derived logical state (see RelayLogicalState above).
  RelayLogicalState getLogicalState(RelayId id) const;

  ValveType getType(RelayId id) const;
  const char* getName(RelayId id) const;
  uint8_t getPin(RelayId id) const;

  // Immediately forces all 3 relays to the de-energized (safe) state.
  void allSafe();

  static constexpr size_t kCount = static_cast<size_t>(RelayId::Count);

 private:
  bool energized_[kCount] = {false, false, false};

  int8_t indexOf(RelayId id) const { return static_cast<int8_t>(id); }
};

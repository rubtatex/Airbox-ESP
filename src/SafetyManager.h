#pragma once

// =============================================================================
// SafetyManager — the ONLY allowed entry point for changing relay state.
// Read .claude/CLAUDE.md, section "Safety model — the heart of the
// firmware", before any change to this file.
//
// Absolute rule: WebApi, BleManager, DisplayManager must
// NEVER call RelayController directly. They call
// SafetyManager::setRelay()/toggleRelay(), which decides whether it is allowed.
//
// States: DISARMED (default at every boot) -> ARMED (explicit action)
//         -> ESTOP (can be triggered from anywhere) -> ARMED via
//         reArm() only (manual re-arm required).
// =============================================================================

#include <Arduino.h>
#include <functional>
#include <vector>
#include "RelayController.h"

enum class SystemState : uint8_t { Disarmed, Armed, Estop };

struct SafetyEvent {
  enum class Type {
    Armed,
    Disarmed,
    Estop,
    Rearmed,
    RelayChanged,
    SafetyCapTriggered,  // the `relay` field tells which one (Air or Rebreath)
  };

  Type type;
  const char* message;  // human-readable text (log/screen)
  RelayId relay;         // only relevant for RelayChanged
  bool relayValid;

  SafetyEvent(Type t, const char* msg, RelayId r = RelayId::Air, bool rv = false)
      : type(t), message(msg), relay(r), relayValid(rv) {}
};

using SafetyEventHandler = std::function<void(const SafetyEvent&)>;
using ShortPressHandler = std::function<void()>;

class SafetyManager {
 public:
  void begin(RelayController& relays);

  // Must be called on EVERY loop() iteration, without exception, before any
  // potentially blocking code: polls the button, checks the Re-Breath cap,
  // feeds the hardware watchdog.
  void tick();

  // --- State transitions -----------------------------------------------
  bool arm();     // DISARMED -> ARMED. Refused if ESTOP (reArm() required).
  bool disarm();  // ARMED -> DISARMED (also forces all relays to the safe state).
  void estop(const char* reason);  // From any state -> ESTOP.
  bool reArm();   // ESTOP -> ARMED (explicit manual re-arm).

  SystemState getState() const { return state_; }
  const char* getStateName() const;

  // --- Relay control (gatekeeper) --------------------------------------
  // Allows switching to the energized state ONLY when ARMED. De-energizing
  // (going to the safe state) is always allowed. Returns false if the
  // command is refused.
  bool setRelay(RelayId id, bool energized);
  bool toggleRelay(RelayId id);

  // --- Hard limits (Re-Breath open / Air closed) ----------------------------
  // INDEPENDENT settings: Air closed cuts off normal air, potentially more
  // critical than Re-Breath — see .claude/CLAUDE.md, section "Hard limits —
  // Re-Breath cap AND Air cap (independent)".
  uint32_t getRebreathCapMs() const { return rebreathCap_.capMs; }
  void setRebreathCapMs(uint32_t ms);  // clamped to [REBREATH_CAP_MIN_MS, MAX_MS]
  uint32_t getRebreathRemainingMs() const;  // 0 if not active

  uint32_t getAirCapMs() const { return airCap_.capMs; }
  void setAirCapMs(uint32_t ms);  // clamped to [AIR_CAP_MIN_MS, MAX_MS]
  uint32_t getAirRemainingMs() const;  // 0 if not active

  // --- Event subscription -----------------------------------------------
  void onEvent(SafetyEventHandler handler);
  void onShortPress(ShortPressHandler handler);

  RelayController& relays() { return *relaysPtr_; }

 private:
  RelayController* relaysPtr_ = nullptr;

  struct CapTimer {
    uint32_t capMs = 0;        // initialized in begin()
    uint32_t deadlineMs = 0;
    bool active = false;
  };

  SystemState state_ = SystemState::Disarmed;
  CapTimer rebreathCap_;
  CapTimer airCap_;

  // BOOT button
  bool buttonPressed_ = false;
  uint32_t buttonPressStartMs_ = 0;
  bool buttonLongFired_ = false;
  int lastRawReading_ = HIGH;
  uint32_t lastDebounceMs_ = 0;

  std::vector<SafetyEventHandler> eventHandlers_;
  std::vector<ShortPressHandler> shortPressHandlers_;

  void pollButton();
  void checkCaps();
  static uint32_t remainingMs(const CapTimer& t);
  void fire(SafetyEvent::Type type, const char* message, RelayId relay = RelayId::Air,
            bool relayValid = false);
  void forceAllSafeInternal();
};

#include "SafetyManager.h"
#include "Config.h"
#include <esp_task_wdt.h>

void SafetyManager::begin(RelayController& relays) {
  relaysPtr_ = &relays;
  rebreathCap_.capMs = REBREATH_CAP_DEFAULT_MS;
  airCap_.capMs = AIR_CAP_DEFAULT_MS;
  state_ = SystemState::Disarmed;  // Always disarmed at boot, without exception.

  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  lastRawReading_ = digitalRead(PIN_BOOT_BUTTON);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // arduino-esp32 core 3.x (IDF5): the task watchdog is already
  // initialized by the framework; reconfigure it with our timeout.
  esp_task_wdt_config_t wdtConfig;
  wdtConfig.timeout_ms = WATCHDOG_TIMEOUT_S * 1000;
  wdtConfig.idle_core_mask = 0;
  wdtConfig.trigger_panic = true;
  esp_task_wdt_reconfigure(&wdtConfig);
  esp_task_wdt_add(NULL);
#else
  esp_task_wdt_init(WATCHDOG_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);
#endif
}

const char* SafetyManager::getStateName() const {
  switch (state_) {
    case SystemState::Disarmed: return "disarmed";
    case SystemState::Armed: return "armed";
    case SystemState::Estop: return "estop";
  }
  return "unknown";
}

bool SafetyManager::arm() {
  if (state_ == SystemState::Estop) return false;  // reArm() required, not arm()
  if (state_ == SystemState::Armed) return true;
  state_ = SystemState::Armed;
  fire(SafetyEvent::Type::Armed, "system armed");
  return true;
}

bool SafetyManager::disarm() {
  if (state_ == SystemState::Estop) return false;
  forceAllSafeInternal();
  state_ = SystemState::Disarmed;
  fire(SafetyEvent::Type::Disarmed, "system disarmed");
  return true;
}

void SafetyManager::estop(const char* reason) {
  forceAllSafeInternal();
  state_ = SystemState::Estop;
  fire(SafetyEvent::Type::Estop, reason ? reason : "emergency stop");
}

bool SafetyManager::reArm() {
  if (state_ != SystemState::Estop) return false;
  state_ = SystemState::Armed;
  fire(SafetyEvent::Type::Rearmed, "system re-armed after e-stop");
  return true;
}

void SafetyManager::forceAllSafeInternal() {
  relays().allSafe();
  rebreathCap_.active = false;
  airCap_.active = false;
}

bool SafetyManager::setRelay(RelayId id, bool energized) {
  // De-energizing (going to the safe state) is ALWAYS allowed, whatever
  // the system state. Energizing is only allowed when ARMED.
  if (energized && state_ != SystemState::Armed) {
    return false;
  }

  relays().setEnergized(id, energized);

  // Hard limit: starts/stops the timer as soon as the relay enters its
  // "active" state (Rebreath open = rebreathing; Air closed = no normal
  // air) — whatever the command source (REST, BLE).
  CapTimer* cap = nullptr;
  if (id == RelayId::Rebreath) cap = &rebreathCap_;
  else if (id == RelayId::Air) cap = &airCap_;
  if (cap) {
    if (energized) {
      cap->active = true;
      cap->deadlineMs = millis() + cap->capMs;
    } else {
      cap->active = false;
    }
  }

  fire(SafetyEvent::Type::RelayChanged, energized ? "relay energized" : "relay de-energized", id,
       true);
  return true;
}

bool SafetyManager::toggleRelay(RelayId id) {
  bool current = relays().isEnergized(id);
  return setRelay(id, !current);
}

void SafetyManager::setRebreathCapMs(uint32_t ms) {
  if (ms < REBREATH_CAP_MIN_MS) ms = REBREATH_CAP_MIN_MS;
  if (ms > REBREATH_CAP_MAX_MS) ms = REBREATH_CAP_MAX_MS;
  rebreathCap_.capMs = ms;
}

void SafetyManager::setAirCapMs(uint32_t ms) {
  if (ms < AIR_CAP_MIN_MS) ms = AIR_CAP_MIN_MS;
  if (ms > AIR_CAP_MAX_MS) ms = AIR_CAP_MAX_MS;
  airCap_.capMs = ms;
}

uint32_t SafetyManager::remainingMs(const CapTimer& t) {
  if (!t.active) return 0;
  // Signed comparison to handle millis() rollover correctly.
  int32_t remaining = static_cast<int32_t>(t.deadlineMs - millis());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

uint32_t SafetyManager::getRebreathRemainingMs() const { return remainingMs(rebreathCap_); }
uint32_t SafetyManager::getAirRemainingMs() const { return remainingMs(airCap_); }

void SafetyManager::onEvent(SafetyEventHandler handler) { eventHandlers_.push_back(handler); }

void SafetyManager::onShortPress(ShortPressHandler handler) {
  shortPressHandlers_.push_back(handler);
}

void SafetyManager::fire(SafetyEvent::Type type, const char* message, RelayId relay,
                          bool relayValid) {
  SafetyEvent evt{type, message, relay, relayValid};
  for (auto& handler : eventHandlers_) {
    handler(evt);
  }
}

void SafetyManager::pollButton() {
  uint32_t now = millis();
  int raw = digitalRead(PIN_BOOT_BUTTON);  // pressed = LOW (pull-up)

  if (raw != lastRawReading_) {
    lastDebounceMs_ = now;
    lastRawReading_ = raw;
  }

  if ((now - lastDebounceMs_) > BUTTON_DEBOUNCE_MS) {
    bool pressedNow = (raw == LOW);

    if (pressedNow && !buttonPressed_) {
      // Falling edge: press starts.
      buttonPressed_ = true;
      buttonPressStartMs_ = now;
      buttonLongFired_ = false;
    } else if (pressedNow && buttonPressed_ && !buttonLongFired_) {
      if (now - buttonPressStartMs_ >= BUTTON_LONG_PRESS_MS) {
        buttonLongFired_ = true;
        if (state_ == SystemState::Estop) {
          reArm();
        } else {
          estop("boot button held");
        }
      }
    } else if (!pressedNow && buttonPressed_) {
      // Release.
      if (!buttonLongFired_) {
        // Short press -> cycle the display.
        for (auto& handler : shortPressHandlers_) handler();
      }
      buttonPressed_ = false;
    }
  }
}

void SafetyManager::checkCaps() {
  if (rebreathCap_.active && remainingMs(rebreathCap_) == 0) {
    relays().setEnergized(RelayId::Rebreath, false);
    rebreathCap_.active = false;
    fire(SafetyEvent::Type::SafetyCapTriggered, "rebreath cap reached, forced closed",
         RelayId::Rebreath, true);
  }
  if (airCap_.active && remainingMs(airCap_) == 0) {
    relays().setEnergized(RelayId::Air, false);
    airCap_.active = false;
    fire(SafetyEvent::Type::SafetyCapTriggered, "air cap reached, forced open", RelayId::Air,
         true);
  }
}

void SafetyManager::tick() {
  pollButton();
  checkCaps();
  esp_task_wdt_reset();
}

#include "RelayController.h"
#include "Config.h"

namespace {

struct RelayDef {
  uint8_t pin;
  ValveType type;
  const char* name;
  const char* onLabel;
  const char* offLabel;
};

// Order = RelayId order (Air, Rebreath, Aroma).
// onLabel/offLabel match the LOGICAL state (logicalOn == true/false),
// not the physical "energized" state — see getLogicalState().
static const RelayDef kRelayDefs[RelayController::kCount] = {
    {PIN_RELAY_AIR, ValveType::NormallyOpen, "air", "OPEN", "CLOSED"},
    {PIN_RELAY_REBREATH, ValveType::NormallyClosed, "rebreath", "OPEN", "CLOSED"},
    {PIN_RELAY_AROMA, ValveType::Pump, "aroma", "RUNNING", "STOPPED"},
};

inline uint8_t gpioLevelFor(bool energized) {
  // RELAY_ACTIVE_LOW == true  -> energized = LOW level
  // RELAY_ACTIVE_LOW == false -> energized = HIGH level
  bool high = RELAY_ACTIVE_LOW ? !energized : energized;
  return high ? HIGH : LOW;
}

}  // namespace

void RelayController::begin() {
  // Safety first: force the de-energized state BEFORE even knowing whether
  // the rest of the firmware will start up correctly.
  for (size_t i = 0; i < kCount; ++i) {
    energized_[i] = false;
    pinMode(kRelayDefs[i].pin, OUTPUT);
    digitalWrite(kRelayDefs[i].pin, gpioLevelFor(false));
  }
}

void RelayController::setEnergized(RelayId id, bool energized) {
  int8_t i = indexOf(id);
  if (i < 0 || static_cast<size_t>(i) >= kCount) return;
  energized_[i] = energized;
  digitalWrite(kRelayDefs[i].pin, gpioLevelFor(energized));
}

bool RelayController::isEnergized(RelayId id) const {
  int8_t i = indexOf(id);
  if (i < 0 || static_cast<size_t>(i) >= kCount) return false;
  return energized_[i];
}

RelayLogicalState RelayController::getLogicalState(RelayId id) const {
  int8_t i = indexOf(id);
  RelayLogicalState st{};
  if (i < 0 || static_cast<size_t>(i) >= kCount) return st;

  const RelayDef& def = kRelayDefs[i];
  bool energized = energized_[i];
  bool logicalOn;
  switch (def.type) {
    case ValveType::NormallyOpen:
      // NO: de-energized = open -> "logicalOn" (open) = !energized
      logicalOn = !energized;
      break;
    case ValveType::NormallyClosed:
      // NC: de-energized = closed -> "logicalOn" (open) = energized
      logicalOn = energized;
      break;
    case ValveType::Pump:
    default:
      logicalOn = energized;
      break;
  }

  st.energized = energized;
  st.logicalOn = logicalOn;
  st.name = def.name;
  st.logicalOnLabel = def.onLabel;
  st.logicalOffLabel = def.offLabel;
  return st;
}

ValveType RelayController::getType(RelayId id) const {
  int8_t i = indexOf(id);
  if (i < 0 || static_cast<size_t>(i) >= kCount) return ValveType::Pump;
  return kRelayDefs[i].type;
}

const char* RelayController::getName(RelayId id) const {
  int8_t i = indexOf(id);
  if (i < 0 || static_cast<size_t>(i) >= kCount) return "";
  return kRelayDefs[i].name;
}

uint8_t RelayController::getPin(RelayId id) const {
  int8_t i = indexOf(id);
  if (i < 0 || static_cast<size_t>(i) >= kCount) return 0;
  return kRelayDefs[i].pin;
}

void RelayController::allSafe() {
  for (size_t i = 0; i < kCount; ++i) {
    energized_[i] = false;
    digitalWrite(kRelayDefs[i].pin, gpioLevelFor(false));
  }
}

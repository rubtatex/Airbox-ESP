#include "SequenceEngine.h"

namespace {
struct Lock {
  explicit Lock(SemaphoreHandle_t m) : m_(m) { if (m_) xSemaphoreTakeRecursive(m_, portMAX_DELAY); }
  ~Lock() { if (m_) xSemaphoreGiveRecursive(m_); }
  SemaphoreHandle_t m_;
};
}  // namespace

void SequenceEngine::begin(SafetyManager& safety) {
  safety_ = &safety;
  mutex_ = xSemaphoreCreateRecursiveMutex();

  core_.begin(
      [this](uint8_t relay, bool energized) {
        return safety_->setRelay(static_cast<RelayId>(relay), energized);
      },
      [this](seq::Notice notice, const char* detail) {
        for (auto& listener : listeners_) listener(notice, detail);
      });

  // Emergency stop and disarm already force every relay to the safe state:
  // the sequence just has to end.
  safety_->onEvent([this](const SafetyEvent& evt) {
    if (evt.type == SafetyEvent::Type::Estop) {
      Lock lock(mutex_);
      core_.terminated("emergency stop");
    } else if (evt.type == SafetyEvent::Type::Disarmed) {
      Lock lock(mutex_);
      core_.terminated("system disarmed");
    }
  });
}

void SequenceEngine::tick() {
  Lock lock(mutex_);
  core_.tick(millis());
}

bool SequenceEngine::upload(size_t offset, size_t total, const uint8_t* data, size_t length,
                            String& error) {
  Lock lock(mutex_);
  const char* message = nullptr;
  bool ok = core_.upload(offset, total, data, length, message);
  if (!ok) error = message ? message : "upload failed";
  return ok;
}

bool SequenceEngine::start(uint32_t holdMs, String& error) {
  Lock lock(mutex_);
  const char* message = nullptr;
  bool armed = safety_->getState() == SystemState::Armed;
  bool ok = core_.start(millis(), holdMs, armed, message);
  if (!ok) error = message ? message : "cannot start";
  return ok;
}

void SequenceEngine::stop(const char* reason) {
  Lock lock(mutex_);
  core_.abort(reason);
}

void SequenceEngine::touch() {
  Lock lock(mutex_);
  core_.touch(millis());
}

void SequenceEngine::onNotice(Listener listener) { listeners_.push_back(std::move(listener)); }

seq::State SequenceEngine::state() {
  Lock lock(mutex_);
  return core_.state();
}

bool SequenceEngine::describe(JsonObject out) {
  Lock lock(mutex_);
  seq::State st = core_.state();
  const char* error = core_.lastError();
  if (st == seq::State::Empty && !error) return false;
  out["state"] = core_.stateName();
  if (st != seq::State::Empty) {
    out["events"] = core_.eventCount();
    out["totalMs"] = core_.totalMs();
  }
  if (st == seq::State::Running) {
    // Rounded to 100 ms: it only changes the STATE text ten times a second at most.
    out["elapsedMs"] = (core_.elapsedMs(millis()) / 100) * 100;
    out["holdMs"] = core_.holdMs();
  }
  if (error) out["error"] = error;
  return true;
}

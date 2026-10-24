#pragma once

// =============================================================================
// SequenceCore — the state machine that plays a sequence on the Airbox.
//
// Plain C++ with injected time and relay access, so it is unit-tested on a
// computer (test/host/). The Arduino-side SequenceEngine only wires it to
// millis() and SafetyManager.
//
// Safety rules enforced here:
//  - a sequence only starts when the system is ARMED;
//  - every relay change goes through the injected `setRelay`, which is
//    SafetyManager::setRelay(): it keeps refusing to energize outside ARMED
//    and the Re-Breath / Air caps keep applying. If it ever refuses, the
//    sequence aborts and every relay is de-energized;
//  - the sequence ends (completed, stopped, aborted) with ALL relays
//    de-energized;
//  - if the app stays silent for `holdMs`, the sequence is aborted. Short
//    drop-outs are absorbed, a real loss of contact is not left running;
//  - estop / disarm end the sequence (SafetyManager already forced the safe
//    state, see terminated());
//  - nothing is persisted: a reboot forgets any sequence and comes back
//    DISARMED.
// =============================================================================

#include <functional>
#include <utility>
#include "SequenceTimeline.h"

namespace seq {

constexpr uint32_t kHoldDefaultMs = 30000;
constexpr uint32_t kHoldMinMs = 5000;
constexpr uint32_t kHoldMaxMs = 600000;

enum class State : uint8_t { Empty, Loaded, Running };
enum class Notice : uint8_t { Loaded, Started, Completed, Aborted };

class Core {
 public:
  using SetRelay = std::function<bool(uint8_t relay, bool energized)>;
  using Notify = std::function<void(Notice, const char* detail)>;

  void begin(SetRelay setRelay, Notify notify) {
    setRelay_ = std::move(setRelay);
    notify_ = std::move(notify);
  }

  // --- Upload ---------------------------------------------------------------
  // The sequence is sent in order, in one or several pieces. `offset` 0 starts
  // a new upload. Returns false with `error` set on failure; the last piece
  // validates the whole sequence.
  bool upload(size_t offset, size_t total, const uint8_t* data, size_t length, const char*& error) {
    error = nullptr;
    if (state_ == State::Running) { error = "a sequence is running"; return false; }
    if (offset == 0) {
      uploading_ = false;
      received_ = 0;
      state_ = State::Empty;
      lastError_ = nullptr;
      if (total < kHeaderBytes) return fail("sequence data too short", error);
      if (total > kMaxBytes) return fail("sequence too large", error);
      uploadTotal_ = total;
      uploading_ = true;
    }
    if (!uploading_) return fail("no upload in progress", error);
    if (offset != received_ || total != uploadTotal_ || length > uploadTotal_ - received_) {
      uploading_ = false;
      return fail("upload out of order", error);
    }
    if (length > 0) memcpy(timeline_.buffer() + offset, data, length);
    received_ += length;
    if (received_ < uploadTotal_) return true;

    uploading_ = false;
    ParseResult result = timeline_.validate(uploadTotal_);
    if (result != ParseResult::Ok) return fail(describe(result), error);
    state_ = State::Loaded;
    if (notify_) notify_(Notice::Loaded, nullptr);
    return true;
  }

  // --- Control --------------------------------------------------------------
  bool start(uint32_t nowMs, uint32_t holdMs, bool armed, const char*& error) {
    error = nullptr;
    if (state_ == State::Running) { error = "a sequence is already running"; return false; }
    if (state_ != State::Loaded) return fail("no sequence loaded", error);
    if (!armed) return fail("refused: system not armed", error);
    if (holdMs < kHoldMinMs) holdMs = kHoldMinMs;
    if (holdMs > kHoldMaxMs) holdMs = kHoldMaxMs;
    holdMs_ = holdMs;
    lastError_ = nullptr;
    startMs_ = nowMs;
    lastContactMs_ = nowMs;
    next_ = 0;
    state_ = State::Running;
    if (notify_) notify_(Notice::Started, nullptr);
    tick(nowMs);  // events at t = 0 do not wait for the next loop
    return true;
  }

  // The app is still there. Any authenticated request counts as contact.
  void touch(uint32_t nowMs) { lastContactMs_ = nowMs; }

  // Stops on request or after a refusal: de-energizes everything.
  void abort(const char* reason) {
    if (state_ != State::Running) return;
    deenergizeAll();
    end(Notice::Aborted, reason);
  }

  // The system already forced the safe state (estop, disarm): just stop.
  void terminated(const char* reason) {
    if (state_ != State::Running) return;
    end(Notice::Aborted, reason);
  }

  void tick(uint32_t nowMs) {
    if (state_ != State::Running) return;
    if (nowMs - lastContactMs_ > holdMs_) { abort("no contact from the app"); return; }
    uint32_t elapsed = nowMs - startMs_;
    while (next_ < timeline_.count()) {
      Event e = timeline_.at(next_);
      if (elapsed < static_cast<uint32_t>(e.seconds) * 1000UL) break;
      if (!setRelay_ || !setRelay_(e.relay, e.energized)) { abort("a relay command was refused"); return; }
      ++next_;
    }
    if (elapsed >= timeline_.totalSeconds() * 1000UL && next_ >= timeline_.count()) {
      deenergizeAll();
      end(Notice::Completed, nullptr);
    }
  }

  // --- Status ---------------------------------------------------------------
  State state() const { return state_; }
  size_t eventCount() const { return state_ == State::Empty ? 0 : timeline_.count(); }
  uint32_t totalMs() const { return state_ == State::Empty ? 0 : timeline_.totalSeconds() * 1000UL; }
  uint32_t elapsedMs(uint32_t nowMs) const { return state_ == State::Running ? nowMs - startMs_ : 0; }
  uint32_t holdMs() const { return holdMs_; }
  const char* lastError() const { return lastError_; }
  const char* stateName() const {
    switch (state_) {
      case State::Empty: return "empty";
      case State::Loaded: return "loaded";
      case State::Running: return "running";
    }
    return "empty";
  }

 private:
  Timeline timeline_;
  SetRelay setRelay_;
  Notify notify_;
  State state_ = State::Empty;
  bool uploading_ = false;
  size_t uploadTotal_ = 0;
  size_t received_ = 0;
  uint32_t startMs_ = 0;
  uint32_t lastContactMs_ = 0;
  uint32_t holdMs_ = kHoldDefaultMs;
  size_t next_ = 0;
  const char* lastError_ = nullptr;

  bool fail(const char* message, const char*& error) {
    error = message;
    lastError_ = message;
    return false;
  }

  void deenergizeAll() {
    if (!setRelay_) return;
    for (uint8_t r = 0; r < kRelayCount; ++r) setRelay_(r, false);
  }

  void end(Notice notice, const char* detail) {
    state_ = State::Empty;  // a finished sequence is not kept: it must be sent again
    lastError_ = notice == Notice::Aborted ? detail : nullptr;
    if (notify_) notify_(notice, detail);
  }
};

}  // namespace seq

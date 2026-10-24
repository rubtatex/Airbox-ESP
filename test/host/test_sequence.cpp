// Host-side tests of the sequence format and state machine (no Arduino needed).
// Build and run: ./test/host/run.sh
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include "../../src/SequenceCore.h"

using namespace seq;

static int failures = 0;
#define CHECK(cond)                                                        \
  do {                                                                     \
    if (!(cond)) {                                                         \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
      ++failures;                                                          \
    }                                                                      \
  } while (0)

struct Ev { uint16_t t; uint8_t relay; bool on; };

static std::vector<uint8_t> encode(const std::vector<Ev>& events, uint32_t total) {
  std::vector<uint8_t> b = {'A', 'S', 1, 0, (uint8_t)(events.size() & 255), (uint8_t)(events.size() >> 8),
                            (uint8_t)total, (uint8_t)(total >> 8), (uint8_t)(total >> 16), (uint8_t)(total >> 24), 0, 0};
  for (const Ev& e : events) {
    b.push_back(e.t & 255);
    b.push_back(e.t >> 8);
    b.push_back((uint8_t)((e.relay << 1) | (e.on ? 1 : 0)));
  }
  uint16_t crc = crc16(b.data(), 10);
  crc = crc16(b.data() + kHeaderBytes, b.size() - kHeaderBytes, crc);
  b[10] = crc & 255;
  b[11] = crc >> 8;
  return b;
}

static std::vector<uint8_t> fromHex(const char* hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
    char pair[3] = {hex[i], hex[i + 1], 0};
    out.push_back((uint8_t)strtol(pair, nullptr, 16));
  }
  return out;
}

static ParseResult validate(const std::vector<uint8_t>& data, Timeline& t) {
  memcpy(t.buffer(), data.data(), data.size());
  return t.validate(data.size());
}

// Written independently in Python: Re-Breath + Aroma together for 20 s, then closed for 10 s.
static const char* kGolden = "4153010004001e000000940d000003000005140002140004";

static void testFormat() {
  Timeline t;
  CHECK(validate(fromHex(kGolden), t) == ParseResult::Ok);
  CHECK(t.count() == 4);
  CHECK(t.totalSeconds() == 30);
  CHECK(t.at(0).seconds == 0 && t.at(0).relay == 1 && t.at(0).energized);
  CHECK(t.at(1).relay == 2 && t.at(1).energized);
  CHECK(t.at(2).seconds == 20 && t.at(2).relay == 1 && !t.at(2).energized);
  CHECK(encode({{0, 1, true}, {0, 2, true}, {20, 1, false}, {20, 2, false}}, 30) == fromHex(kGolden));

  std::vector<uint8_t> bad = fromHex(kGolden);
  bad[13] ^= 1;  // flip a bit in an event
  CHECK(validate(bad, t) == ParseResult::BadCrc);
  CHECK(t.count() == 0);

  bad = fromHex(kGolden);
  bad[0] = 'X';
  CHECK(validate(bad, t) == ParseResult::BadMagic);
  bad = fromHex(kGolden);
  bad[2] = 2;
  CHECK(validate(bad, t) == ParseResult::BadVersion);
  bad = fromHex(kGolden);
  bad.pop_back();
  CHECK(validate(bad, t) == ParseResult::BadLength);
  CHECK(validate(std::vector<uint8_t>(5, 0), t) == ParseResult::TooShort);

  CHECK(validate(encode({{5, 0, true}, {2, 0, false}}, 10), t) == ParseResult::NotSorted);
  CHECK(validate(encode({{0, 3, true}}, 10), t) == ParseResult::BadRelay);
  CHECK(validate(encode({{20, 0, true}}, 10), t) == ParseResult::BadTotal);
  CHECK(validate(encode({}, 70000), t) == ParseResult::BadTotal);
  CHECK(validate(encode({}, 5), t) == ParseResult::Ok);
}

struct Harness {
  Core core;
  std::vector<std::string> log;  // "r<relay>=<0|1>" and notices, in order
  bool refuseEnergize = false;
  Harness() {
    core.begin(
        [this](uint8_t relay, bool on) {
          if (on && refuseEnergize) return false;
          log.push_back("r" + std::to_string(relay) + "=" + (on ? "1" : "0"));
          return true;
        },
        [this](Notice n, const char* detail) {
          static const char* names[] = {"loaded", "started", "completed", "aborted"};
          log.push_back(std::string(names[(int)n]) + (detail ? std::string(":") + detail : ""));
        });
  }
  bool load(const std::vector<uint8_t>& data, size_t piece = 0) {
    const char* err = nullptr;
    if (piece == 0) piece = data.size();
    bool ok = true;
    for (size_t off = 0; off < data.size(); off += piece) {
      size_t n = std::min(piece, data.size() - off);
      ok = core.upload(off, data.size(), data.data() + off, n, err);
      if (!ok) return false;
    }
    return ok;
  }
  bool start(uint32_t now, uint32_t hold = 30000, bool armed = true) {
    const char* err = nullptr;
    return core.start(now, hold, armed, err);
  }
};

static void testUpload() {
  Harness h;
  CHECK(h.load(fromHex(kGolden)));
  CHECK(h.core.state() == State::Loaded);
  CHECK(h.core.eventCount() == 4 && h.core.totalMs() == 30000);

  Harness pieces;  // same bytes sent in small pieces
  CHECK(pieces.load(fromHex(kGolden), 5));
  CHECK(pieces.core.state() == State::Loaded);

  Harness bad;
  std::vector<uint8_t> data = fromHex(kGolden);
  data[14] ^= 0x40;
  CHECK(!bad.load(data));
  CHECK(bad.core.state() == State::Empty);
  CHECK(bad.core.lastError() != nullptr);

  // Out of order piece is refused, and so is a continuation without a start.
  Harness order;
  const char* err = nullptr;
  data = fromHex(kGolden);
  CHECK(order.core.upload(0, data.size(), data.data(), 10, err));
  CHECK(!order.core.upload(12, data.size(), data.data() + 12, 12, err));
  CHECK(!order.core.upload(10, data.size(), data.data() + 10, 14, err));  // upload was dropped
  CHECK(order.core.state() == State::Empty);

  // Too large is refused up front.
  Harness big;
  uint8_t one = 0;
  CHECK(!big.core.upload(0, kMaxBytes + 1, &one, 1, err));
}

static void testPlayback() {
  Harness h;
  CHECK(h.load(fromHex(kGolden)));
  CHECK(!h.start(1000, 30000, /*armed=*/false));  // never starts disarmed
  CHECK(h.core.state() == State::Loaded);
  CHECK(h.start(1000));
  CHECK(h.core.state() == State::Running);
  CHECK((h.log == std::vector<std::string>{"loaded", "started", "r1=1", "r2=1"}));  // t = 0 events fire at once

  h.log.clear();
  h.core.touch(5000); h.core.tick(5000);
  h.core.touch(20999); h.core.tick(20999);   // 19.999 s: nothing yet
  CHECK(h.log.empty());
  h.core.tick(21000);                         // 20 s
  CHECK((h.log == std::vector<std::string>{"r1=0", "r2=0"}));
  CHECK(h.core.elapsedMs(21000) == 20000);

  h.log.clear();
  h.core.touch(30500); h.core.tick(30999);
  CHECK(h.core.state() == State::Running);
  h.core.tick(31000);                         // 30 s: end
  CHECK(h.core.state() == State::Empty);
  // Completion de-energizes every relay, then reports.
  CHECK((h.log == std::vector<std::string>{"r0=0", "r1=0", "r2=0", "completed"}));
}

static void testSafety() {
  // A refused energize aborts and de-energizes everything.
  Harness h;
  h.load(fromHex(kGolden));
  h.refuseEnergize = true;
  CHECK(h.start(0));
  CHECK(h.core.state() == State::Empty);
  CHECK(h.log.back().rfind("aborted", 0) == 0);

  // Silence from the app aborts after holdMs, short silences are absorbed.
  Harness d;
  d.load(fromHex(kGolden));
  CHECK(d.start(0, 10000));
  d.core.tick(9999);
  CHECK(d.core.state() == State::Running);
  d.core.touch(9999);
  d.core.tick(19999);                         // 10 s since the last contact: still ok
  CHECK(d.core.state() == State::Running);
  d.log.clear();
  d.core.tick(20000);                         // more than 10 s since contact
  CHECK(d.core.state() == State::Empty);
  CHECK((d.log == std::vector<std::string>{"r0=0", "r1=0", "r2=0", "aborted:no contact from the app"}));

  // hold is clamped
  Harness c;
  c.load(fromHex(kGolden));
  CHECK(c.start(0, 1));
  CHECK(c.core.holdMs() == kHoldMinMs);
  Harness c2;
  c2.load(fromHex(kGolden));
  CHECK(c2.start(0, 99999999));
  CHECK(c2.core.holdMs() == kHoldMaxMs);

  // Stopping: everything is de-energized, the sequence is gone.
  Harness s;
  s.load(fromHex(kGolden));
  s.start(0);
  s.log.clear();
  s.core.abort("stopped by the app");
  CHECK(s.core.state() == State::Empty);
  CHECK((s.log == std::vector<std::string>{"r0=0", "r1=0", "r2=0", "aborted:stopped by the app"}));

  // estop/disarm: the system already forced the safe state, no relay command is sent.
  Harness e;
  e.load(fromHex(kGolden));
  e.start(0);
  e.log.clear();
  e.core.terminated("estop");
  CHECK(e.core.state() == State::Empty);
  CHECK((e.log == std::vector<std::string>{"aborted:estop"}));

  // No upload while running; no second start; a finished sequence must be sent again.
  Harness r;
  r.load(fromHex(kGolden));
  r.start(0);
  const char* err = nullptr;
  std::vector<uint8_t> data = fromHex(kGolden);
  CHECK(!r.core.upload(0, data.size(), data.data(), data.size(), err));
  CHECK(!r.start(1));
  r.core.abort("x");
  CHECK(!r.start(2));
}

static void testClockRollover() {
  // millis() wraps every ~49 days: differences must stay correct across the wrap.
  Harness h;
  h.load(fromHex(kGolden));
  uint32_t start = 0xFFFFF000u;
  CHECK(h.start(start));
  h.log.clear();
  h.core.touch(start + 20000); h.core.tick(start + 20000);  // wraps past 2^32
  CHECK((h.log == std::vector<std::string>{"r1=0", "r2=0"}));
}

static void testEventsAtTheEnd() {
  // Actions at the very end (e.g. "close" at t = total) are applied before completion.
  Harness h;
  h.load(encode({{0, 1, true}, {5, 1, false}}, 5));
  h.start(0);
  h.log.clear();
  h.core.tick(5000);
  CHECK((h.log == std::vector<std::string>{"r1=0", "r0=0", "r1=0", "r2=0", "completed"}));
}

int main() {
  testFormat();
  testUpload();
  testPlayback();
  testSafety();
  testClockRollover();
  testEventsAtTheEnd();
  if (failures == 0) printf("all sequence tests passed\n");
  return failures == 0 ? 0 : 1;
}

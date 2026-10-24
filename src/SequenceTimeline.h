#pragma once

// =============================================================================
// SequenceTimeline — a sequence as the firmware plays it: a flat, time-sorted
// list of relay events, with the end time of the sequence.
//
// The app unrolls loops and draws random waits BEFORE sending, so the firmware
// never interprets a script: it just applies events at their time. This file
// is plain C++ (no Arduino API) so it can be unit-tested on a computer —
// see test/host/.
//
// Binary format (little-endian), documented for the app in API.md:
//   bytes 0-1   magic "AS"
//   byte  2     format version (1)
//   byte  3     reserved, 0
//   bytes 4-5   event count (u16)
//   bytes 6-9   total duration in seconds (u32): when the sequence ends
//   bytes 10-11 CRC-16/CCITT-FALSE of bytes 0-9 followed by all event bytes
//   then, per event (3 bytes):
//     u16  time in seconds since the start
//     u8   (relay << 1) | energized     relay: 0 air, 1 rebreath, 2 aroma
// "energized" is the coil state, like relay.set's `state`: Air energized =
// valve closed, Re-Breath energized = valve open, Aroma energized = running.
// =============================================================================

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace seq {

constexpr uint8_t kMagic0 = 'A';
constexpr uint8_t kMagic1 = 'S';
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderBytes = 12;
constexpr size_t kEventBytes = 3;
constexpr size_t kMaxEvents = 3000;
constexpr uint32_t kMaxTotalSeconds = 65535;  // events carry a u16 time
constexpr size_t kMaxBytes = kHeaderBytes + kMaxEvents * kEventBytes;
constexpr uint8_t kRelayCount = 3;

inline uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

struct Event {
  uint16_t seconds;
  uint8_t relay;
  bool energized;
};

enum class ParseResult : uint8_t {
  Ok,
  TooShort,
  BadMagic,
  BadVersion,
  TooManyEvents,
  BadLength,
  BadTotal,
  BadCrc,
  NotSorted,
  BadRelay,
};

inline const char* describe(ParseResult r) {
  switch (r) {
    case ParseResult::Ok: return "ok";
    case ParseResult::TooShort: return "sequence data too short";
    case ParseResult::BadMagic: return "not an Airbox sequence";
    case ParseResult::BadVersion: return "unsupported sequence format version";
    case ParseResult::TooManyEvents: return "too many events";
    case ParseResult::BadLength: return "sequence length does not match its event count";
    case ParseResult::BadTotal: return "invalid sequence duration";
    case ParseResult::BadCrc: return "sequence data corrupted (bad checksum)";
    case ParseResult::NotSorted: return "events are not in time order";
    case ParseResult::BadRelay: return "unknown relay in sequence";
  }
  return "invalid sequence";
}

// Owns the (single) buffer a sequence is received into, then reads events
// out of it. The buffer is filled by the upload code, then validate()d.
class Timeline {
 public:
  uint8_t* buffer() { return buf_; }
  const uint8_t* buffer() const { return buf_; }
  static constexpr size_t capacity() { return kMaxBytes; }

  // Checks the first `length` bytes of the buffer. On success the timeline is
  // ready to be read; on failure it is empty.
  ParseResult validate(size_t length) {
    count_ = 0;
    totalSeconds_ = 0;
    if (length < kHeaderBytes) return ParseResult::TooShort;
    if (buf_[0] != kMagic0 || buf_[1] != kMagic1) return ParseResult::BadMagic;
    if (buf_[2] != kVersion) return ParseResult::BadVersion;
    size_t count = static_cast<size_t>(buf_[4]) | (static_cast<size_t>(buf_[5]) << 8);
    uint32_t total = static_cast<uint32_t>(buf_[6]) | (static_cast<uint32_t>(buf_[7]) << 8) |
                     (static_cast<uint32_t>(buf_[8]) << 16) | (static_cast<uint32_t>(buf_[9]) << 24);
    if (count > kMaxEvents) return ParseResult::TooManyEvents;
    if (length != kHeaderBytes + count * kEventBytes) return ParseResult::BadLength;
    if (total > kMaxTotalSeconds) return ParseResult::BadTotal;

    uint16_t expected = static_cast<uint16_t>(buf_[10]) | (static_cast<uint16_t>(buf_[11]) << 8);
    uint16_t crc = crc16(buf_, 10);
    crc = crc16(buf_ + kHeaderBytes, count * kEventBytes, crc);
    if (crc != expected) return ParseResult::BadCrc;

    uint16_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
      const uint8_t* e = buf_ + kHeaderBytes + i * kEventBytes;
      uint16_t seconds = static_cast<uint16_t>(e[0]) | (static_cast<uint16_t>(e[1]) << 8);
      if (e[2] >> 1 >= kRelayCount) return ParseResult::BadRelay;
      if (seconds < previous) return ParseResult::NotSorted;
      if (seconds > total) return ParseResult::BadTotal;
      previous = seconds;
    }
    count_ = count;
    totalSeconds_ = total;
    return ParseResult::Ok;
  }

  size_t count() const { return count_; }
  uint32_t totalSeconds() const { return totalSeconds_; }

  Event at(size_t i) const {
    const uint8_t* e = buf_ + kHeaderBytes + i * kEventBytes;
    return Event{static_cast<uint16_t>(e[0] | (e[1] << 8)), static_cast<uint8_t>(e[2] >> 1),
                 static_cast<bool>(e[2] & 1)};
  }

 private:
  uint8_t buf_[kMaxBytes];
  size_t count_ = 0;
  uint32_t totalSeconds_ = 0;
};

}  // namespace seq

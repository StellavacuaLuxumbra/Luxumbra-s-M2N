#pragma once
// 测试公共工具: 手工构造 MIDI 字节

#include <cstdint>
#include <string>
#include <vector>

namespace m2n::test {

class MidiMaker {
 public:
  explicit MidiMaker(int division = 480) { header(0, 1, division); }

  void header(int format, int ntrks, int division) {
    bytes.clear();
    push("MThd");
    u32(6);
    u16(static_cast<uint16_t>(format));
    u16(static_cast<uint16_t>(ntrks));
    u16(static_cast<uint16_t>(division));
    lastTick_ = 0;
  }

  void beginTrack() {
    push("MTrk");
    lenPos_ = bytes.size();
    u32(0);
    lastTick_ = 0;
  }

  void endTrack() {
    delta(0);
    meta(0x2F, {});
    uint32_t len = static_cast<uint32_t>(bytes.size() - lenPos_ - 4);
    for (int i = 0; i < 4; ++i) bytes[lenPos_ + i] = static_cast<uint8_t>(len >> (8 * (3 - i)));
  }

  void noteOn(int tick, uint8_t ch, uint8_t pitch, uint8_t vel) {
    delta(tick - lastTick_);
    lastTick_ = tick;
    bytes.push_back(static_cast<uint8_t>(0x90 | ch));
    bytes.push_back(pitch);
    bytes.push_back(vel);
  }

  void noteOff(int tick, uint8_t ch, uint8_t pitch) {
    delta(tick - lastTick_);
    lastTick_ = tick;
    bytes.push_back(static_cast<uint8_t>(0x80 | ch));
    bytes.push_back(pitch);
    bytes.push_back(0);
  }

  void tempo(int tick, int usPerQuarter) {
    delta(tick - lastTick_);
    lastTick_ = tick;
    bytes.push_back(0xFF);
    bytes.push_back(0x51);
    bytes.push_back(3);
    bytes.push_back(static_cast<uint8_t>(usPerQuarter >> 16));
    bytes.push_back(static_cast<uint8_t>(usPerQuarter >> 8));
    bytes.push_back(static_cast<uint8_t>(usPerQuarter));
  }

  void program(uint8_t ch, uint8_t program) {
    delta(0);
    bytes.push_back(static_cast<uint8_t>(0xC0 | ch));
    bytes.push_back(program);
  }

  std::vector<uint8_t> bytes;

 private:
  void push(const char* s) {
    for (const char* p = s; *p; ++p) bytes.push_back(static_cast<uint8_t>(*p));
  }
  void u16(uint16_t v) {
    bytes.push_back(static_cast<uint8_t>(v >> 8));
    bytes.push_back(static_cast<uint8_t>(v));
  }
  void u32(uint32_t v) {
    for (int i = 3; i >= 0; --i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void meta(uint8_t type, std::initializer_list<uint8_t> data) {
    bytes.push_back(0xFF);
    bytes.push_back(type);
    bytes.push_back(static_cast<uint8_t>(data.size()));
    for (uint8_t d : data) bytes.push_back(d);
  }
  void delta(int v) {
    uint8_t buf[4];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    v >>= 7;
    while (v > 0) {
      buf[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
      v >>= 7;
    }
    for (int i = n - 1; i >= 0; --i) bytes.push_back(buf[i]);
  }

  size_t lenPos_ = 0;
  int lastTick_ = 0;
};

}  // namespace m2n::test

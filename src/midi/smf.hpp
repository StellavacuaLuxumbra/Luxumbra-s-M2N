#pragma once
// 标准 MIDI 文件 (SMF) 解析器
// 支持格式 0/1/2 (2 当作 1 处理), running status, tempo map, SMPTE 时间

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace m2n::midi {

// 解析出的原始音符 (时间已换算为秒)
struct RawNote {
  double startSec = 0;
  double durationSec = 0;
  uint8_t pitch = 0;    // 0-127
  uint8_t velocity = 0; // 1-127
  uint8_t program = 0;  // GM 音色 0-127 (音符开始时)
  uint8_t channel = 0;  // 0-15
  int trackIndex = 0;
  bool drum = false;    // channel 10 (下标 9)
};

struct TrackInfo {
  int index = 0;
  std::string name;
  int noteCount = 0;
};

struct MidiData {
  int format = 0;
  int division = 480;              // ticks per quarter note (metrical 时)
  bool smpte = false;              // SMPTE 时间
  double secPerTick = 0;           // SMPTE 时的固定换算率
  std::vector<RawNote> notes;      // 全部音符 (note-off 已配对)
  std::vector<TrackInfo> tracks;
  std::vector<std::pair<int, int>> tempoMap;  // (tick, usPerQuarter), 已按 tick 排序
  double durationSec = 0;          // 最后一个音符结束时间
  double firstNoteSec = -1;        // 首个音符开始时间 (-1 = 无音符)

  // tick -> 秒 (按 tempo map 积分)
  double tickToSec(int tick) const;
};

// 解析 .mid 文件字节; 失败抛 std::runtime_error
MidiData parseSmf(const uint8_t* data, size_t size);

}  // namespace m2n::midi

#pragma once
// 转换后的歌曲模型: 量化到游戏 tick 网格的音符盒歌曲

#include <cstdint>
#include <string>
#include <vector>

#include "model/instruments.hpp"

namespace m2n::model {

// 一条乐器轨里的一个音符
struct StepNote {
  int step = 0;           // 第几步, 游戏内 tick = step * gridTicks
  uint8_t nbNote = 0;     // 音符盒音高 0-24 (相对乐器基准音)
  int midiPitch = 0;      // 八度适配后的绝对 MIDI 音高
  uint8_t velocity = 100; // 1-127 (数据包播放用; 红石结构忽略)
  double durationSec = 0; // 原始时值 (音频渲染用)
};

// 一条乐器轨 (对应红石结构的一条泳道)
struct Lane {
  Instrument instrument = Instrument::Harp;
  std::string sourceDesc;      // 来源描述, 如 "GM 0: Acoustic Grand Piano" / "鼓组-小军鼓"
  int octaveShift = 0;         // 八度适配位移 (12 的倍数)
  int clampedNotes = 0;        // 超出 25 音范围被钳制的音符数
  std::vector<StepNote> notes; // 按 step 升序
};

struct Song {
  std::string name;        // 显示名 (来自文件名或 MIDI 轨道名)
  std::string id;          // 命名空间 id (安全字符)
  std::vector<Lane> lanes; // 按音符数降序
  int totalSteps = 0;      // 总步数
  int gridTicks = 2;       // 每步游戏 tick (20 tick = 1 秒)
  double durationSec = 0;
  int totalNotes = 0;

  int totalTicks() const { return totalSteps * gridTicks; }
};

}  // namespace m2n::model

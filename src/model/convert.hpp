#pragma once
// MIDI -> Song 转换: 时间量化, 乐器映射, 八度适配, 轨道合并

#include "midi/smf.hpp"
#include "model/song.hpp"

namespace m2n::model {

struct ConvertOptions {
  int gridTicks = 2;    // 每步游戏 tick 数 (速度: 每秒 20/gridTicks 步)
  int maxLanes = 16;    // 最大乐器轨数 (超出的小轨并入最少冲突轨或丢弃)
  bool trimLeadIn = false;  // 去掉开头空拍 (把首个音符对齐到 step 0)
};

// 转换过程中的警告 (八度钳制/轨道合并等), 由 CLI 汇总展示
struct ConvertWarning {
  std::string message;
};

struct ConvertResult {
  Song song;
  std::vector<ConvertWarning> warnings;
};

ConvertResult convert(const midi::MidiData& midi, ConvertOptions options);

}  // namespace m2n::model

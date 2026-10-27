#pragma once
// 内置合成器: 用振荡器+包络近似音符盒音色, 把 Song 渲染成立体声 PCM

#include <cstddef>
#include <vector>

#include "model/song.hpp"

namespace m2n::audio {

struct SynthOptions {
  double sampleRate = 44100;
  double tailSec = 2.0;    // 歌曲结束后额外混响尾
  double maxPeak = 0.9;    // 归一化峰值
};

// 返回交错立体声 float 样本 (左右成对), 取值 [0, maxPeak]
std::vector<float> renderSong(const model::Song& song, const SynthOptions& options = {});

}  // namespace m2n::audio

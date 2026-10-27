#pragma once
// 从本地 .minecraft 提取原版音符盒音色 (用户本机文件, 无版权分发问题)

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "model/instruments.hpp"
#include "model/song.hpp"

namespace m2n::audio {

// 单个解码后的样本 (单声道化)
struct DecodedSample {
  std::vector<float> samples;
  int sampleRate = 44100;
};

// 在 vanillaDir (如 .minecraft) 下找最新的资源索引并解码音符盒音色.
// 返回: 乐器 -> 样本; 找不到的乐器不在 map 中.
// vanillaDir 为空时自动探测常见安装位置.
std::map<model::Instrument, DecodedSample> loadVanillaNoteSamples(
    const std::string& vanillaDir = {});

// 解码 ogg 字节 (stb_vorbis); 失败返回 nullopt
std::optional<DecodedSample> decodeOgg(const uint8_t* data, size_t size);

// 用原版样本渲染歌曲: 每个音符按采样率倍速重采样播放.
// samples 缺失的乐器回退到内置合成器.
std::vector<float> renderSongWithSamples(const model::Song& song,
                                         const std::map<model::Instrument, DecodedSample>& samples,
                                         double sampleRate = 44100);

}  // namespace m2n::audio

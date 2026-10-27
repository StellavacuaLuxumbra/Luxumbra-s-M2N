#pragma once
// Ogg Vorbis 编码 (libvorbis)

#include <cstdint>
#include <vector>

namespace m2n::audio {

// interleaved: 交错立体声样本 (左右成对), frameCount = 帧数 (样本对数)
// quality: VBR 质量 0.0-1.0 (-0.1 为最小)
std::vector<uint8_t> encodeOggVorbis(const float* interleaved, size_t frameCount, int sampleRate,
                                     float quality = 0.5f);

}  // namespace m2n::audio

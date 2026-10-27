#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>

#include "audio/ogg_encoder.hpp"
#include "audio/synth.hpp"
#include "audio/vanilla_assets.hpp"
#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "test_util.hpp"

using namespace m2n;
using namespace m2n::audio;
namespace fs = std::filesystem;

namespace {

model::Song makeSong() {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 500000);
  mk.program(0, 0);
  for (int i = 0; i < 6; ++i) {
    mk.noteOn(i * 240, 0, static_cast<uint8_t>(60 + (i % 4) * 3), 100);
    mk.noteOff(i * 240 + 200, 0, static_cast<uint8_t>(60 + (i % 4) * 3));
  }
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  auto song = model::convert(md, {.gridTicks = 2}).song;
  song.id = "audiotest";
  song.name = "audio test";
  return song;
}

}  // namespace

TEST_CASE("合成器: 输出尺寸与幅度") {
  auto song = makeSong();
  SynthOptions opts;
  opts.sampleRate = 22050;  // 测试用低采样率
  auto pcm = renderSong(song, opts);

  size_t frames = pcm.size() / 2;
  double expectSec = song.durationSec + opts.tailSec;
  CHECK(frames >= static_cast<size_t>(expectSec * 0.9 * opts.sampleRate));
  CHECK(frames <= static_cast<size_t>((expectSec + 0.2) * opts.sampleRate));

  // 有声音: RMS > 0; 峰值 <= maxPeak
  double sumSq = 0, peak = 0;
  for (float s : pcm) {
    sumSq += static_cast<double>(s) * s;
    peak = std::max(peak, std::fabs(static_cast<double>(s)));
  }
  CHECK(sumSq / static_cast<double>(pcm.size()) > 1e-6);
  CHECK(peak <= opts.maxPeak + 1e-6);
  CHECK(peak > opts.maxPeak * 0.5);  // 归一化后接近峰值
}

#ifdef M2N_HAS_AUDIO
TEST_CASE("Ogg Vorbis 编码往返 (可被 stb_vorbis 解码)") {
  auto song = makeSong();
  SynthOptions opts;
  opts.sampleRate = 22050;
  auto pcm = renderSong(song, opts);

  auto ogg = encodeOggVorbis(pcm.data(), pcm.size() / 2, 22050, 0.3f);
  REQUIRE(ogg.size() > 100);
  // Ogg magic
  CHECK(ogg[0] == 'O');
  CHECK(ogg[1] == 'g');
  CHECK(ogg[2] == 'g');
  CHECK(ogg[3] == 'S');

  // 用 stb_vorbis 解码回来
  auto decoded = decodeOgg(ogg.data(), ogg.size());
  REQUIRE(decoded.has_value());
  CHECK(decoded->sampleRate == 22050);
  // 长度接近 (VBR 容差)
  size_t expectFrames = pcm.size() / 2;
  CHECK(decoded->samples.size() > expectFrames * 3 / 4);   // 解码为单声道混合
  CHECK(decoded->samples.size() < expectFrames * 5 / 4);
}
#endif

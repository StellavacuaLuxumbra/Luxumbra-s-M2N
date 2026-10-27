#include <doctest/doctest.h>

#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "model/instruments.hpp"
#include "test_util.hpp"

using namespace m2n;
using namespace m2n::model;

namespace {

// 构造简单歌曲: 钢琴 C4-E4-G4 分解和弦, 120BPM, 每 0.25s 一个音
Song makeTestSong() {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 500000);
  mk.program(0, 0);  // 钢琴 -> harp
  for (int i = 0; i < 3; ++i) {
    mk.noteOn(i * 240, 0, static_cast<uint8_t>(60 + i * 4), 100);
    mk.noteOff(i * 240 + 200, 0, static_cast<uint8_t>(60 + i * 4));
  }
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  ConvertOptions opts;
  opts.gridTicks = 2;  // 0.25s = 5 tick = 2.5 步 -> 量化
  return convert(md, opts).song;
}

}  // namespace

TEST_CASE("乐器映射表") {
  CHECK(mapGmProgram(0) == Instrument::Harp);       // 钢琴
  CHECK(mapGmProgram(33) == Instrument::Bass);      // 电子贝斯
  CHECK(mapGmProgram(73) == Instrument::Flute);     // 长笛
  CHECK(mapGmProgram(9) == Instrument::Bell);       // 钟琴
  CHECK(mapGmProgram(81) == Instrument::Bit);       // 锯齿波
  CHECK(mapDrumNote(36) == Instrument::BaseDrum);   // 底鼓
  CHECK(mapDrumNote(38) == Instrument::Snare);      // 军鼓
  CHECK(mapDrumNote(42) == Instrument::Hat);        // 闭镲
  CHECK(mapDrumNote(56) == Instrument::CowBell);    // 牛铃
  CHECK(mapDrumNote(58) == Instrument::IronXylophone);  // 颤音滑奏
}

TEST_CASE("乐器元数据") {
  CHECK(instrumentId(Instrument::IronXylophone) == "iron_xylophone");
  CHECK(instrumentBaseMidi(Instrument::Harp) == 54);
  CHECK(instrumentBaseMidi(Instrument::Bell) == 90);
  CHECK(instrumentBaseMidi(Instrument::Bass) == 30);
  CHECK(instrumentPitched(Instrument::Snare) == false);
  CHECK(instrumentPitched(Instrument::Bell) == true);
  // 下方材质与乐器一致 (双保险)
  CHECK(instrumentFloorBlock(Instrument::Bell) == "gold_block");
  CHECK(instrumentFloorBlock(Instrument::Chime) == "packed_ice");
  CHECK(instrumentFloorBlock(Instrument::Didgeridoo) == "pumpkin");
}

TEST_CASE("量化与八度适配") {
  auto song = makeTestSong();
  REQUIRE(song.lanes.size() == 1);
  const Lane& lane = song.lanes[0];
  CHECK(lane.instrument == Instrument::Harp);
  REQUIRE(lane.notes.size() == 3);

  // 音高 60,64,68 全在 harp 窗口 [54,78] 内 -> 无位移
  CHECK(lane.octaveShift == 0);
  CHECK(lane.notes[0].nbNote == 60 - 54);
  CHECK(lane.notes[1].nbNote == 64 - 54);
  CHECK(lane.notes[2].nbNote == 68 - 54);

  // 时间: 0s, 0.25s, 0.5s -> tick 0, 5, 10 -> step 0, 2.5->2或3, 5
  CHECK(lane.notes[0].step == 0);
  CHECK(lane.notes[2].step == 5);
  CHECK(song.totalSteps == 6);
}

TEST_CASE("高音轨八度下移") {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.program(0, 0);
  // C7-C8 区间 (96-108), harp 窗口 [54,78] -> 需要下移 1-2 个八度
  for (int i = 0; i < 4; ++i) {
    mk.noteOn(i * 240, 0, static_cast<uint8_t>(96 + i * 4), 100);
    mk.noteOff(i * 240 + 200, 0, static_cast<uint8_t>(96 + i * 4));
  }
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  auto conv = convert(md, {.gridTicks = 2});
  REQUIRE(conv.song.lanes.size() == 1);
  const Lane& lane = conv.song.lanes[0];
  // 96-108 (跨 12) 下移 24 -> 72-84? 或下移 12 -> 84-96 (超上界 18), 下移 24 -> 72-84 (超 6)
  // 实际最优: 108 为主 -> shift -24: 72..84, 超 78 的部分 6; shift -36: 60..72 全在界内!
  CHECK(lane.octaveShift % 12 == 0);
  // 所有音符映射后都在 0..24
  for (const auto& n : lane.notes) CHECK(n.nbNote <= 24);
  CHECK(lane.clampedNotes == 0);
}

TEST_CASE("鼓组分离到打击乐轨") {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 500000);
  mk.noteOn(0, 9, 36, 100);   // 底鼓
  mk.noteOff(120, 9, 36);
  mk.noteOn(240, 9, 38, 100); // 军鼓
  mk.noteOff(360, 9, 38);
  mk.noteOn(480, 9, 42, 100); // 闭镲
  mk.noteOff(600, 9, 42);
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  auto song = convert(md, {}).song;
  REQUIRE(song.lanes.size() == 3);
  CHECK(song.lanes[0].instrument == Instrument::BaseDrum);
  CHECK(song.lanes[1].instrument == Instrument::Snare);
  CHECK(song.lanes[2].instrument == Instrument::Hat);
}

TEST_CASE("同轨同音高同时音符去重") {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.program(0, 0);
  mk.noteOn(0, 0, 60, 100);
  mk.noteOn(0, 5, 60, 100);  // 不同通道同音高同时 -> 同乐器轨同一步同 nbNote
  mk.noteOff(240, 0, 60);
  mk.noteOff(240, 5, 60);
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  auto song = convert(md, {}).song;
  REQUIRE(song.lanes.size() == 1);
  CHECK(song.lanes[0].notes.size() == 1);
}

#include <doctest/doctest.h>

#include "midi/smf.hpp"
#include "test_util.hpp"

using namespace m2n;
using namespace m2n::midi;
using m2n::test::MidiMaker;

TEST_CASE("最小 MIDI: 单音符解析与时间换算") {
  MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 500000);  // 120 BPM
  mk.noteOn(0, 0, 60, 100);
  mk.noteOff(480, 0, 60);
  mk.endTrack();

  auto md = parseSmf(mk.bytes.data(), mk.bytes.size());
  CHECK(md.format == 0);
  CHECK(md.division == 480);
  REQUIRE(md.notes.size() == 1);
  // 480 ticks = 1 四分音符 = 0.5 秒 (120BPM)
  CHECK(md.notes[0].startSec == doctest::Approx(0.0));
  CHECK(md.notes[0].durationSec == doctest::Approx(0.5));
  CHECK(md.notes[0].pitch == 60);
  CHECK(md.notes[0].velocity == 100);
  CHECK(md.durationSec == doctest::Approx(0.5));
}

TEST_CASE("tempo 变化与多轨道") {
  MidiMaker mk(480);
  mk.header(1, 2, 480);
  // 轨道 0: tempo
  mk.beginTrack();
  mk.tempo(0, 500000);    // 0.5s per beat
  mk.tempo(480, 250000);  // 之后 0.25s per beat
  mk.endTrack();
  // 轨道 1: 音符
  mk.beginTrack();
  mk.noteOn(0, 0, 60, 80);
  mk.noteOff(480, 0, 60);
  mk.noteOn(480, 0, 64, 80);
  mk.noteOff(960, 0, 64);
  mk.endTrack();

  auto md = parseSmf(mk.bytes.data(), mk.bytes.size());
  REQUIRE(md.notes.size() == 2);
  CHECK(md.notes[0].startSec == doctest::Approx(0.0));
  CHECK(md.notes[0].durationSec == doctest::Approx(0.5));
  CHECK(md.notes[1].startSec == doctest::Approx(0.5));
  CHECK(md.notes[1].durationSec == doctest::Approx(0.25));  // 250000us -> 0.25s
}

TEST_CASE("program change / 鼓组通道 / note-on vel=0") {
  MidiMaker mk(480);
  mk.beginTrack();
  mk.program(0, 40);  // 小提琴
  mk.noteOn(0, 0, 60, 90);
  mk.noteOff(240, 0, 60);
  mk.noteOn(0, 9, 38, 100);
  mk.noteOff(240, 9, 38);
  mk.noteOn(480, 0, 67, 80);
  mk.noteOn(720, 0, 67, 0);  // vel=0 = note-off
  mk.endTrack();

  auto md = parseSmf(mk.bytes.data(), mk.bytes.size());
  REQUIRE(md.notes.size() == 3);
  CHECK(md.notes[0].program == 40);
  CHECK(md.notes[0].drum == false);
  CHECK(md.notes[1].drum == true);
  CHECK(md.notes[1].channel == 9);
  CHECK(md.notes[2].pitch == 67);
  CHECK(md.notes[2].durationSec == doctest::Approx(0.25));
}

TEST_CASE("坏文件报错") {
  std::vector<uint8_t> garbage = {1, 2, 3, 4, 5, 6};
  CHECK_THROWS(parseSmf(garbage.data(), garbage.size()));

  std::vector<uint8_t> truncated = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0};
  CHECK_THROWS(parseSmf(truncated.data(), truncated.size()));
}

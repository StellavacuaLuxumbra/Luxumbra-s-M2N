#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "nbt/nbt.hpp"
#include "structure/export_nbt.hpp"
#include "test_util.hpp"
#include "util/fs.hpp"
#include "util/zip.hpp"

using namespace m2n;
namespace fs = std::filesystem;

namespace {

// 生成一个多轨测试 MIDI 并写成临时 .mid 文件
fs::path writeTestMidi(const fs::path& dir) {
  test::MidiMaker mk(480);
  mk.header(1, 2, 480);
  mk.beginTrack();
  mk.tempo(0, 500000);
  mk.endTrack();
  mk.beginTrack();
  for (int i = 0; i < 16; ++i) {
    mk.noteOn(i * 240, 0, static_cast<uint8_t>(60 + (i % 5) * 2), 110);
    mk.noteOff(i * 240 + 180, 0, static_cast<uint8_t>(60 + (i % 5) * 2));
  }
  for (int i = 0; i < 8; ++i) {
    mk.noteOn(i * 480, 9, static_cast<uint8_t>(i % 2 ? 38 : 36), 100);
    mk.noteOff(i * 480 + 120, 9, static_cast<uint8_t>(i % 2 ? 38 : 36));
  }
  mk.endTrack();

  fs::create_directories(dir);
  fs::path file = dir / "e2e_test.mid";
  auto& bytes = mk.bytes;
  std::ofstream f(file, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return file;
}

}  // namespace

TEST_CASE("E2E: MIDI -> 红石结构 .nbt (gzip 合法, 大小合理)") {
  auto dir = fs::temp_directory_path() / ("m2n_e2e_" + std::to_string(std::rand()));
  auto midiFile = writeTestMidi(dir);

  // 走完整管线
  auto bytes = util::readFile(midiFile);
  auto md = midi::parseSmf(bytes.data(), bytes.size());
  auto conv = model::convert(md, {.gridTicks = 2});
  conv.song.id = "e2e";
  conv.song.name = "e2e";

  fs::path nbtFile = dir / "machine.nbt";
  auto r = structure::exportStructureNbt(conv.song, nbtFile, 3465, {});
  CHECK(fs::exists(nbtFile));
  CHECK(fs::file_size(nbtFile) > 100);
  CHECK(r.noteBlocks == conv.song.totalNotes);
  CHECK(r.sizeY == 3);

  // gzip 可解回且含 NBT 特征
  auto encoded = util::readFile(nbtFile);
  auto raw = util::gzipDecompress(encoded.data(), encoded.size());
  std::string s(raw.begin(), raw.end());
  CHECK(s.find("note_block") != std::string::npos);
  CHECK(s.find("repeater") != std::string::npos);
  CHECK(s.find("DataVersion") != std::string::npos);

  fs::remove_all(dir);
}

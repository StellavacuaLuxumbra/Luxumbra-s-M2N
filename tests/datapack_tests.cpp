#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "datapack/export.hpp"
#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "test_util.hpp"
#include "util/fs.hpp"
#include "util/mc_versions.hpp"

using namespace m2n;
namespace fs = std::filesystem;

namespace {

model::Song makeSong() {
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 400000);  // 150BPM
  for (int i = 0; i < 8; ++i) {
    mk.noteOn(i * 160, 0, static_cast<uint8_t>(60 + (i % 3) * 4), 100);
    mk.noteOff(i * 160 + 120, 0, static_cast<uint8_t>(60 + (i % 3) * 4));
    if (i % 2 == 0) {
      mk.noteOn(i * 160, 9, 38, 100);
      mk.noteOff(i * 160 + 60, 9, 38);
    }
  }
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  auto song = model::convert(md, {.gridTicks = 2}).song;
  song.id = "testsong";
  song.name = "测试歌曲";
  return song;
}

fs::path tempDir(const std::string& tag) {
  return fs::temp_directory_path() / (tag + "_" + std::to_string(std::rand()));
}

}  // namespace

TEST_CASE("数据包导出: 文件结构与内容") {
  auto song = makeSong();
  auto mc = util::findMcVersion("1.20.1");
  REQUIRE(mc.has_value());

  fs::path tmp = tempDir("m2n_test_dp");
  auto r = datapack::exportDatapack(song, tmp, *mc, {.loop = false, .particles = true});

  CHECK(fs::exists(tmp / "pack.mcmeta"));
  CHECK(fs::exists(tmp / "data" / "m2n_testsong" / "functions" / "tick.mcfunction"));
  CHECK(fs::exists(tmp / "data" / "m2n_testsong" / "functions" / "start.mcfunction"));
  CHECK(fs::exists(tmp / "data" / "m2n_testsong" / "functions" / "stop.mcfunction"));
  CHECK(fs::exists(tmp / "data" / "minecraft" / "tags" / "functions" / "tick.json"));

  auto tickJson = util::readTextFile(tmp / "data" / "minecraft" / "tags" / "functions" / "tick.json");
  CHECK(tickJson.find("m2n_testsong:tick") != std::string::npos);

  auto meta = util::readTextFile(tmp / "pack.mcmeta");
  CHECK(meta.find("\"pack_format\":15") != std::string::npos);

  auto tickFn = util::readTextFile(tmp / "data" / "m2n_testsong" / "functions" / "tick.mcfunction");
  // 第一个分片区间: 0..min(totalTicks, sliceTicks)-1 (短歌曲不占满整个分片)
  int firstT1 = std::min(song.totalTicks(), 100) - 1;
  CHECK(tickFn.find("matches 0.." + std::to_string(firstT1)) != std::string::npos);

  CHECK(r.functionCount >= 5);

  // 分片内容检查: 含 playsound 且音高参数位于 0.5..2.0
  bool sawPlaysound = false;
  for (const auto& e : fs::recursive_directory_iterator(tmp / "data" / "m2n_testsong" / "functions" / "tick")) {
    auto text = util::readTextFile(e.path());
    size_t pos = 0;
    while ((pos = text.find("playsound", pos)) != std::string::npos) {
      sawPlaysound = true;
      auto lineEnd = text.find('\n', pos);
      std::string line = text.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
      size_t lastSpace = line.rfind(' ');
      double pitch = std::atof(line.c_str() + lastSpace + 1);
      CHECK(pitch >= 0.5 - 1e-6);
      CHECK(pitch <= 2.0 + 1e-6);
      pos = lineEnd;
    }
  }
  CHECK(sawPlaysound);

  // 粒子命令
  bool sawParticle = false;
  for (const auto& e : fs::recursive_directory_iterator(tmp / "data" / "m2n_testsong" / "functions" / "tick"))
    if (util::readTextFile(e.path()).find("particle minecraft:note") != std::string::npos)
      sawParticle = true;
  CHECK(sawParticle);

  fs::remove_all(tmp);
}

TEST_CASE("1.21 目录命名 (function 而非 functions)") {
  auto song = makeSong();
  auto mc = util::findMcVersion("1.21.1");
  REQUIRE(mc.has_value());
  fs::path tmp = tempDir("m2n_test_dp21");
  datapack::exportDatapack(song, tmp, *mc, {});
  CHECK(fs::exists(tmp / "data" / "m2n_testsong" / "function" / "tick.mcfunction"));
  CHECK(fs::exists(tmp / "data" / "minecraft" / "tags" / "function" / "tick.json"));
  fs::remove_all(tmp);
}

TEST_CASE("循环模式生成回卷 on_end") {
  auto song = makeSong();
  auto mc = util::findMcVersion("1.20.1");
  fs::path tmp = tempDir("m2n_test_dploop");
  datapack::exportDatapack(song, tmp, *mc, {.loop = true});
  auto end = util::readTextFile(tmp / "data" / "m2n_testsong" / "functions" / "on_end.mcfunction");
  CHECK(end.find("$t m2n_testsong -1") != std::string::npos);
  fs::remove_all(tmp);
}

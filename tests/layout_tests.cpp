#include <doctest/doctest.h>

#include <map>
#include <set>
#include <tuple>

#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "structure/layout.hpp"
#include "test_util.hpp"

using namespace m2n;
using namespace m2n::structure;

namespace {

model::Song makeSong(int gridTicks = 2) {
  // 两条轨: 钢琴旋律 + 鼓, 共 8 步
  test::MidiMaker mk(480);
  mk.beginTrack();
  mk.tempo(0, 500000);  // 120BPM: 四分音符 0.5s = 10 tick
  // 钢琴: 步 0,2,4,6 (每步 0.2s? -> grid 2 时 0.1s/步; 这里给 240tick=0.25s 间隔)
  for (int i = 0; i < 4; ++i) {
    int t = i * 240;  // 0.25s
    mk.noteOn(t, 0, static_cast<uint8_t>(60 + i * 2), 100);
    mk.noteOff(t + 120, 0, static_cast<uint8_t>(60 + i * 2));
  }
  // 鼓
  for (int i = 0; i < 8; ++i) {
    mk.noteOn(i * 120, 9, 36, 100);
    mk.noteOff(i * 120 + 60, 9, 36);
  }
  mk.endTrack();
  auto md = midi::parseSmf(mk.bytes.data(), mk.bytes.size());
  return model::convert(md, {.gridTicks = gridTicks}).song;
}

}  // namespace

TEST_CASE("布局不变量: 无重叠 / 界内 / 音符盒数量") {
  auto song = makeSong(2);
  auto layout = buildMachine(song, {});

  REQUIRE(layout.tmpl.sizeX > 0);
  CHECK(layout.tmpl.sizeY == 3);  // 地板 + 结构层 + 总线层

  // 无重叠, 且都在界内
  std::set<std::tuple<int, int, int>> seen;
  for (const auto& b : layout.tmpl.blocks) {
    auto key = std::make_tuple(b.x, b.y, b.z);
    CHECK(seen.insert(key).second);  // 不重复
    CHECK(b.x >= 0);
    CHECK(b.y >= 0);
    CHECK(b.z >= 0);
    CHECK(b.x < layout.tmpl.sizeX);
    CHECK(b.y < layout.tmpl.sizeY);
    CHECK(b.z < layout.tmpl.sizeZ);
  }

  // 音符盒数 = 歌曲音符数 (转换已去重)
  CHECK(layout.noteBlockCount == song.totalNotes);
}

TEST_CASE("布局不变量: 每条泳道中继器延迟和 = 前缀 + 步数*grid") {
  auto song = makeSong(3);
  auto layout = buildMachine(song, {});

  REQUIRE(layout.laneBusZ.size() == song.lanes.size());
  // 按泳道统计中继器总延迟
  // 世界坐标: 结构坐标 = 世界坐标 - min。xMin=-2, yMin=-1 (地板), zMin=0 (zFirst-1=0)
  // → x 偏移 +2, y 偏移 +1, z 不偏移; 总线层世界 y=1 → 结构 y=2
  for (size_t i = 0; i < song.lanes.size(); ++i) {
    int busZ = layout.laneBusZ[i];
    int totalDelay = 0;
    for (const auto& b : layout.tmpl.blocks) {
      if (b.name == "minecraft:repeater" && b.z == busZ && b.y == 2) {
        totalDelay += std::stoi(b.properties.at("delay"));
      }
    }
    int expected = layout.lanePrefixDelay[i] + song.totalSteps * song.gridTicks;
    CHECK(totalDelay == expected);
  }
}

TEST_CASE("布局不变量: 音符盒乐器与音高, 下方材质匹配") {
  auto song = makeSong(2);
  auto layout = buildMachine(song, {.decorativeFloor = true});

  for (const auto& b : layout.tmpl.blocks) {
    if (b.name != "minecraft:note_block") continue;
    int note = std::stoi(b.properties.at("note"));
    CHECK(note >= 0);
    CHECK(note <= 24);
    auto ins = model::instrumentFromId(b.properties.at("instrument"));
    REQUIRE(ins.has_value());
    // 下方 (y-1) 必须是对应乐器材质
    std::string expectFloor =
        std::string("minecraft:") + std::string(model::instrumentFloorBlock(*ins));
    bool found = false;
    for (const auto& f : layout.tmpl.blocks) {
      if (f.x == b.x && f.y == b.y - 1 && f.z == b.z) {
        CHECK(f.name == expectFloor);
        found = true;
      }
    }
    CHECK(found);
  }
}

TEST_CASE("红石粉连接状态正确 (与相邻导体互连, 不连接孤立方向)") {
  auto song = makeSong(2);
  auto layout = buildMachine(song, {});

  // 建索引
  auto at = [&](int x, int y, int z) -> const nbt::StructureBlock* {
    for (const auto& b : layout.tmpl.blocks)
      if (b.x == x && b.y == y && b.z == z) return &b;
    return nullptr;
  };
  auto isWireOrRepeater = [&](int x, int y, int z) {
    const auto* b = at(x, y, z);
    return b && (b->name == "minecraft:redstone_wire" || b->name == "minecraft:repeater");
  };

  for (const auto& b : layout.tmpl.blocks) {
    if (b.name != "minecraft:redstone_wire") continue;
    CHECK(b.properties.count("east"));
    CHECK(b.properties.count("west"));
    CHECK(b.properties.count("north"));
    CHECK(b.properties.count("south"));
    CHECK(b.properties.at("east") == (isWireOrRepeater(b.x + 1, b.y, b.z) ? "side" : "none"));
    CHECK(b.properties.at("west") == (isWireOrRepeater(b.x - 1, b.y, b.z) ? "side" : "none"));
    CHECK(b.properties.at("north") == (isWireOrRepeater(b.x, b.y, b.z - 1) ? "side" : "none"));
    CHECK(b.properties.at("south") == (isWireOrRepeater(b.x, b.y, b.z + 1) ? "side" : "none"));
  }
}

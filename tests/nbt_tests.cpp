#include <doctest/doctest.h>

#include "nbt/nbt.hpp"
#include "util/zip.hpp"

using namespace m2n;
using namespace m2n::nbt;

TEST_CASE("gzip 压缩往返") {
  std::string text = "Hello, Minecraft! 你好,红石音乐! 0123456789";
  std::vector<uint8_t> raw(text.begin(), text.end());
  auto compressed = util::gzipCompress(raw.data(), raw.size());
  // gzip magic
  REQUIRE(compressed.size() > 10);
  CHECK(compressed[0] == 0x1F);
  CHECK(compressed[1] == 0x8B);
  auto restored = util::gzipDecompress(compressed.data(), compressed.size());
  CHECK(restored == raw);
}

TEST_CASE("NBT Writer 基本结构") {
  Writer w;
  w.beginRoot("");
  w.int32("DataVersion", 3465);
  w.listInt32("size", {3, 2, 1});
  w.emptyList("entities");
  w.beginListCompound("blocks");
  w.beginCompound();
  w.listInt32("pos", {0, 0, 0});
  w.int32("state", 0);
  w.endCompound();
  w.endList();
  w.string("Name", "stone");
  auto bytes = w.finish();

  // 根: TAG_Compound(10) + 名长 0
  REQUIRE(bytes.size() > 4);
  CHECK(bytes[0] == 10);
  CHECK(bytes[1] == 0);
  CHECK(bytes[2] == 0);

  // 手工解剖一个已知片段: "DataVersion" 前应有 TAG_Int(3)
  std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  std::string needle = std::string("\x03", 1) + std::string("\x00\x0b", 2) + "DataVersion";
  CHECK(s.find(needle) != std::string::npos);
}

TEST_CASE("结构模板编码 (gzip 可解回)") {
  StructureTemplate tmpl;
  tmpl.sizeX = 4;
  tmpl.sizeY = 3;
  tmpl.sizeZ = 2;
  tmpl.dataVersion = 3465;
  tmpl.blocks.push_back({0, 0, 0, "minecraft:note_block", {{"instrument", "harp"}, {"note", "3"}}});
  tmpl.blocks.push_back({1, 1, 1, "minecraft:redstone_wire", {}});
  tmpl.blocks.push_back({1, 1, 1, "minecraft:repeater", {{"delay", "2"}}});  // 故意同位置? 不行, 改掉
  tmpl.blocks.pop_back();
  tmpl.blocks.push_back({2, 1, 1, "minecraft:repeater", {{"delay", "2"}}});

  auto encoded = encodeStructure(tmpl);
  CHECK(encoded.size() > 20);
  CHECK(encoded[0] == 0x1F);
  CHECK(encoded[1] == 0x8B);
  // 能解压回来
  auto raw = util::gzipDecompress(encoded.data(), encoded.size());
  CHECK(raw.size() > 50);
  // palette 去重后应有 3 种方块; 检查 NBT 里有 palette 字样
  std::string s(raw.begin(), raw.end());
  CHECK(s.find("palette") != std::string::npos);
  CHECK(s.find("note_block") != std::string::npos);
  CHECK(s.find("DataVersion") != std::string::npos);
}

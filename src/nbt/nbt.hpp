#pragma once
// Java 版 NBT (Named Binary Tag) 写入器 + 结构方块 (Structure Template) 格式输出

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace m2n::nbt {

// 二进制 NBT 写入器 (大端)
// 用法:
//   Writer w;
//   w.beginRoot("");
//     w.int32("DataVersion", 3465);
//     w.beginListCompound("blocks");
//       w.beginCompound(); ... w.endCompound();
//     w.endList();
//   w.endRoot();  // 或 finish()
class Writer {
 public:
  // ---- 根 ----
  void beginRoot(std::string_view name);
  std::vector<uint8_t> finish();  // 结束根 compound 并返回字节 (gzip 前)

  // ---- compound 内标量 ----
  void int32(std::string_view name, int32_t v);
  void int64(std::string_view name, int64_t v);
  void string(std::string_view name, std::string_view v);

  // int 列表 (结构里的 size / pos)
  void listInt32(std::string_view name, const std::vector<int32_t>& values);
  // 空列表 (entities)
  void emptyList(std::string_view name);

  // ---- compound 列表 ----
  void beginListCompound(std::string_view name);
  void beginCompound();  // 列表内的匿名 compound
  void endCompound();
  void endList();

  // ---- 有名嵌套 compound (如 palette 里的 Properties) ----
  void beginNamedCompound(std::string_view name);

 private:
  void checkInCompound(const char* what);
  void checkInCompoundList(const char* what);
  void writeTagHeader(uint8_t type, std::string_view name);
  void u8_(uint8_t v);
  void i16_(int16_t v);
  void i32_(int32_t v);
  void i64_(int64_t v);
  void str_(std::string_view v);

  std::vector<uint8_t> buf_;
  // 容器栈: true = compound, false = list
  std::vector<bool> stack_;
  std::vector<size_t> sizeListLens_;   // 待回填的列表长度偏移
  std::vector<int32_t> listCounters_;  // 每层列表元素计数
};

// 结构模板 NBT 的数据模型
struct StructureBlock {
  int32_t x = 0, y = 0, z = 0;
  std::string name;  // 如 "minecraft:note_block"
  std::map<std::string, std::string> properties;  // 如 {"instrument","harp"},{"note","3"}
};

struct StructureTemplate {
  int32_t sizeX = 0, sizeY = 0, sizeZ = 0;
  std::vector<StructureBlock> blocks;
  int32_t dataVersion = 3465;
};

// 生成 gzip 压缩的结构 .nbt 字节
std::vector<uint8_t> encodeStructure(const StructureTemplate& tmpl);

}  // namespace m2n::nbt

#include "nbt/nbt.hpp"

#include <stdexcept>

#include "util/zip.hpp"

namespace m2n::nbt {

namespace {
constexpr uint8_t kTagEnd = 0, kTagByte = 1, kTagInt = 3, kTagLong = 4,
                    kTagString = 8, kTagList = 9, kTagCompound = 10;
}

void Writer::u8_(uint8_t v) { buf_.push_back(v); }
void Writer::i16_(int16_t v) {
  u8_(static_cast<uint8_t>(v >> 8));
  u8_(static_cast<uint8_t>(v));
}
void Writer::i32_(int32_t v) {
  for (int i = 3; i >= 0; --i) u8_(static_cast<uint8_t>(v >> (8 * i)));
}
void Writer::i64_(int64_t v) {
  for (int i = 7; i >= 0; --i) u8_(static_cast<uint8_t>(v >> (8 * i)));
}
void Writer::str_(std::string_view v) {
  i16_(static_cast<int16_t>(v.size()));
  for (char c : v) u8_(static_cast<uint8_t>(c));
}

void Writer::writeTagHeader(uint8_t type, std::string_view name) {
  u8_(type);
  str_(name);
}

void Writer::checkInCompound(const char* what) {
  if (stack_.empty() || !stack_.back())
    throw std::runtime_error(std::string("NBT 写入错误 (期望在 compound 内): ") + what);
}

void Writer::checkInCompoundList(const char* what) {
  // 栈约定: true=当前在 compound 内, false=当前在 (compound) 列表内
  // beginCompound 要求: 父容器是 compound (true) 且当前处于列表上下文 (false)
  if (stack_.size() < 2 || !stack_[stack_.size() - 2] || stack_.back())
    throw std::runtime_error(std::string("NBT 写入错误 (期望在列表内 compound 中): ") + what);
}

void Writer::beginRoot(std::string_view name) {
  if (!stack_.empty()) throw std::runtime_error("NFT beginRoot 嵌套");
  writeTagHeader(kTagCompound, name);
  stack_.push_back(true);
}

std::vector<uint8_t> Writer::finish() {
  if (stack_.empty() || !stack_.back()) throw std::runtime_error("NBT finish 状态错误");
  u8_(kTagEnd);
  stack_.pop_back();
  if (!stack_.empty()) throw std::runtime_error("NBT finish 时容器未闭合");
  return std::move(buf_);
}

void Writer::int32(std::string_view name, int32_t v) {
  checkInCompound("int32");
  writeTagHeader(kTagInt, name);
  i32_(v);
}

void Writer::int64(std::string_view name, int64_t v) {
  checkInCompound("int64");
  writeTagHeader(kTagLong, name);
  i64_(v);
}

void Writer::string(std::string_view name, std::string_view v) {
  checkInCompound("string");
  writeTagHeader(kTagString, name);
  str_(v);
}

void Writer::listInt32(std::string_view name, const std::vector<int32_t>& values) {
  checkInCompound("listInt32");
  writeTagHeader(kTagList, name);
  u8_(kTagInt);
  i32_(static_cast<int32_t>(values.size()));
  for (int32_t v : values) i32_(v);
}

void Writer::emptyList(std::string_view name) {
  checkInCompound("emptyList");
  writeTagHeader(kTagList, name);
  u8_(kTagEnd);
  i32_(0);
}

void Writer::beginListCompound(std::string_view name) {
  checkInCompound("beginListCompound");
  writeTagHeader(kTagList, name);
  u8_(kTagCompound);
  // 长度占位, endList 时回填
  sizeListLens_.push_back(buf_.size());
  i32_(0);
  stack_.push_back(false);
  listCounters_.push_back(0);
}

void Writer::beginCompound() {
  checkInCompoundList("beginCompound");
  u8_(kTagCompound);  // 列表元素无名字
  stack_.push_back(true);
  listCounters_.back()++;
}

void Writer::endCompound() {
  if (stack_.empty() || !stack_.back()) throw std::runtime_error("NBT endCompound 状态错误");
  u8_(kTagEnd);
  stack_.pop_back();
}

void Writer::endList() {
  if (stack_.empty() || stack_.back()) throw std::runtime_error("NBT endList 状态错误");
  stack_.pop_back();
  size_t offset = sizeListLens_.back();
  sizeListLens_.pop_back();
  int32_t count = listCounters_.back();
  listCounters_.pop_back();
  for (int i = 0; i < 4; ++i) buf_[offset + i] = static_cast<uint8_t>(count >> (8 * (3 - i)));
}

void Writer::beginNamedCompound(std::string_view name) {
  checkInCompound("beginNamedCompound");
  writeTagHeader(kTagCompound, name);
  stack_.push_back(true);
}

std::vector<uint8_t> encodeStructure(const StructureTemplate& tmpl) {
  // palette 去重
  std::map<std::pair<std::string, std::map<std::string, std::string>>, int32_t> paletteIndex;
  std::vector<StructureBlock> palette;  // 只用 name/properties

  Writer w;
  w.beginRoot("");

  w.listInt32("size", {tmpl.sizeX, tmpl.sizeY, tmpl.sizeZ});
  w.emptyList("entities");

  w.beginListCompound("blocks");
  for (const auto& b : tmpl.blocks) {
    auto key = std::make_pair(b.name, b.properties);
    auto it = paletteIndex.find(key);
    int32_t state;
    if (it == paletteIndex.end()) {
      state = static_cast<int32_t>(palette.size());
      paletteIndex.emplace(key, state);
      palette.push_back(b);
    } else {
      state = it->second;
    }
    w.beginCompound();
    w.listInt32("pos", {b.x, b.y, b.z});
    w.int32("state", state);
    w.endCompound();
  }
  w.endList();

  w.beginListCompound("palette");
  for (const auto& p : palette) {
    w.beginCompound();
    w.string("Name", p.name);
    if (!p.properties.empty()) {
      w.beginNamedCompound("Properties");
      for (const auto& [k, v] : p.properties) w.string(k, v);
      w.endCompound();
    }
    w.endCompound();
  }
  w.endList();

  w.int32("DataVersion", tmpl.dataVersion);
  auto bytes = w.finish();
  return util::gzipCompress(bytes.data(), bytes.size());
}

}  // namespace m2n::nbt

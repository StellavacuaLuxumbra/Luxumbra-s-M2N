#include "util/fs.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace m2n::util {

std::vector<uint8_t> readFile(const fs::path& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) throw std::runtime_error("无法打开文件: " + path.string());
  auto size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (size > 0) f.read(reinterpret_cast<char*>(data.data()), size);
  if (!f && !f.eof()) throw std::runtime_error("读取文件失败: " + path.string());
  return data;
}

std::string readTextFile(const fs::path& path) {
  auto data = readFile(path);
  return std::string(data.begin(), data.end());
}

void writeFile(const fs::path& path, const void* data, size_t size) {
  if (path.has_parent_path()) fs::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) throw std::runtime_error("无法写入文件: " + path.string());
  if (size > 0) f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}

void writeTextFile(const fs::path& path, std::string_view text) {
  writeFile(path, text.data(), text.size());
}

std::string sanitizeName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    if (ok) out += c;
  }
  // 非 ASCII (中文等) 会被丢弃; 若结果为空给一个默认名
  if (out.empty() || out == "." || out == "..") out = "song";
  // 命名空间 id 不能以数字开头
  if (out[0] >= '0' && out[0] <= '9') out = "song_" + out;
  return out;
}

std::string zipPath(const fs::path& dir, const fs::path& sub) {
  fs::path rel = fs::relative(sub, dir);
  std::string s = rel.generic_string();
  return s;
}

}  // namespace m2n::util

#pragma once
// 文件系统小工具: 读/写整个文件, 递归建目录, 枚举目录

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace m2n::util {

namespace fs = std::filesystem;

// 读整个文件; 失败抛 std::runtime_error
std::vector<uint8_t> readFile(const fs::path& path);

std::string readTextFile(const fs::path& path);

// 写文件 (自动创建父目录)
void writeFile(const fs::path& path, const void* data, size_t size);
void writeTextFile(const fs::path& path, std::string_view text);

// 安全文件名: 把不适合做文件名/命名空间 id 的字符替换掉
std::string sanitizeName(std::string_view name);

// 统一 / 分隔符的路径字符串 (用于 zip 内部路径)
std::string zipPath(const fs::path& dir, const fs::path& sub);

}  // namespace m2n::util

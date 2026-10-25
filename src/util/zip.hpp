#pragma once
// miniz 封装: gzip (结构 NBT 压缩) 与 zip 打包 (数据包/资源包)

#include <cstdint>
#include <filesystem>
#include <vector>

namespace m2n::util {

// gzip 压缩 (raw deflate + gzip 头/尾), 压缩等级 9
std::vector<uint8_t> gzipCompress(const uint8_t* data, size_t size);

// gzip 解压 (测试用)
std::vector<uint8_t> gzipDecompress(const uint8_t* data, size_t size);

// 把一个目录整体打包成 zip 文件 (zip 内路径为相对目录的 / 分隔路径)
void zipDirectory(const std::filesystem::path& dir, const std::filesystem::path& zipFile);

}  // namespace m2n::util

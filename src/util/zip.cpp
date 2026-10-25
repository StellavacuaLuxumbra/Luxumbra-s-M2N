#include "util/zip.hpp"

#include <miniz.h>

#include <cstring>
#include <stdexcept>

#include "util/fs.hpp"

namespace m2n::util {

std::vector<uint8_t> gzipCompress(const uint8_t* data, size_t size) {
  // raw deflate: 负窗口位 = 不带 zlib 头
  mz_uint flags = tdefl_create_comp_flags_from_zip_params(9, -MZ_DEFAULT_WINDOW_BITS, TDEFL_DEFAULT_STRATEGY);
  size_t outSize = 0;
  void* out = tdefl_compress_mem_to_heap(data, size, &outSize, flags);
  if (!out) throw std::runtime_error("deflate 压缩失败");

  std::vector<uint8_t> result;
  result.reserve(outSize + 32);
  // gzip 头: magic, deflate, 无标志/时间/扩展名, 最大压缩
  const uint8_t header[] = {0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF};
  result.insert(result.end(), header, header + sizeof(header));
  const uint8_t* body = static_cast<const uint8_t*>(out);
  result.insert(result.end(), body, body + outSize);
  mz_free(out);

  // 尾部: CRC32 (LE) + 原始长度 (LE)
  uint32_t crc = mz_crc32(MZ_CRC32_INIT, data, static_cast<mz_uint>(size));
  uint32_t isize = static_cast<uint32_t>(size & 0xFFFFFFFF);
  for (int i = 0; i < 4; ++i) result.push_back(static_cast<uint8_t>(crc >> (8 * i)));
  for (int i = 0; i < 4; ++i) result.push_back(static_cast<uint8_t>(isize >> (8 * i)));
  return result;
}

std::vector<uint8_t> gzipDecompress(const uint8_t* data, size_t size) {
  if (size < 18 || data[0] != 0x1F || data[1] != 0x8B)
    throw std::runtime_error("非法 gzip 数据");
  const uint8_t* body = data + 10;
  size_t bodySize = size - 10 - 8;

  size_t outSize = size * 4 + 1024;  // 初始猜测, 失败后翻倍重试
  for (int attempt = 0; attempt < 32; ++attempt) {
    std::vector<uint8_t> out(outSize);
    tinfl_decompressor infl;
    tinfl_init(&infl);
    size_t inBytes = bodySize;
    size_t outBytes = outSize;
    tinfl_status st = tinfl_decompress(&infl, body, &inBytes, out.data(), out.data(), &outBytes, 0);
    if (st == TINFL_STATUS_DONE) {
      out.resize(outBytes);
      return out;
    }
    outSize *= 2;
  }
  throw std::runtime_error("gzip 解压失败");
}

void zipDirectory(const std::filesystem::path& dir, const std::filesystem::path& zipFile) {
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, zipFile.string().c_str(), 0))
    throw std::runtime_error("无法创建 zip: " + zipFile.string());

  bool ok = true;
  std::string error;
  try {
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file(ec)) continue;
      std::string rel = zipPath(dir, it->path());
      auto data = readFile(it->path());
      if (!mz_zip_writer_add_mem(&zip, rel.c_str(), data.data(), data.size(), MZ_BEST_COMPRESSION))
        throw std::runtime_error("zip 添加文件失败: " + rel);
    }
  } catch (const std::exception& e) {
    ok = false;
    error = e.what();
  }

  mz_zip_writer_finalize_archive(&zip);
  mz_zip_writer_end(&zip);
  if (!ok) throw std::runtime_error(error);
}

}  // namespace m2n::util

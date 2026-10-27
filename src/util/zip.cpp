#include "util/zip.hpp"

#include <miniz.h>

#include <cstring>
#include <stdexcept>

#include "util/fs.hpp"

namespace m2n::util {

std::vector<uint8_t> gzipCompress(const uint8_t* data, size_t size) {
  // raw deflate: miniz 的 zlib 兼容 API, 负 windowBits = 不带 zlib 头
  mz_stream s = {};
  if (mz_deflateInit2(&s, 9, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS, 1, MZ_DEFAULT_STRATEGY) != MZ_OK)
    throw std::runtime_error("deflate 初始化失败");

  std::vector<uint8_t> body(mz_deflateBound(&s, static_cast<mz_ulong>(size)));
  s.next_in = const_cast<mz_uint8*>(data);
  s.avail_in = static_cast<mz_uint>(size);
  s.next_out = body.data();
  s.avail_out = static_cast<mz_uint>(body.size());
  int rc = mz_deflate(&s, MZ_FINISH);
  mz_deflateEnd(&s);
  if (rc != MZ_STREAM_END) throw std::runtime_error("deflate 压缩失败");
  body.resize(s.total_out);

  std::vector<uint8_t> result;
  result.reserve(body.size() + 32);
  // gzip 头: magic, deflate, 无标志/时间/扩展名, 最大压缩
  const uint8_t header[] = {0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF};
  result.insert(result.end(), header, header + sizeof(header));
  result.insert(result.end(), body.begin(), body.end());

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
    // 非环绕缓冲: 线性 buffer 必须设此标志, 否则 tinfl 要求大小为 2 的幂 (当作环形缓冲)
    tinfl_status st = tinfl_decompress(&infl, body, &inBytes, out.data(), out.data(), &outBytes,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (st == TINFL_STATUS_DONE) {
      out.resize(outBytes);
      // gzip 尾部 CRC32 校验 (小端): 防止解码出损坏数据
      uint32_t expect = 0;
      for (int i = 0; i < 4; ++i)
        expect |= static_cast<uint32_t>(data[size - 8 + i]) << (8 * i);
      if (mz_crc32(MZ_CRC32_INIT, out.data(), static_cast<mz_uint>(outBytes)) != expect)
        throw std::runtime_error("gzip CRC32 校验失败");
      return out;
    }
    if (st != TINFL_STATUS_HAS_MORE_OUTPUT)  // 硬错误 (数据损坏) 不再翻倍重试
      throw std::runtime_error("gzip 解压失败");
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

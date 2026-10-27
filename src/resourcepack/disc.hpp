#pragma once
// 唱片资源包: 渲染歌曲音频 -> Ogg Vorbis -> 替换原版唱片

#include <filesystem>
#include <string>
#include <vector>

#include "model/song.hpp"
#include "util/mc_versions.hpp"

namespace m2n::resourcepack {

struct DiscInfo {
  const char* id;       // 资源里的唱片名 (records/<id>.ogg)
  const char* nameZh;
  int lengthSec;        // 原版唱片时长 (秒) - 歌曲超过会被截断
};

// 可替换的原版唱片
const std::vector<DiscInfo>& discList();

struct DiscOptions {
  std::string disc = "pigstep";          // 目标唱片 id
  bool useVanillaSamples = false;        // 用本地 .minecraft 原版音色渲染
  std::string vanillaDir;                // .minecraft 目录 (空则自动探测)
  float oggQuality = 0.5f;
};

struct DiscExportResult {
  std::filesystem::path dir;
  std::filesystem::path zip;
  double audioSec = 0;
};

DiscExportResult exportDiscPack(const model::Song& song, const std::filesystem::path& outDir,
                                const util::McVersionPreset& mc, const DiscOptions& options,
                                const std::filesystem::path& packZip = {});

}  // namespace m2n::resourcepack

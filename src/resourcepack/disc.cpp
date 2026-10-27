#include "resourcepack/disc.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#include "audio/ogg_encoder.hpp"
#include "audio/synth.hpp"
#include "audio/vanilla_assets.hpp"
#include "util/fs.hpp"
#include "util/mc_versions.hpp"
#include "util/zip.hpp"

namespace m2n::resourcepack {

const std::vector<DiscInfo>& discList() {
  static const std::vector<DiscInfo> discs = {
      {"pigstep",   "Pigstep",        149},
      {"otherside", "Otherside",      179},
      {"relic",     "Relic",          190},
      {"5",         "5",              178},
      {"wait",      "Wait",           239},
      {"blocks",    "Blocks",         345},
      {"ward",      "Ward",           252},
      {"strad",     "Strad",          189},
      {"mall",      "Mall",           198},
      {"mellohi",   "Mellohi",         97},
      {"stal",      "Stal",           151},
      {"far",       "Far",            175},
      {"chirp",     "Chirp",          186},
      {"cat",       "Cat",            186},
      {"13",        "13",             179},
      {"11",        "11 (录音带)",     72},
  };
  return discs;
}

DiscExportResult exportDiscPack(const model::Song& song, const std::filesystem::path& outDir,
                                const util::McVersionPreset& mc, const DiscOptions& options,
                                const std::filesystem::path& packZip) {
  using util::writeTextFile;
  namespace fs = std::filesystem;

  // 找目标唱片
  const DiscInfo* disc = nullptr;
  for (const auto& d : discList())
    if (options.disc == d.id) disc = &d;
  if (!disc) throw std::runtime_error("未知唱片 id: " + options.disc + " (可用: pigstep/otherside/...)");

  // 渲染音频
  const double sampleRate = 44100;
  std::vector<float> pcm;
  if (options.useVanillaSamples) {
    auto samples = audio::loadVanillaNoteSamples(options.vanillaDir);
    pcm = audio::renderSongWithSamples(song, samples, sampleRate);
  }
  if (pcm.empty()) pcm = audio::renderSong(song, {.sampleRate = sampleRate});

  auto ogg = audio::encodeOggVorbis(pcm.data(), pcm.size() / 2, static_cast<int>(sampleRate),
                                    options.oggQuality);

  // 资源包结构
  fs::path soundFile = outDir / "assets" / "minecraft" / "sounds" / "records" /
                       (std::string(disc->id) + ".ogg");
  util::writeFile(soundFile, ogg.data(), ogg.size());

  {
    std::ostringstream oss;
    oss << "{\"pack\":{\"pack_format\":" << mc.resourcePackFormat
        << ",\"description\":\"M2N 唱片: " << song.name << " -> " << disc->nameZh << "\"}}";
    writeTextFile(outDir / "pack.mcmeta", oss.str());
  }

  DiscExportResult result;
  result.dir = outDir;
  result.audioSec = static_cast<double>(pcm.size() / 2) / sampleRate;

  {
    std::ostringstream oss;
    oss << "M2N 唱片资源包\n";
    oss << "歌曲: " << song.name << " (" << static_cast<int>(result.audioSec) << " 秒)\n";
    oss << "替换唱片: " << disc->nameZh << " (" << disc->id << ", 原版时长 " << disc->lengthSec
        << " 秒)\n\n";
    oss << "安装: 放入 .minecraft/resourcepacks/ 后在游戏内启用\n";
    oss << "使用: 在生存模式找到 \"" << disc->nameZh
        << "\" 唱片放入唱片机即可播放本歌曲\n\n";
    if (result.audioSec > disc->lengthSec)
      oss << "[注意] 歌曲比原版唱片时长长, 唱片会在 " << disc->lengthSec
          << " 秒处提前弹出; 建议选择时长更长的唱片 (如 Blocks 345 秒)\n";
    oss << "由 Luxumbra's M2N 生成\n";
    writeTextFile(outDir / "使用说明.txt", oss.str());
  }

  if (!packZip.empty()) {
    util::zipDirectory(outDir, packZip);
    result.zip = packZip;
  }
  return result;
}

}  // namespace m2n::resourcepack

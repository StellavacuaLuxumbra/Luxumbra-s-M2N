#include "audio/vanilla_assets.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

#define STB_VORBIS_HEADER_ONLY  // 只取声明, 实现在 stb_vorbis_impl.c
#include <stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY

#include "audio/synth.hpp"
#include "util/fs.hpp"
#include "util/json.hpp"

namespace m2n::audio {

namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;

namespace {

// 资源索引里音符盒音效的可能路径前缀 (不同版本路径不同)
std::vector<std::string> soundKeyCandidates(std::string_view instrumentId) {
  std::string id(instrumentId);
  return {
      "minecraft/sounds/block/note_block/" + id + ".ogg",
      "minecraft/sounds/note/" + id + ".ogg",
  };
}

std::optional<fs::path> findVanillaDir() {
  // 常见安装位置 (%APPDATA%\.minecraft 等)
  std::vector<fs::path> candidates;
  const char* appdata = std::getenv("APPDATA");
  if (appdata) candidates.emplace_back(std::string(appdata) + "\\.minecraft");
  const char* userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    candidates.emplace_back(std::string(userprofile) + "\\AppData\\Roaming\\.minecraft");
    candidates.emplace_back(std::string(userprofile) + "\\.minecraft");
  }
  candidates.emplace_back(".minecraft");
  for (const auto& c : candidates) {
    if (fs::exists(c / "assets" / "indexes")) return c;
  }
  return std::nullopt;
}

}  // namespace

std::optional<DecodedSample> decodeOgg(const uint8_t* data, size_t size) {
  int error = 0;
  stb_vorbis* vor = stb_vorbis_open_memory(data, static_cast<int>(size), &error, nullptr);
  if (!vor) return std::nullopt;
  stb_vorbis_info info = stb_vorbis_get_info(vor);
  DecodedSample out;
  out.sampleRate = static_cast<int>(info.sample_rate);
  std::vector<short> pcm;
  const int kBufFrames = 4096;
  std::vector<short> tmp(static_cast<size_t>(kBufFrames) * info.channels);
  while (true) {
    int frames = stb_vorbis_get_samples_short_interleaved(
        vor, info.channels, tmp.data(), static_cast<int>(tmp.size()));
    if (frames <= 0) break;
    pcm.insert(pcm.end(), tmp.begin(), tmp.begin() + frames * info.channels);
  }
  stb_vorbis_close(vor);
  // 多声道混合为单声道
  size_t frames = pcm.size() / info.channels;
  out.samples.reserve(frames);
  for (size_t f = 0; f < frames; ++f) {
    int acc = 0;
    for (int c = 0; c < info.channels; ++c) acc += pcm[f * info.channels + c];
    out.samples.push_back(static_cast<float>(acc) / info.channels / 32768.0f);
  }
  return out;
}

std::map<model::Instrument, DecodedSample> loadVanillaNoteSamples(const std::string& vanillaDirIn) {
  std::map<model::Instrument, DecodedSample> result;
  std::optional<fs::path> dir;
  if (!vanillaDirIn.empty())
    dir = fs::path(vanillaDirIn);
  else
    dir = findVanillaDir();
  if (!dir || !fs::exists(*dir / "assets" / "indexes")) return result;

  // 选最新索引: 按文件修改时间
  fs::path indexPath;
  std::filesystem::file_time_type bestTime{};
  for (const auto& entry : fs::directory_iterator(*dir / "assets" / "indexes")) {
    if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
    auto time = entry.last_write_time();
    // 跳过预发布索引 (带 -experimental 等)
    auto name = entry.path().stem().string();
    if (name.find('-') != std::string::npos) continue;
    if (!indexPath.empty() && time <= bestTime) continue;
    indexPath = entry.path();
    bestTime = time;
  }
  if (indexPath.empty()) return result;

  auto root = util::json::parse(util::readTextFile(indexPath));
  if (!root || !root->isObject()) return result;
  const auto* objects = root->get("objects");
  if (!objects || !objects->isObject()) return result;

  const fs::path objectsDir = *dir / "assets" / "objects";
  for (int i = 0; i < static_cast<int>(model::Instrument::Count); ++i) {
    auto ins = static_cast<model::Instrument>(i);
    std::string id(model::instrumentId(ins));
    for (const auto& key : soundKeyCandidates(id)) {
      const auto* entry = objects->get(key);
      if (!entry || !entry->isObject()) continue;
      const auto* hash = entry->get("hash");
      if (!hash || !hash->isString()) continue;
      std::string h(hash->str);
      if (h.size() < 2) break;
      fs::path file = objectsDir / h.substr(0, 2) / h;
      if (!fs::exists(file)) break;
      auto bytes = util::readFile(file);
      auto decoded = decodeOgg(bytes.data(), bytes.size());
      if (decoded && decoded->samples.size() > 16) {
        result[ins] = std::move(*decoded);
      }
      break;
    }
  }
  return result;
}

std::vector<float> renderSongWithSamples(const model::Song& song,
                                         const std::map<model::Instrument, DecodedSample>& samples,
                                         double sampleRate) {
  const double lengthSec = song.durationSec + 2.0;
  const size_t totalFrames = static_cast<size_t>(lengthSec * sampleRate) + 1;
  std::vector<double> left(totalFrames, 0.0), right(totalFrames, 0.0);

  for (const auto& lane : song.lanes) {
    auto it = samples.find(lane.instrument);
    if (it == samples.end()) continue;
    const DecodedSample& smp = it->second;
    const int base = model::instrumentBaseMidi(lane.instrument);
    // 假设原版样本录制在该乐器音域中间 (base+12)
    const double sampleFreq = 440.0 * std::pow(2.0, (base + 12 - 69) / 12.0);
    // 乐器声像
    double l = 0.8, r = 0.8;
    int idx = static_cast<int>(lane.instrument);
    double pan = ((idx * 7) % 16 - 7.5) / 10.0;  // -0.75..0.75 稳定伪随机
    l = std::cos((pan + 1.0) * kPi / 4.0);
    r = std::sin((pan + 1.0) * kPi / 4.0);

    for (const auto& note : lane.notes) {
      double freq = 440.0 * std::pow(2.0, (note.midiPitch - 69) / 12.0);
      double rate = freq / sampleFreq * smp.sampleRate / sampleRate;
      double start = note.step * song.gridTicks / 20.0;
      double amp = 0.5 * (0.3 + 0.7 * note.velocity / 127.0);
      size_t startFrame = static_cast<size_t>(start * sampleRate);
      double pos = 0.0;
      size_t f = 0;
      while (true) {
        size_t idxFrame = startFrame + f;
        if (idxFrame >= totalFrames) break;
        size_t i0 = static_cast<size_t>(pos);
        if (i0 + 1 >= smp.samples.size()) break;
        double frac = pos - i0;
        double sample = smp.samples[i0] * (1.0 - frac) + smp.samples[i0 + 1] * frac;
        left[idxFrame] += sample * amp * l;
        right[idxFrame] += sample * amp * r;
        pos += rate;
        ++f;
      }
    }
  }

  // 缺样本的乐器用内置合成器补 (先合成完整再叠加)
  bool hasMissing = false;
  for (const auto& lane : song.lanes) {
    if (samples.find(lane.instrument) == samples.end()) {
      hasMissing = true;
      break;
    }
  }
  if (hasMissing) {
    model::Song fallback = song;
    std::erase_if(fallback.lanes, [&](const model::Lane& lane) {
      return samples.find(lane.instrument) != samples.end();
    });
    if (!fallback.lanes.empty()) {
      auto synth = renderSong(fallback, {.sampleRate = sampleRate});
      for (size_t i = 0; i + 1 < synth.size() && 2 * i + 1 < totalFrames * 2; ++i) {
        left[i] += synth[2 * i];
        right[i] += synth[2 * i + 1];
      }
    }
  }

  double peak = 1e-9;
  for (size_t i = 0; i < totalFrames; ++i)
    peak = std::max(peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
  double norm = 0.9 / peak;

  std::vector<float> out(totalFrames * 2);
  for (size_t i = 0; i < totalFrames; ++i) {
    out[2 * i] = static_cast<float>(left[i] * norm);
    out[2 * i + 1] = static_cast<float>(right[i] * norm);
  }
  return out;
}

}  // namespace m2n::audio

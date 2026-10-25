#include "util/mc_versions.hpp"

#include <algorithm>

namespace m2n::util {

// pack_format / DataVersion 参考: minecraft.wiki/w/Pack_format 与 /w/Data_version
const std::vector<McVersionPreset>& mcVersionPresets() {
  static const std::vector<McVersionPreset> presets = {
      {"1.21.4", 61, 46, 4189, true},
      {"1.21.3", 57, 42, 4082, true},
      {"1.21.2", 57, 42, 4080, true},
      {"1.21.1", 48, 34, 3955, true},
      {"1.21", 48, 34, 3953, true},
      {"1.20.6", 41, 32, 3839, false},
      {"1.20.5", 41, 32, 3839, false},
      {"1.20.4", 26, 22, 3700, false},
      {"1.20.3", 26, 22, 3698, false},
      {"1.20.2", 18, 18, 3579, false},
      {"1.20.1", 15, 15, 3465, false},
      {"1.20", 15, 15, 3463, false},
      {"1.19.4", 12, 13, 3120, false},
  };
  return presets;
}

std::optional<McVersionPreset> findMcVersion(const std::string& version) {
  const auto& presets = mcVersionPresets();
  // 精确匹配
  for (const auto& p : presets)
    if (version == p.version) return p;
  // "1.20" -> 该大版本最新的小版本 (列表新->旧, 先匹配到的就是最新)
  std::string major = version + '.';
  for (const auto& p : presets) {
    std::string_view v(p.version);
    if (v.size() > major.size() && v.compare(0, major.size(), major) == 0) return p;
  }
  return std::nullopt;
}

}  // namespace m2n::util

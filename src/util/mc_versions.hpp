#pragma once
// Minecraft Java 版本预设: pack_format (数据包/资源包) 与 DataVersion (结构 NBT)
// 以及 1.20 -> 1.21 的 function 目录更名

#include <optional>
#include <string>
#include <vector>

namespace m2n::util {

struct McVersionPreset {
  const char* version;     // "1.20.1"
  int dataPackFormat;      // 数据包 pack_format
  int resourcePackFormat;  // 资源包 pack_format
  int dataVersion;         // 结构 NBT DataVersion
  bool useNewFunctionDir;  // 1.21+: function/ 与 tags/function/, 更早是 functions/ 与 tags/functions/
};

// 支持的版本列表 (新->旧)
const std::vector<McVersionPreset>& mcVersionPresets();

// 按版本号查找预设; 支持精确匹配 ("1.20.1") 与主次版本匹配 ("1.20" -> 最新 1.20.x)
std::optional<McVersionPreset> findMcVersion(const std::string& version);

}  // namespace m2n::util

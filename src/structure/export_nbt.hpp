#pragma once
// Song -> 红石机器 -> 结构 .nbt 文件

#include <filesystem>

#include "model/song.hpp"
#include "structure/layout.hpp"

namespace m2n::structure {

struct StructureExportResult {
  std::filesystem::path file;
  int blocks = 0;
  int noteBlocks = 0;
  int sizeX = 0, sizeY = 0, sizeZ = 0;
};

StructureExportResult exportStructureNbt(const model::Song& song,
                                          const std::filesystem::path& outFile,
                                          int dataVersion,
                                          const LayoutOptions& options = {});

}  // namespace m2n::structure

#include "structure/export_nbt.hpp"

#include "nbt/nbt.hpp"
#include "util/fs.hpp"

namespace m2n::structure {

StructureExportResult exportStructureNbt(const model::Song& song,
                                         const std::filesystem::path& outFile, int dataVersion,
                                         const LayoutOptions& options) {
  auto layout = buildMachine(song, options);
  layout.tmpl.dataVersion = dataVersion;
  auto bytes = nbt::encodeStructure(layout.tmpl);
  util::writeFile(outFile, bytes.data(), bytes.size());
  return {outFile, static_cast<int>(layout.tmpl.blocks.size()), layout.noteBlockCount,
          layout.sizeX, layout.sizeY, layout.sizeZ};
}

}  // namespace m2n::structure

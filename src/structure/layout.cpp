#include "structure/layout.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace m2n::structure {

namespace {

struct Vec3 {
  int x, y, z;
  bool operator<(const Vec3& o) const {
    return std::tie(x, y, z) < std::tie(o.x, o.y, o.z);
  }
};

struct BlockEntry {
  std::string name;
  std::map<std::string, std::string> props;
};

// 布局构建器: 先在 "世界坐标" (x: -2 起, y: -1..1) 摆方块, 最后平移到结构坐标
class MachineBuilder {
 public:
  void place(int x, int y, int z, std::string name,
             std::map<std::string, std::string> props = {}) {
    Vec3 p{x, y, z};
    if (blocks_.count(p))
      throw std::runtime_error("布局冲突: 位置重复占用 (" + std::to_string(x) + "," +
                               std::to_string(y) + "," + std::to_string(z) + ")");
    blocks_[p] = BlockEntry{std::move(name), std::move(props)};
  }

  void placeNoCheck(int x, int y, int z, std::string name,
                    std::map<std::string, std::string> props = {}) {
    blocks_[Vec3{x, y, z}] = BlockEntry{std::move(name), std::move(props)};
  }

  bool wireConnects(int x, int y, int z) const {
    auto it = blocks_.find(Vec3{x, y, z});
    if (it == blocks_.end()) return false;
    return it->second.name == "minecraft:redstone_wire" ||
           it->second.name == "minecraft:repeater";
  }

  nbt::StructureTemplate finish(int dataVersion) {
    int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    bool first = true;
    for (const auto& [p, b] : blocks_) {
      if (first) { minX = maxX = p.x; minY = maxY = p.y; minZ = maxZ = p.z; first = false; }
      minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
      minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
      minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }

    nbt::StructureTemplate tmpl;
    tmpl.dataVersion = dataVersion;
    tmpl.sizeX = maxX - minX + 1;
    tmpl.sizeY = maxY - minY + 1;
    tmpl.sizeZ = maxZ - minZ + 1;

    tmpl.blocks.reserve(blocks_.size());
    for (const auto& [p, b] : blocks_) {
      nbt::StructureBlock sb;
      sb.x = p.x - minX;
      sb.y = p.y - minY;
      sb.z = p.z - minZ;
      sb.name = b.name;
      sb.properties = b.props;
      if (b.name == "minecraft:redstone_wire") {
        // 显式写入连接状态, 不依赖放置后的方块更新
        auto side = [&](int dx, int dz) {
          return wireConnects(p.x + dx, p.y, p.z + dz) ? "side" : "none";
        };
        sb.properties["north"] = side(0, -1);
        sb.properties["south"] = side(0, 1);
        sb.properties["west"] = side(-1, 0);
        sb.properties["east"] = side(1, 0);
        sb.properties["power"] = "0";
      }
      tmpl.blocks.push_back(std::move(sb));
    }
    // 低处先放置 (地板在下, 音符盒乐器由下方材质双保险)
    std::stable_sort(tmpl.blocks.begin(), tmpl.blocks.end(),
                     [](const auto& a, const auto& b) { return a.y < b.y; });
    return tmpl;
  }

 private:
  std::map<Vec3, BlockEntry> blocks_;
};

// 把 g tick 拆成 1-4 的中继器延迟序列 (数量最少)
std::vector<int> splitDelay(int g) {
  if (g <= 0) return {};
  int r = (g + 3) / 4;
  int base = g / r, rem = g % r;
  std::vector<int> delays(static_cast<size_t>(r), base);
  for (int i = 0; i < rem; ++i) delays[static_cast<size_t>(i)] += 1;
  return delays;
}

void placeRepeater(MachineBuilder& b, int x, int y, int z, const char* facing, int delay) {
  b.place(x, y, z, "minecraft:repeater",
          {{"delay", std::to_string(delay)},
           {"facing", facing},
           {"locked", "false"},
           {"powered", "false"}});
}

}  // namespace

MachineLayout buildMachine(const model::Song& song, const LayoutOptions& options) {
  MachineBuilder builder;
  MachineLayout out;
  const int grid = std::max(1, song.gridTicks);
  const int lanePitch = 3 + std::max(0, options.laneGap);

  const size_t nLanes = song.lanes.size();
  if (nLanes == 0) throw std::runtime_error("歌曲没有可用的乐器轨");
  std::vector<int> busZ(nLanes);
  for (size_t i = 0; i < nLanes; ++i) busZ[i] = static_cast<int>(i) * lanePitch + 1;
  out.laneBusZ = busZ;

  // ---- 汇流排 (x=-1, 沿 z): 每 13 格插 1 个 1 tick 中继器防衰减,
  //      中继器位置避开泳道总线 z (入口必须是红石粉才能被旁边泳道读到) ----
  int zFirst = busZ.front(), zLast = busZ.back();
  int leverZ = zFirst;
  out.leverZ = leverZ;
  std::set<int> busZSet(busZ.begin(), busZ.end());

  std::vector<int> trunkRepeaters;
  {
    int sinceRep = 0;
    for (int z = zFirst; z <= zLast; ++z) {
      bool isRepeater = false;
      if (sinceRep >= 13 && !busZSet.count(z) && z < zLast) {
        trunkRepeaters.push_back(z);
        sinceRep = 0;
        isRepeater = true;
      }
      if (!isRepeater) ++sinceRep;
    }
  }
  auto trunkDelayAt = [&](int z) {
    int d = 0;
    for (int r : trunkRepeaters)
      if (r < z) ++d;
    return d;
  };
  int maxTrunkDelay = 0;
  std::vector<int> trunkDelay(nLanes);
  for (size_t i = 0; i < nLanes; ++i) {
    trunkDelay[i] = trunkDelayAt(busZ[i]);
    maxTrunkDelay = std::max(maxTrunkDelay, trunkDelay[i]);
  }
  out.trunkExtraDelay = maxTrunkDelay;

  // ---- 逐泳道布局 ----
  int maxX = 0;
  struct NbFloor { int x, z; std::string block; };
  std::vector<NbFloor> nbFloors;

  for (size_t i = 0; i < nLanes; ++i) {
    const model::Lane& lane = song.lanes[i];
    int bz = busZ[i];

    int sync = maxTrunkDelay - trunkDelay[i];
    out.lanePrefixDelay.push_back(sync);

    std::map<int, std::vector<const model::StepNote*>> stepNotes;
    for (const auto& n : lane.notes) stepNotes[n.step].push_back(&n);

    int x = 0;
    for (const int d : splitDelay(sync)) placeRepeater(builder, x++, 1, bz, "east", d);

    for (int s = 0; s < song.totalSteps; ++s) {
      auto it = stepNotes.find(s);
      size_t noteCount = it == stepNotes.end() ? 0 : it->second.size();
      if (noteCount > 0) {
        size_t k = (noteCount + 1) / 2;  // 红石粉单元数, 每单元触发两侧各 1 个音符盒
        for (size_t n = 0; n < noteCount; ++n) {
          const model::StepNote* sn = it->second[n];
          int dustX = x + static_cast<int>(n / 2);
          int nz = bz + (n % 2 == 0 ? -1 : 1);
          builder.place(dustX, 0, nz, "minecraft:note_block",
                        {{"instrument", std::string(model::instrumentId(lane.instrument))},
                         {"note", std::to_string(sn->nbNote)},
                         {"powered", "false"}});
          nbFloors.push_back({dustX, nz, std::string("minecraft:") +
                                            std::string(model::instrumentFloorBlock(lane.instrument))});
        }
        for (size_t c = 0; c < k; ++c) builder.place(x++, 1, bz, "minecraft:redstone_wire");
      }
      for (const int d : splitDelay(grid)) placeRepeater(builder, x++, 1, bz, "east", d);
    }
    maxX = std::max(maxX, x - 1);

    // 支承行 (总线之下)
    for (int sx = 0; sx <= x - 1; ++sx) builder.place(sx, 0, bz, "minecraft:smooth_stone");
  }

  // ---- 汇流排方块 ----
  std::set<int> repZ(trunkRepeaters.begin(), trunkRepeaters.end());
  for (int z = zFirst; z <= zLast; ++z) {
    if (repZ.count(z))
      placeRepeater(builder, -1, 1, z, "south", 1);
    else
      builder.place(-1, 1, z, "minecraft:redstone_wire");
    builder.place(-1, 0, z, "minecraft:smooth_stone");
  }

  // ---- 拉杆 ----
  builder.place(-2, 0, leverZ, "minecraft:smooth_stone");
  builder.place(-2, 1, leverZ, "minecraft:lever",
                {{"face", "floor"}, {"facing", "north"}, {"powered", "false"}});

  // ---- 地板 ----
  int zMin = zFirst - 1, zMaxPlane = zLast + 1;
  if (options.decorativeFloor) {
    for (int x = -2; x <= maxX; ++x)
      for (int z = zMin; z <= zMaxPlane; ++z)
        builder.place(x, -1, z, "minecraft:polished_deepslate");
  } else {
    // 至少给音符盒和支承一个底座
    for (size_t i = 0; i < nLanes; ++i) {
      int bz = busZ[i];
      for (int x = 0; x <= maxX; ++x) builder.place(x, -1, bz, "minecraft:polished_deepslate");
      for (int x = 0; x <= maxX; ++x) {
        builder.place(x, -1, bz - 1, "minecraft:polished_deepslate");
        builder.place(x, -1, bz + 1, "minecraft:polished_deepslate");
      }
    }
    builder.place(-1, -1, leverZ, "minecraft:polished_deepslate");
    builder.place(-2, -1, leverZ, "minecraft:polished_deepslate");
  }
  // 音符盒下方换乐器对应材质 (装饰 + 方块下方材质双保险)
  for (const auto& f : nbFloors) builder.placeNoCheck(f.x, -1, f.z, f.block);

  auto tmpl = builder.finish(3465);
  out.sizeX = tmpl.sizeX;
  out.sizeY = tmpl.sizeY;
  out.sizeZ = tmpl.sizeZ;
  for (const auto& b : tmpl.blocks)
    if (b.name == "minecraft:note_block") ++out.noteBlockCount;
  out.tmpl = std::move(tmpl);
  return out;
}

}  // namespace m2n::structure

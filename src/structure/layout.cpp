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

// 放 count 个红石粉单元, 从 (x,z) 沿 (dx,dz) 推进。
// 红石粉信号 15 起每格 -1: 连续 14 个红石粉后必须经中继器复位 (1 tick)。
// *since 跨调用累计同一条信号路径上的连续红石粉数; 返回插入的中继器数。
int layWire(MachineBuilder& b, int x, int y, int z, int dx, int dz, int count,
            const char* repFacing, int* since) {
  int repeaters = 0;
  for (int k = 0; k < count; ++k) {
    if (*since >= 14) {
      placeRepeater(b, x, y, z, repFacing, 1);
      ++repeaters;
      *since = 0;
    } else {
      b.place(x, y, z, "minecraft:redstone_wire");
      ++*since;
    }
    x += dx;
    z += dz;
  }
  return repeaters;
}

}  // namespace

MachineLayout buildMachine(const model::Song& song, const LayoutOptions& options) {
  MachineBuilder builder;
  MachineLayout out;
  const int grid = std::max(1, song.gridTicks);
  const int lanePitch = 3 + std::max(0, options.laneGap);

  const size_t nLanes = song.lanes.size();
  if (nLanes == 0) throw std::runtime_error("歌曲没有可用的乐器轨");
  const int totalSteps = std::max(1, song.totalSteps);

  // ---- 蛇形折叠 (时间分带) ----
  // foldWidth = 每条"带"覆盖的步数; 0 = 不折叠 (单带)。
  // 带 q 播放步 [q*W, (q+1)*W), 带在 z 方向堆叠, 带内都是向东直线。
  // 汇流排沿 z 贯穿所有带, 带间空隙放精确 bandTicks 的延迟链 => 第 q 带
  // 在 q*bandTicks + 基准 时刻得电, 各带自动衔接, 时序不失步。
  const int foldWidth = std::max(0, options.foldWidth);
  const int nPasses = foldWidth > 0 ? (totalSteps + foldWidth - 1) / foldWidth : 1;
  const int bandTicks = foldWidth > 0 ? foldWidth * grid : 0;
  const int chainRows = bandTicks > 0 ? (bandTicks + 3) / 4 : 0;  // T tick 需 ⌈T/4⌉ 个中继器
  // 带距 = 末泳道行 + 南侧音符行 + 带间延迟链行数 + 1 空隙
  const int bandPitch =
      static_cast<int>(nLanes - 1) * lanePitch + 2 + chainRows + 1;

  // 泳道 lane 在第 pass 带的行 z (顺序每带相同)
  auto rowZ = [&](size_t lane, int pass) {
    return pass * bandPitch + static_cast<int>(lane) * lanePitch + 1;
  };

  for (size_t i = 0; i < nLanes; ++i) {
    out.laneBusZ.push_back(rowZ(i, 0));
    std::vector<int> rows;
    for (int k = 0; k < nPasses; ++k) rows.push_back(rowZ(i, k));
    out.laneRowsZ.push_back(std::move(rows));
  }
  out.bandDelayTicks = bandTicks;
  out.passes = nPasses;

  // 步 -> 音符索引 (按泳道)
  std::vector<std::map<int, std::vector<const model::StepNote*>>> stepNotes(nLanes);
  for (size_t i = 0; i < nLanes; ++i)
    for (const auto& n : song.lanes[i].notes) stepNotes[i][n.step].push_back(&n);

  // ---- 汇流排 (x=-1, 沿 z): 贯穿所有带。
  //   - 泳道行 (任何带): 红石粉 (泳道入口由此供电)
  //   - 带间空隙: 精确 bandTicks 延迟链 (⌈T/4⌉ 个中继器)
  //   - 其他间隔: 每 14 格 1 个 1tick 中继器防衰减 ----
  const int zFirst = 1;
  const int zTrunkLast = rowZ(nLanes - 1, nPasses - 1);
  out.leverZ = zFirst;
  std::set<int> laneRowSet;
  for (const auto& rows : out.laneRowsZ)
    for (int z : rows) laneRowSet.insert(z);

  std::vector<int> trunkRepeaters;
  {
    // 收集所有带间空隙区间 (本带最后一行 + 1 .. 下带第一行 - 1)
    std::vector<std::pair<int, int>> gaps;
    for (int q = 0; q + 1 < nPasses; ++q) {
      int from = rowZ(nLanes - 1, q) + 1;
      int to = rowZ(0, q + 1) - 1;
      gaps.emplace_back(from, to);
    }
    std::map<int, int> chainAt;  // z -> delay (带间链)
    for (auto [from, to] : gaps) {
      int rows = to - from + 1;
      int t = bandTicks;
      int n = (t + 3) / 4;
      if (n > rows) throw std::runtime_error("内部错误: 带间空隙不足容纳延迟链");
      for (int k = 0; k < n; ++k) {
        int d = std::min(4, t);
        t -= d;
        chainAt[from + k] = d;
      }
    }
    int sinceRep = 0;
    for (int z = zFirst; z <= zTrunkLast; ++z) {
      if (laneRowSet.count(z)) {
        ++sinceRep;
        continue;  // 泳道行放红石粉 (下面统一放)
      }
      auto it = chainAt.find(z);
      if (it != chainAt.end()) {
        placeRepeater(builder, -1, 1, z, "south", it->second);
        trunkRepeaters.push_back(z);
        sinceRep = 0;
        continue;
      }
      if (sinceRep >= 14 && z < zTrunkLast) {
        placeRepeater(builder, -1, 1, z, "south", 1);
        trunkRepeaters.push_back(z);
        sinceRep = 0;
        continue;
      }
      ++sinceRep;
    }
  }

  auto trunkDelayAt = [&](int z) {
    int d = 0;
    for (int r : trunkRepeaters)
      if (r < z) ++d;
    return d;
  };

  // 同步基准: step s 应在 s*grid + D* 处触发。第 q 带泳道行从汇流排收到信号的时刻为
  // trunkDelayAt(row)。取 D* = max_rows(trunkDelayAt(row) - q*bandTicks), 各行头部前置
  // 中继器补齐差值, 保证各带各泳道 step s 的绝对 tick 一致。
  int baseDelay = 0;
  for (size_t i = 0; i < nLanes; ++i)
    for (int q = 0; q < nPasses; ++q) {
      int d = trunkDelayAt(rowZ(i, q)) - q * bandTicks;
      baseDelay = std::max(baseDelay, d);
    }
  out.trunkExtraDelay = baseDelay;

  // ---- 逐泳道逐带布局 ----
  struct NbFloor { int x, z; std::string block; };
  std::vector<NbFloor> nbFloors;
  int maxX = 0;

  for (size_t i = 0; i < nLanes; ++i) {
    const model::Lane& lane = song.lanes[i];
    out.lanePrefixDelay.emplace_back();

    for (int pass = 0; pass < nPasses; ++pass) {
      const int bz = rowZ(i, pass);
      const int s0 = pass * foldWidth;
      const int s1 =
          std::min(totalSteps, s0 + (foldWidth > 0 ? foldWidth : totalSteps));

      int x = 0;
      // 头部同步补偿中继器 (信号从汇流排西侧进入)
      int sync = baseDelay + pass * bandTicks - trunkDelayAt(bz);
      out.lanePrefixDelay.back().push_back(sync);
      for (const int d : splitDelay(sync)) placeRepeater(builder, x++, 1, bz, "east", d);

      for (int s = s0; s < s1; ++s) {
        auto it = stepNotes[i].find(s);
        size_t noteCount = it == stepNotes[i].end() ? 0 : it->second.size();
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
          maxX = std::max(maxX, static_cast<int>(x - 1));
        }
        // 步间中继器: 延迟和 = gridTicks, 放在红石粉之后 (单元方向 = 向东)
        int sinceWire = 0;
        for (const int d : splitDelay(grid)) {
          placeRepeater(builder, x++, 1, bz, "east", d);
        }
        maxX = std::max(maxX, static_cast<int>(x - 1));
      }
      maxX = std::max(maxX, x - 1);

      // 支承行 (y=0, 该泳道本带占据的 x 范围)
      for (int sx = 0; sx <= x - 1; ++sx) builder.place(sx, 0, bz, "minecraft:smooth_stone");
    }
  }

  // ---- 汇流排方块 ----
  std::set<int> repZ(trunkRepeaters.begin(), trunkRepeaters.end());
  for (int z = zFirst; z <= zTrunkLast; ++z) {
    if (repZ.count(z))
      placeRepeater(builder, -1, 1, z, "south", 1);
    else
      builder.place(-1, 1, z, "minecraft:redstone_wire");
    builder.place(-1, 0, z, "minecraft:smooth_stone");
    maxX = std::max(maxX, -1);
  }

  // ---- 拉杆 ----
  builder.place(-2, 0, leverZ(), out, "minecraft:smooth_stone");
  builder.place(-2, 1, leverZ(), out, "minecraft:lever",
                {{"face", "floor"}, {"facing", "north"}, {"powered", "false"}});
  maxX = std::max(maxX, -2);

  // ---- 地板: 所有 y=0 方块下方放底座; 音符盒位置再换成乐器对应材质 ----
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

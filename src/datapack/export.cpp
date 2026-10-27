#include "datapack/export.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

#include "util/fs.hpp"
#include "util/mc_versions.hpp"
#include "util/zip.hpp"

namespace m2n::datapack {

namespace {

std::string jsonEscape(std::string_view s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace

DatapackExportResult exportDatapack(const model::Song& song, const std::filesystem::path& outDir,
                                    const util::McVersionPreset& mc, const DatapackOptions& options,
                                    const std::filesystem::path& packZip) {
  using util::writeTextFile;
  namespace fs = std::filesystem;

  const std::string ns = "m2n_" + song.id;
  const int totalTicks = song.totalTicks();
  const std::string fnDir = mc.useNewFunctionDir ? "function" : "functions";
  const std::string tagDir = mc.useNewFunctionDir ? "tags/function" : "tags/functions";
  fs::path data = outDir / "data" / ns / fnDir;

  DatapackExportResult result;
  result.dir = outDir;
  int functionCount = 0;

  // pack.mcmeta
  {
    std::ostringstream oss;
    oss << "{\"pack\":{\"pack_format\":" << mc.dataPackFormat
        << ",\"description\":\"M2N 数据包播放器: " << jsonEscape(song.name) << "\"}}";
    writeTextFile(outDir / "pack.mcmeta", oss.str());
  }

  // tick 函数: 推进计时器并按区间分发
  {
    std::ostringstream oss;
    oss << "# M2N 数据包播放器 - " << song.name << "\n";
    oss << "# 播放: /function " << ns << ":start   停止: /function " << ns << ":stop\n";
    oss << "execute if score $state " << ns << " matches 1 run scoreboard players add $t " << ns << " 1\n";
    for (int t0 = 0; t0 < totalTicks; t0 += options.sliceTicks) {
      int t1 = std::min(totalTicks, t0 + options.sliceTicks) - 1;
      oss << "execute if score $state " << ns << " matches 1 if score $t " << ns << " matches "
          << t0 << ".." << t1 << " run function " << ns << ":tick/t" << t0 << "\n";
    }
    oss << "execute if score $state " << ns << " matches 1 if score $t " << ns << " matches "
        << totalTicks << ".. run function " << ns << ":on_end\n";
    writeTextFile(data / "tick.mcfunction", oss.str());
    functionCount++;
  }

  // start / stop / on_end
  {
    std::ostringstream oss;
    oss << "scoreboard objectives add " << ns << " dummy\n";
    oss << "scoreboard players set $state " << ns << " 1\n";
    oss << "scoreboard players set $t " << ns << " -1\n";
    oss << "tellraw @a {\"text\":\"[M2N] 开始播放: " << jsonEscape(song.name)
        << "\",\"color\":\"gold\"}\n";
    writeTextFile(data / "start.mcfunction", oss.str());

    std::ostringstream stop;
    stop << "scoreboard players set $state " << ns << " 0\n";
    stop << "stopsound @a record\n";
    stop << "tellraw @a {\"text\":\"[M2N] 停止播放\",\"color\":\"yellow\"}\n";
    writeTextFile(data / "stop.mcfunction", stop.str());

    std::ostringstream end;
    if (options.loop) {
      end << "scoreboard players set $t " << ns << " -1\n";
    } else {
      end << "function " << ns << ":stop\n";
    }
    writeTextFile(data / "on_end.mcfunction", end.str());
    functionCount += 3;
  }

  // 分片函数: 每 sliceTicks 一个文件, 只含该区间内的音符命令
  {
    const int slices = (totalTicks + options.sliceTicks - 1) / options.sliceTicks;
    std::vector<std::ostringstream> sliceOss(static_cast<size_t>(slices));

    for (const auto& lane : song.lanes) {
      int base = model::instrumentBaseMidi(lane.instrument);
      std::string sound(model::instrumentSound(lane.instrument));
      for (const auto& n : lane.notes) {
        int tick = n.step * song.gridTicks;
        size_t slice = static_cast<size_t>(tick / options.sliceTicks);
        if (slice >= sliceOss.size()) continue;
        double vol = 0.25 + 0.75 * (n.velocity / 127.0);
        // playsound 音高倍率: 相对该乐器采样的中间音 (基准+12), 恰落在 0.5..2.0
        double pitch = std::pow(2.0, (n.midiPitch - base - 12) / 12.0);
        char volS[16], pitchS[16];
        std::snprintf(volS, sizeof(volS), "%.3f", vol);
        std::snprintf(pitchS, sizeof(pitchS), "%.5f", pitch);
        sliceOss[slice] << "execute if score $t " << ns << " matches " << tick
                        << " as @a at @s run playsound minecraft:" << sound
                        << " record @s ~ ~ ~ " << volS << " " << pitchS << "\n";
        if (options.particles) {
          double hue = n.nbNote / 24.0;
          char hueS[16];
          std::snprintf(hueS, sizeof(hueS), "%.3f", hue);
          sliceOss[slice] << "execute if score $t " << ns << " matches " << tick
                          << " as @a at @s run particle minecraft:note ~ ~1.8 ~ 0.15 0.25 0.15 "
                          << hueS << " 0\n";
        }
      }
    }
    for (size_t i = 0; i < sliceOss.size(); ++i) {
      if (sliceOss[i].tellp() == 0) continue;  // 空分片不落盘
      int t0 = static_cast<int>(i) * options.sliceTicks;
      writeTextFile(data / "tick" / ("t" + std::to_string(t0) + ".mcfunction"),
                    sliceOss[i].str());
      functionCount++;
    }
  }

  // tick.json
  {
    fs::path tagFile = outDir / "data" / "minecraft" / tagDir / "tick.json";
    writeTextFile(tagFile, "{\"values\":[\"" + ns + ":tick\"]}");
  }

  // 使用说明
  {
    std::ostringstream oss;
    oss << "M2N 数据包播放器 - " << song.name << "\n\n";
    oss << "安装: 把本数据包放入 world/datapacks/ 目录 (压缩包或文件夹均可), /reload 生效\n";
    oss << "播放: /function " << ns << ":start\n";
    oss << "停止: /function " << ns << ":stop\n";
    if (options.loop) oss << "循环播放已开启 (改 on_end.mcfunction 可关闭)\n";
    oss << "\n歌曲时长: " << song.durationSec << " 秒, 音符数: " << song.totalNotes << "\n";
    oss << "由 Luxumbra's M2N 生成\n";
    writeTextFile(outDir / "使用说明.txt", oss.str());
  }

  result.functionCount = functionCount;
  if (!packZip.empty()) {
    util::zipDirectory(outDir, packZip);
    result.zip = packZip;
  }
  return result;
}

}  // namespace m2n::datapack

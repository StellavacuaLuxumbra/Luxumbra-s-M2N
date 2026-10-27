#include "app.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "datapack/export.hpp"
#ifdef M2N_HAS_AUDIO
#include "resourcepack/disc.hpp"
#endif
#include "midi/smf.hpp"
#include "model/convert.hpp"
#include "model/instruments.hpp"
#include "structure/export_nbt.hpp"
#include "util/fs.hpp"
#include "util/mc_versions.hpp"
#include "version.hpp"

namespace m2n::app {

namespace fs = std::filesystem;

namespace {

// 加载并解析 MIDI, 出错时打印信息并返回空
std::optional<midi::MidiData> loadMidi(const std::string& path) {
  try {
    auto bytes = util::readFile(fs::path(path));
    return midi::parseSmf(bytes.data(), bytes.size());
  } catch (const std::exception& e) {
    std::cerr << "错误: " << e.what() << "\n";
    return std::nullopt;
  }
}

std::optional<util::McVersionPreset> pickMcVersion(const std::string& v) {
  auto mc = util::findMcVersion(v.empty() ? "1.20.1" : v);
  if (!mc) {
    std::cerr << "错误: 不支持的 MC 版本 \"" << v << "\" (可用如 1.19.4 / 1.20.1 / 1.21.4)\n";
    return std::nullopt;
  }
  return mc;
}

void printWarnings(const std::vector<model::ConvertWarning>& warnings) {
  for (const auto& w : warnings) std::cout << "  [警告] " << w.message << "\n";
}

}  // namespace

int info(const std::string& midiPath) {
  auto midi = loadMidi(midiPath);
  if (!midi) return 1;

  std::cout << "MIDI 文件: " << midiPath << "\n";
  std::cout << "格式: " << midi->format << " (SMF), division: "
            << (midi->smpte ? "SMPTE" : std::to_string(midi->division) + " ticks/四分音符") << "\n";
  std::cout << "轨道: " << midi->tracks.size() << ", 音符: " << midi->notes.size() << "\n";
  std::cout << "时长: " << midi->durationSec << " 秒\n";
  int tempoBpm = 0;
  if (!midi->tempoMap.empty()) tempoBpm = static_cast<int>(60'000'000.0 / midi->tempoMap.front().second);
  std::cout << "初始速度: " << tempoBpm << " BPM\n";
  std::cout << "轨道列表:\n";
  for (const auto& t : midi->tracks) {
    std::cout << "  #" << t.index << ": " << (t.name.empty() ? "(无名)" : t.name) << " - "
              << t.noteCount << " 音符\n";
  }
  std::cout << "乐器分布 (映射到音符盒):\n";
  std::map<model::Instrument, int> lanes;
  for (const auto& n : midi->notes) {
    model::Instrument ins = n.drum ? model::mapDrumNote(n.pitch) : model::mapGmProgram(n.program);
    lanes[ins]++;
  }
  for (const auto& [ins, count] : lanes)
    std::cout << "  " << model::instrumentNameZh(ins) << " (" << model::instrumentId(ins)
              << "): " << count << " 音符\n";
  return 0;
}

int build(const std::string& midiPath, const std::vector<std::string>& args) {
  // ---- 参数解析 ----
  std::string outDirArg, mcVersionArg, formatsArg = "nbt,datapack", discArg = "pigstep",
                                                    vanillaDirArg;
  int grid = 2, maxLanes = 16;
  bool loop = false, particles = false, vanillaSamples = false, noFloor = false, zip = false;

  auto needValue = [&](size_t& i, const char* name) -> std::string {
    if (i + 1 >= args.size()) throw std::runtime_error(std::string("选项缺少值: ") + name);
    return args[++i];
  };
  try {
    for (size_t i = 0; i < args.size(); ++i) {
      const std::string& a = args[i];
      if (a == "-o" || a == "--out") outDirArg = needValue(i, a.c_str());
      else if (a == "--mc") mcVersionArg = needValue(i, a.c_str());
      else if (a == "--formats") formatsArg = needValue(i, a.c_str());
      else if (a == "--grid") grid = std::stoi(needValue(i, a.c_str()));
      else if (a == "--max-lanes") maxLanes = std::stoi(needValue(i, a.c_str()));
      else if (a == "--loop") loop = true;
      else if (a == "--particles") particles = true;
      else if (a == "--disc") discArg = needValue(i, a.c_str());
      else if (a == "--vanilla-samples") vanillaSamples = true;
      else if (a == "--vanilla-dir") vanillaDirArg = needValue(i, a.c_str());
      else if (a == "--no-floor") noFloor = true;
      else if (a == "--zip") zip = true;
      else throw std::runtime_error("未知选项: " + a);
    }
  } catch (const std::exception& e) {
    std::cerr << "参数错误: " << e.what() << "\n";
    return 1;
  }

  if (grid < 1 || grid > 40) {
    std::cerr << "参数错误: --grid 需在 1..40\n";
    return 1;
  }

  auto mc = pickMcVersion(mcVersionArg);
  if (!mc) return 1;

  // 解析 formats
  std::vector<std::string> formats;
  {
    std::istringstream ss(formatsArg);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      if (!tok.empty()) formats.push_back(tok);
    }
  }
  for (const auto& f : formats) {
    if (f != "nbt" && f != "datapack" && f != "disc") {
      std::cerr << "错误: 未知产物类型 \"" << f << "\" (可用: nbt, datapack, disc)\n";
      return 1;
    }
#ifndef M2N_HAS_AUDIO
    if (f == "disc") {
      std::cerr << "错误: 本构建未启用音频模块 (disc), 请用 nbt/datapack\n";
      return 1;
    }
#endif
  }

  // ---- 加载与转换 ----
  auto midi = loadMidi(midiPath);
  if (!midi) return 1;

  model::ConvertOptions copts;
  copts.gridTicks = grid;
  copts.maxLanes = maxLanes;
  auto conv = model::convert(*midi, copts);
  model::Song& song = conv.song;

  fs::path inputPath(midiPath);
  std::string stem = inputPath.stem().string();
  song.id = util::sanitizeName(stem);
  song.name = stem;
  if (song.lanes.empty()) {
    std::cerr << "错误: 该 MIDI 没有可转换的音符\n";
    return 1;
  }

  std::cout << "歌曲: " << song.name << "\n";
  std::cout << "  时长 " << song.durationSec << " 秒, " << song.totalNotes << " 音符, "
            << song.lanes.size() << " 条乐器轨, 每步 " << song.gridTicks << " tick ("
            << (20.0 / song.gridTicks) << " 步/秒), 共 " << song.totalSteps << " 步\n";
  printWarnings(conv.warnings);
  for (const auto& lane : song.lanes) {
    std::cout << "  轨道: " << model::instrumentNameZh(lane.instrument) << " - "
              << lane.notes.size() << " 音符";
    if (lane.octaveShift != 0)
      std::cout << " (八度位移 " << (lane.octaveShift > 0 ? "+" : "") << lane.octaveShift / 12
                << ")";
    std::cout << "\n";
  }

  fs::path outDir = outDirArg.empty() ? fs::path("out") / song.id : fs::path(outDirArg);
  fs::create_directories(outDir);

  // ---- 导出各产物 ----
  for (const auto& f : formats) {
    if (f == "nbt") {
      try {
        structure::LayoutOptions lopts;
        lopts.decorativeFloor = !noFloor;
        fs::path file = outDir / (song.id + "_machine.nbt");
        auto r = structure::exportStructureNbt(song, file, mc->dataVersion, lopts);
        std::cout << "[nbt] 红石结构: " << r.file.string() << "\n";
        std::cout << "      尺寸 " << r.sizeX << "x" << r.sizeY << "x" << r.sizeZ << ", "
                  << r.blocks << " 方块 (含 " << r.noteBlocks << " 音符盒)\n";
        std::cout << "      用结构方块加载, 或 /place template ~ ~ ~ <file>\n";
      } catch (const std::exception& e) {
        std::cerr << "[nbt] 失败: " << e.what() << "\n";
        return 1;
      }
    } else if (f == "datapack") {
      try {
        datapack::DatapackOptions dopts;
        dopts.loop = loop;
        dopts.particles = particles;
        fs::path dir = outDir / (song.id + "_datapack");
        fs::path zipPath = zip ? fs::path(outDir / (song.id + "_datapack.zip")) : fs::path();
        auto r = datapack::exportDatapack(song, dir, *mc, dopts, zipPath);
        std::cout << "[datapack] 数据包播放器: " << r.dir.string() << " (" << r.functionCount
                  << " 个函数)\n";
        if (!r.zip.empty()) std::cout << "            zip: " << r.zip.string() << "\n";
        std::cout << "            /function m2n_" << song.id << ":start 开始播放\n";
      } catch (const std::exception& e) {
        std::cerr << "[datapack] 失败: " << e.what() << "\n";
        return 1;
      }
    }
#ifdef M2N_HAS_AUDIO
    else if (f == "disc") {
      try {
        resourcepack::DiscOptions ropts;
        ropts.disc = discArg;
        ropts.useVanillaSamples = vanillaSamples;
        ropts.vanillaDir = vanillaDirArg;
        fs::path dir = outDir / (song.id + "_disc_pack");
        fs::path zipPath = zip ? fs::path(outDir / (song.id + "_disc_pack.zip")) : fs::path();
        auto r = resourcepack::exportDiscPack(song, dir, *mc, ropts, zipPath);
        std::cout << "[disc] 唱片资源包: " << r.dir.string() << " (音频 " << static_cast<int>(r.audioSec)
                  << " 秒)\n";
        if (!r.zip.empty()) std::cout << "       zip: " << r.zip.string() << "\n";
      } catch (const std::exception& e) {
        std::cerr << "[disc] 失败: " << e.what() << "\n";
        return 1;
      }
    }
#endif
  }

  std::cout << "完成! 产物在 " << outDir.string() << "\n";
  return 0;
}

int wizard() {
  std::cout << M2N_NAME " v" M2N_VERSION " - 交互式向导\n";
  std::cout << "=============================\n\n";

  std::cout << "输入 MIDI 文件路径: ";
  std::string midiPath;
  if (!std::getline(std::cin, midiPath) || midiPath.empty()) {
    std::cout << "已取消.\n";
    return 0;
  }
  // 去除首尾引号/空格
  while (!midiPath.empty() && (midiPath.front() == '"' || midiPath.front() == ' '))
    midiPath.erase(midiPath.begin());
  while (!midiPath.empty() && (midiPath.back() == '"' || midiPath.back() == ' '))
    midiPath.pop_back();

  if (info(midiPath) != 0) return 1;

  std::cout << "\n选择产物 (回车=全部):\n";
#ifdef M2N_HAS_AUDIO
  std::cout << "  1) 红石结构 .nbt\n  2) 数据包播放器\n  3) 唱片资源包\n  4) 全部\n";
#else
  std::cout << "  1) 红石结构 .nbt\n  2) 数据包播放器\n  3) 全部\n";
#endif
  std::cout << "选择 [1/2/3"
#ifdef M2N_HAS_AUDIO
            << "/4"
#endif
            << "]: ";
  std::string choice;
  std::getline(std::cin, choice);
#ifdef M2N_HAS_AUDIO
  std::string formats = choice == "1" ? "nbt" : choice == "2" ? "datapack" : choice == "3" ? "disc" : "nbt,datapack,disc";
#else
  std::string formats = choice == "1" ? "nbt" : choice == "2" ? "datapack" : "nbt,datapack";
#endif

  std::cout << "每步 tick 数 (回车=2, 越小越快): ";
  std::string gridStr;
  std::getline(std::cin, gridStr);
  int grid = gridStr.empty() ? 2 : std::max(1, std::stoi(gridStr));

  std::cout << "MC 版本 (回车=1.20.1): ";
  std::string mcStr;
  std::getline(std::cin, mcStr);

  std::cout << "\n开始生成...\n\n";
  std::vector<std::string> args = {"--formats", formats, "--grid", std::to_string(grid), "--zip"};
  if (!mcStr.empty()) {
    args.push_back("--mc");
    args.push_back(mcStr);
  }
  return build(midiPath, args);
}

}  // namespace m2n::app

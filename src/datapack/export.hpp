#pragma once
// 数据包播放器导出: 原版命令播放 (计时器 + 分片调度 + playsound)
//
// data/<ns>/tick.mcfunction            由 tick.json 驱动: 推进计时器并分发
// data/<ns>/start.mcfunction           开始播放
// data/<ns>/stop.mcfunction            停止
// data/<ns>/tick/tNNNN.mcfunction      每 100 tick 一个分片
// data/minecraft/tags/function(s)/tick.json

#include <filesystem>

#include "model/song.hpp"
#include "util/mc_versions.hpp"

namespace m2n::datapack {

struct DatapackOptions {
  bool loop = false;
  bool particles = false;  // 播放音符时生成 note 粒子
  int sliceTicks = 100;    // 每个分片函数覆盖的 tick 数
};

struct DatapackExportResult {
  std::filesystem::path dir;   // 数据包目录
  std::filesystem::path zip;   // 打包后的 zip (若打包)
  int functionCount = 0;
};

// outDir: 数据包根目录 (会创建). mcPreset 决定 pack_format 与 function 目录命名.
// packZip 为空则不打包, 否则同时输出 zip 文件.
DatapackExportResult exportDatapack(const model::Song& song, const std::filesystem::path& outDir,
                                    const util::McVersionPreset& mc, const DatapackOptions& options,
                                    const std::filesystem::path& packZip = {});

}  // namespace m2n::datapack

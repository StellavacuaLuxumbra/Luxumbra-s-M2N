#pragma once
// 红石音符盒机器布局生成
//
// 结构 (x=时间方向东, z=泳道方向南, y=高度):
//   y=1:  拉杆  [汇流排 dust/repeater 沿 z]   每条泳道: [同步中继器][步单元...]*
//   y=0:  支承行(石) + 音符盒 (支承行两侧 z±1)
//   y=-1: 地板 (音符盒下方是乐器对应材质, 双保险)
//
// 步单元 (每步 gridTicks tick):
//   [K 个红石粉单元][R 个中继器, 延迟和 = gridTicks]
//   脉冲前沿到达时整段红石粉同时点亮 -> 弱充能支承块 -> 触发两侧音符盒
//
// 同步: 汇流排沿 z 的中继器使远端泳道晚启动, 各泳道头部用额外中继器补齐延迟差

#include "model/song.hpp"
#include "nbt/nbt.hpp"

namespace m2n::structure {

struct LayoutOptions {
  int laneGap = 1;            // 泳道间空隙 (格)
  bool decorativeFloor = true; // 生成整层装饰地板
};

struct MachineLayout {
  nbt::StructureTemplate tmpl;
  int sizeX = 0, sizeY = 0, sizeZ = 0;
  int noteBlockCount = 0;
  int leverZ = 0;                       // 拉杆所在 z (世界坐标, 用于文档)
  std::vector<int> laneBusZ;            // 每条泳道总线的 z (世界坐标)
  std::vector<int> lanePrefixDelay;     // 每条泳道同步补偿延迟 (tick)
  int trunkExtraDelay = 0;              // 汇流排带来的最大延迟 (tick)
};

MachineLayout buildMachine(const model::Song& song, const LayoutOptions& options = {});

}  // namespace m2n::structure

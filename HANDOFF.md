# Luxumbra's M2N — AI 交接文档

> 本文档供后续 AI/开发者接手工作。最后更新: 2026-10-28。
> 项目: MIDI → Minecraft 音符盒红石音乐 转换器 (C++20 控制台程序, GPLv3)

## 1. 项目目标

输入 `.mid` 文件，生成三类产物（MVP 全部实现中）：

1. **红石结构 `.nbt`** — 真实音符盒+红石粉+中继器机器，结构方块 / `/place template` 放入世界，生存可用
2. **数据包播放器** — 纯原版命令（计分板计时器 + 分片调度 + `playsound`），音准完美，无需资源包
3. **唱片资源包** — 内置合成器渲染音频 → Ogg Vorbis → 替换原版唱片；`--vanilla-samples` 可读取用户本地 `.minecraft` 原版音色

目标平台 Java 1.20+（1.19.4–1.21.4 版本预设已内置）。

## 2. 当前状态（重要！）

**代码全部写完（M1–M4 全部模块），处于首次编译修错阶段，尚未跑通完整构建，测试一次未运行。**

最后一次构建错误及修复情况：
- ✅ 已修: `M2N_NAME` 缺 include、`cli::` 命名空间、`decodeOgg` 缺 include（audio_tests）
- ✅ 已修: `Reader::u16` 已补回 smf.cpp
- ✅ 已修: gzipCompress 改用 miniz zlib 兼容 API（`mz_deflateInit2` + 负 windowBits = raw deflate）
- ✅ 已修: `M_PI` → 本地 `kPi`（synth.cpp / vanilla_assets.cpp，因 `-std=c++20` 严格模式）
- ✅ 已修: `mz_byte` → `mz_uint8`（zip.cpp）
- ✅ 已修: vanilla_assets.hpp 补 `#include "model/song.hpp"`
- ⏳ **修复后尚未重新编译验证** — 下一步就是重新构建（见 §5）

已验证可用的 CMake configure（依赖已全部下载到 build/_deps，miniz 3.0.2 / stb 003501f6 / doctest v2.4.12 / ogg v1.3.5 / vorbis v1.3.7）。

## 3. 构建方法（Windows，中文路径有坑！）

- 工具链: CMake 4.4 (`C:\Program Files\CMake\bin`) + GCC 15.2 MinGW-w64 (`D:\mingw64`)，无 MSVC/ninja
- **项目路径含中文（`F:\项目\...`），CMake FetchContent 子进程会把路径按错误编码传递而失败。
  已建目录联接（junction）`C:\m2n` → `F:\项目\Luxumbra's M2N`，一切构建命令必须在 `/c/m2n` 下执行！**
  （junction 若丢失，用 PowerShell 重建: `New-Item -ItemType Junction -Path C:\m2n -Target "F:\项目\Luxumbra's M2N"`）

```bash
cd /c/m2n
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=D:/mingw64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
/d/mingw64/bin/mingw32-make.exe -C build -j8
cd build && ctest   # 或直接运行 m2n_tests.exe
```

- 选项 `M2N_ENABLE_AUDIO=ON`（默认）编译 Ogg/唱片功能；出问题可先关掉推进其他模块
- CMakeLists 中 `CMAKE_POLICY_VERSION_MINIMUM 3.5` 是给 miniz 的旧 CMake 声明用的，勿删
- vorbis 不用其自带 CMake，直接编译 lib/*.c 源码列表（已核对实际文件名: `psy.c` 而非 psycho_1.c；barkmel/psytune/tone.c 是带 main 的测试程序，绝不能加入）

## 4. 代码结构与模块

```
src/
  main.cpp            入口（Windows UTF-8 控制台设置）
  app.cpp/.hpp        info/build/wizard 业务编排
  cli/cli.cpp         子命令解析（help/info/build/version/wizard）
  midi/smf.cpp        手写 SMF 解析器（格式0/1、running status、tempo map、SMPTE、note-off 配对含 vel=0）
  model/instruments   16 种乐器枚举 + GM 128 音色映射 + GM 鼓组映射 + 下方材质表 + 音高基准
  model/convert.cpp   量化(秒→tick→step)、按乐器分组、八度适配(12 倍数位移+钳制告警)、去重、maxLanes 裁剪
  model/song.hpp      Song/Lane/StepNote 数据模型
  nbt/nbt.cpp         NBT 大端写入器(栈式 API) + 结构模板编码 + gzip
  structure/layout.cpp 红石机器布局（核心算法，见 §6）
  structure/export_nbt.cpp .nbt 文件导出
  datapack/export.cpp  数据包播放器生成
  audio/synth.cpp      内置合成器（振荡器+包络参数表，16 音色）
  audio/ogg_encoder.cpp libvorbis 编码
  audio/vanilla_assets.cpp 解析 .minecraft/assets/indexes 提取原版音色 + stb_vorbis 解码 + 重采样渲染
  audio/stb_vorbis_impl.c stb_vorbis 唯一实现翻译单元
  resourcepack/disc.cpp 唱片资源包
  util/  fs / json(迷你解析器) / mc_versions(版本预设) / zip(gzip + 目录打包)
tests/  doctest; test_util.hpp 有 MidiMaker 手工构造 MIDI 字节
```

## 5. 剩余工作（按顺序）

1. **重新构建并修完编译错误**（上面 §2 的修复未验证；可能还有少量小错）
2. **跑 `m2n_tests`，修测试失败**。测试已写好: midi/model/nbt/layout/datapack/audio/e2e
3. **CLI 冒烟测试**: `m2n help` / `m2n info test.mid` / `m2n build test.mid --formats nbt,datapack,disc --zip`
4. **进游戏验证（需要用户配合）**:
   - 生成一个小 .nbt 机器，用结构方块放入 1.20.1 世界，拉杆触发，验证: 音符顺序正确、各泳道同步、乐器/音高正确
   - 数据包: `/function m2n_<id>:start` 验证音高（关键假设见 §7 playsound 基准音）
5. **M5 打磨**: README.md 重写（中文使用文档）、蛇形折叠 `--fold-width`（设计见 §6，尚未实现）、音色调准（合成器参数凭听感）、发布打包
6. git commit（尚未做过任何提交，工作树全是新文件）

## 6. 红石机器设计（布局核心，机制已对照 wiki 核实）

坐标: x=时间(东), z=泳道(南), y=-1 地板 / 0 结构层 / 1 总线层。

每条乐器轨一条"泳道"(pitch=3+laneGap): 音符盒行 z=bz±1, 支承行 z=bz(y=0), 总线 z=bz(y=1)。

- **步单元**: `[K 个红石粉][R 个中继器]`，中继器延迟和=gridTicks（`splitDelay()` 拆分）。脉冲前沿到达时整段红石粉同时点亮 → 红石粉弱充能下方支承块 → 触发两侧音符盒（每个红石粉单元 2 个音符盒槽，和弦用 K≥⌈n/2⌉）。空步只放中继器。
- **触发**: 西端 x=-2 拉杆 → x=-1 沿 z 的汇流排（每 13 格 1 个 delay=1 中继器防衰减，位置避开泳道总线 z）→ 各泳道入口。
- **同步补偿**: 泳道 i 头部前置中继器延迟 = maxTrunkDelay − trunkDelay(i)，保证所有泳道 step s 的绝对 tick 一致。
- **双保险**: 音符盒用 `note_block[instrument=..,note=..]` 方块状态，且下方地板块用对应乐器材质（gold_block=bell, packed_ice=chime, clay=flute, white_wool=guitar, bone_block=xylophone, iron_block=iron_xylophone, soul_sand=cow_bell, pumpkin=didgeridoo, emerald_block=bit, hay_block=banjo, glowstone=pling, oak_planks=bass, stone=basedrum, sand=snare, glass=hat, smooth_stone=harp）。
- **红石粉连接面**（north/south/east/west=side|none）在生成时显式计算写入，不依赖放置后方块更新。
- 中继器 facing: 泳道内 "east"，汇流排 "south"。
- 蛇形折叠（未实现）: 折叠宽度 W 步，机器在东端 U 形折返，每泳道竖向转接段长度相同（=bandPitch）故延迟一致、不失步；转接段 x 位置按泳道序号错开避免碰撞。

## 7. 关键技术假设（需进游戏验证）

- **playsound 音高**: `pitch = 2^((midiPitch − base − 12)/12)`，假设原版采样录制在各乐器音域中间（base+12）。这样 pitch 恰落在命令允许的 [0.5, 2.0]。若游戏内整体高/低八度，改 `instrumentBaseMidi()`。
- **乐器音高窗口**（wiki 核实）: harp 系 F#3(54)，bass/didgeridoo F#1(30)，guitar F#2(42)，flute F#4(66)，bell/chime/xylophone/iron_xylophone F#5(90)。note=0..24，八度适配按各窗口独立做。
- **下方材质→乐器** 对照表 §6 已核实（注意: bone_block→木琴、iron_block→铁木琴、wool→吉他，容易记反）。
- 唱片替换: 只替换 `assets/minecraft/sounds/records/<id>.ogg` 即生效；唱片弹出时长硬编码（pigstep 149s 等，disc.cpp 有表），歌长超出会被截断，工具会告警。
- 数据包计时: start 置 `$t=-1`，tick 函数先自增再分发，保证 tick 0 的音符能触发。1.21 起 function 目录改名（`functions`→`function`），由版本预设 `useNewFunctionDir` 控制。

## 8. 版本预设（util/mc_versions.cpp）

pack_format/DataVersion 已录: 1.19.4~1.21.4（data 12→61, resource 13→46, DataVersion 3120→4189）。更新版本需查 minecraft.wiki 的 Pack_format 与 Data_version 页面再补表。

## 9. 约定

- 全部源码/输出 UTF-8；CLI 输出中文；main.cpp 已设 Windows 控制台 65001
- GPLv3；依赖许可证均兼容（miniz MIT, stb 公有领域, doctest MIT, ogg/vorbis BSD）
- 注释密度低、只在讲"为什么"；命名空间 `m2n::*`；错误一律抛 `std::runtime_error` 由 CLI 捕获
- `.gitignore` 已含 build/out；junction `C:\m2n` 不属于仓库

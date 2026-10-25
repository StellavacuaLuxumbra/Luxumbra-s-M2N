# Luxumbra's M2N — 实现方案

C++20 控制台工具：读入 MIDI 文件，一键生成 Minecraft Java 1.20+ 的红石音乐全套产物。管线为 **MIDI 解析 → 统一歌曲模型 → 多导出器**，导出器彼此独立、可任意组合。

## MVP 三大输出（本期全部实现）

1. **红石结构 `.nbt`** — 真实音符盒+红石粉+中继器的物理机器，结构方块或 `/place template` 放入世界，生存可用
2. **数据包播放器** — 纯原版命令（`schedule` 自调度链 + `playsound`），逐 tick 精确、音准完美（playsound 的 pitch 参数覆盖全部半音），不需要资源包
3. **唱片资源包** — 内置合成器把歌曲渲染成音频，编码为 Ogg Vorbis，替换游戏唱片；可选读取用户本地 `.minecraft` 的原版音符盒音色获得真实音色

## 核心流水线与模块

```
src/
  main.cpp            入口：无参数→交互式向导；有参数→子命令 CLI
  cli/                参数解析 + 中文交互向导
  midi/               SMF 解析器（格式 0/1、running status、tempo map，手写零依赖）
  model/              Song 模型：量化到游戏 tick 网格、MIDI 音色→音符盒乐器映射、
                      八度适配（音符盒仅 25 音，按轨道折叠到 2 个八度内）、和弦拆分
  nbt/                NBT 写入器（结构方块格式：size/palette/blocks/DataVersion，gzip）
  structure/          红石机器布局生成器（核心算法模块）
  datapack/           schedule 链播放器生成器
  resourcepack/       唱片替换包生成器
  audio/              音符盒音色合成器（振荡器+包络参数表）、Ogg 编码、
                      可选原版音色加载（stb_vorbis 解码 .minecraft assets + 重采样）
  util/               zip 打包（miniz）、文件系统、版本预设表
```

## 关键设计（已对照 Minecraft Wiki 核实机制）

### 红石机器单元（structure/ 的基础）

利用已验证的充能规则：*红石粉弱充能其铺设的方块，弱充能方块触发相邻机械元件*：

```
y=1:      [dust][rep→][rep→][dust]      ← 时钟总线（铺在支承行上，沿 X 前进）
y=0, z=1:  石石石石石石石石（支承行）
y=0, z=0:  NB    NB  NB        NB       ← 音符盒行（与支承块相邻，被间接触发）
y=0, z=2:      NB      NB              ← 第二音符盒行（和弦/另一声部）
```

- 每个歌曲步（step）占用总线上一段中继器，其延迟之和 = 每步 tick 数（由 MIDI 速度换算，默认网格 2 tick，可 `--grid` 调整）；脉冲前沿到达该段时该段红石粉通电 → 相邻音符盒上升沿触发发声
- 乐器用 `note_block[instrument=…,note=N]` 方块状态（1.19.4+）；每条乐器轨一条三格宽的"泳道"，多轨平行排列
- 机器过长时蛇形折叠（`--fold-width`）；音符盒下方的地板用乐器对应材质（金块=钟琴、钻石块=叮当等）做彩色装饰地板，兼顾"精美建筑"
- 触发按钮/拉杆起振，尾部可选自动停止

### 数据包播放器

- `m2n:start` 启动 → `step/NNNN.mcfunction` 每步一个函数，函数末尾 `schedule function m2n:step/NNNN+1 <grid>t` 自链推进；支持循环、停止（`schedule clear`）
- 每个音符 = `playsound block.note_block.<instrument> record @a ~ ~ ~ 1 <pitch>`（pitch=2^(半音/12)，任意音高无损）
- 可选粒子音符特效；`pack.mcmeta` 按 `--mc 1.20.1|1.21|…` 预设表生成对应 `pack_format` 与 DataVersion

### 唱片资源包

- 合成器：以参数表描述 ~17 种音符盒音色（harp=正弦+衰减、snare=噪声、basedrum=正弦扫频、bit=方波等），按音符时间轴混音 44.1kHz 立体声
- libvorbis+libogg（FetchContent）编码；替换 `sounds/records/<唱片>.ogg`，`--disc` 选择替换哪张唱片
- 已知限制写入文档：原版唱片弹出时长硬编码，需选择时长≥歌曲的唱片（1.21+ 自定义 `jukebox_song` 留作后续版本）
- `--sample-mode vanilla`：解析 `.minecraft/assets/indexes` 哈希索引，用 stb_vorbis 解码原版音色并按音高重采样（用户本机文件，无版权分发问题）

## CLI 设计

```
m2n info <file.mid>                          查看 MIDI 内容（轨道/音色/时长）
m2n build <file.mid> [-o 目录] [--formats nbt,datapack,disc]
          [--mc 1.20.1] [--grid 2] [--loop] [--disc pigstep]
          [--sample-mode synth|vanilla] [--vanilla-dir <path>] [--fold-width 64]
m2n（无参数）                                中文交互式向导
```

## 依赖（全部 CMake FetchContent，GPLv3 兼容）

miniz（gzip+zip）、libogg+libvorbis（编码）、stb_vorbis（解码原版音色）、doctest（测试）。

## 里程碑

1. **M1 骨架**：CMake、CLI/info、SMF 解析器、Song 模型（量化/乐器映射/八度适配）+ 单元测试
2. **M2 结构**：NBT 写入器、红石布局生成、`.nbt` 导出；先产出 8 步最小测试结构供进游戏验证，再扩展折叠/装饰
3. **M3 数据包**：schedule 播放器生成 + 长歌压力测试
4. **M4 音频**：合成器、Ogg 编码、唱片包
5. **M5 打磨**：交互向导、zip 打包产物、README（中文，含游戏内使用说明）、端到端测试

## 测试

doctest 单测（解析器用手工构造的 MIDI 字节夹具、NBT 黄金字节比对、量化与八度适配性质测试、布局不变量：无重叠方块/中继器延迟和=每步 tick 数/音符 0–24）；E2E：对测试 MIDI 跑完整 CLI 并校验产物结构。红石机器的正确性以「最小结构进游戏实测」为验收标准。

## 风险与对策

- **红石布局 bug**：M2 先只做 8 步直线最小机器进游戏验证，通过后再做折叠/装饰等复杂特性
- **libvorbis FetchContent 构建问题**：备选方案是 vendored 源码入仓或改用 vcpkg
- **唱片时长硬编码**：文档说明 + 工具自动按歌曲时长推荐可替换的唱片

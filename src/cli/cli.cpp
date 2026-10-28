#include "cli/cli.hpp"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "app.hpp"
#include "version.hpp"

namespace m2n::cli {

namespace {

void printHelp() {
  std::cout <<
      R"(Luxumbra's M2N v)" M2N_VERSION R"( - MIDI -> Minecraft 音符盒红石音乐转换器

用法:
  m2n                          交互式向导
  m2n info <文件.mid>          查看 MIDI 文件内容
  m2n build <文件.mid> [选项]  生成红石音乐产物
  m2n help                     显示本帮助

build 选项:
  -o, --out <目录>       输出目录 (默认: out/<歌曲名>)
      --formats <列表>   产物类型, 逗号分隔 (默认: nbt,datapack)
                          nbt       红石结构 .nbt (结构方块 / place template)
                          datapack  数据包播放器 (原版命令精确播放)
                          disc      唱片资源包 (渲染音频替换唱片)
      --mc <版本>        目标 MC 版本 (默认 1.20.1, 如 1.20.4 / 1.21.4)
      --grid <tick>      每步游戏 tick 数, 20 tick=1秒 (默认 2; 越小越快)
      --max-lanes <n>    最多乐器轨数 (默认 16)
      --loop             数据包循环播放
      --particles        数据包播放时显示音符粒子
      --disc <唱片>      唱片资源包替换的唱片 (默认 pigstep)
      --vanilla-samples  用本地 .minecraft 原版音色渲染唱片音频
      --vanilla-dir <目录> .minecraft 目录 (默认自动探测)
      --no-floor         结构不生成装饰地板
      --fold-width <n>   蛇形折叠: 每条"带"的步数 (0=不折叠, 越小机器越短)
      --zip              同时打包 zip
)";
}

}  // namespace

int run(const std::vector<std::string>& args) {
  if (args.empty()) return app::wizard();

  const std::string& cmd = args[0];
  std::vector<std::string> rest(args.begin() + 1, args.end());

  if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    printHelp();
    return 0;
  }
  if (cmd == "version" || cmd == "--version" || cmd == "-v") {
    std::cout << M2N_NAME << " v" << M2N_VERSION << "\n";
    return 0;
  }
  if (cmd == "info") {
    if (rest.empty()) {
      std::cerr << "用法: m2n info <文件.mid>\n";
      return 1;
    }
    return app::info(rest[0]);
  }
  if (cmd == "build") {
    if (rest.empty()) {
      std::cerr << "用法: m2n build <文件.mid> [选项]\n";
      return 1;
    }
    std::vector<std::string> opts(rest.begin() + 1, rest.end());
    return app::build(rest[0], opts);
  }
  if (cmd == "wizard") return app::wizard();

  std::cerr << "未知命令: " << cmd << "\n";
  printHelp();
  return 1;
}

}  // namespace m2n::cli

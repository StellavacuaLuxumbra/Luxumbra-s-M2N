#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "cli/cli.hpp"

int main(int argc, char** argv) {
#ifdef _WIN32
  // Windows 控制台输出 UTF-8
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);
#endif

  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
  for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

  try {
    return m2n::cli::run(args);
  } catch (const std::exception& e) {
    std::fputs(("错误: " + std::string(e.what()) + "\n").c_str(), stderr);
    return 1;
  }
}

#pragma once
// 命令行接口: 子命令 + 交互式向导

#include <string>
#include <vector>

namespace m2n::cli {

// 解析并执行; 返回退出码
int run(const std::vector<std::string>& args);

}  // namespace m2n::cli

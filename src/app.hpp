#pragma once
// 应用逻辑: info / build / 交互式向导

#include <string>
#include <vector>

namespace m2n::app {

int info(const std::string& midiPath);
int build(const std::string& midiPath, const std::vector<std::string>& args);
int wizard();

}  // namespace m2n::app

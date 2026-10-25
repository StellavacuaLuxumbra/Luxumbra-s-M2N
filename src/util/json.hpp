#pragma once
// 极简 JSON 解析器 (只需解析 .minecraft 的资源索引, 不需要序列化)
// 解析结果为 Json 值树; 对象保持插入序

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace m2n::util::json {

class Value;
using ValuePtr = std::shared_ptr<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
 public:
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string str;
  std::vector<ValuePtr> array;
  std::vector<std::pair<std::string, ValuePtr>> object;  // 保持插入序

  bool isObject() const { return type == Type::Object; }
  bool isString() const { return type == Type::String; }

  // 对象取值; 不存在返回 nullptr
  const Value* get(std::string_view key) const {
    for (const auto& [k, v] : object)
      if (k == key) return v.get();
    return nullptr;
  }
};

// 解析 JSON 文本; 语法错误抛 std::runtime_error
ValuePtr parse(std::string_view text);

}  // namespace m2n::util::json

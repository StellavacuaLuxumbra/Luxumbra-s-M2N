#include "util/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace m2n::util::json {

namespace {

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  ValuePtr parse() {
    skipWs();
    auto v = parseValue();
    skipWs();
    if (pos_ != text_.size()) throw std::runtime_error("JSON 末尾有多余内容");
    return v;
  }

 private:
  std::string_view text_;
  size_t pos_ = 0;

  void skipWs() {
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        ++pos_;
      else
        break;
    }
  }

  char peek() const {
    if (pos_ >= text_.size()) throw std::runtime_error("JSON 意外结束");
    return text_[pos_];
  }

  void expect(char c) {
    if (pos_ >= text_.size() || text_[pos_] != c)
      throw std::runtime_error(std::string("JSON 期望字符 '") + c + "'");
    ++pos_;
  }

  bool consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  void literal(std::string_view word) {
    if (text_.compare(pos_, word.size(), word) != 0)
      throw std::runtime_error("JSON 无效字面量");
    pos_ += word.size();
  }

  ValuePtr parseValue() {
    char c = peek();
    switch (c) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': {
        auto v = std::make_shared<Value>();
        v->type = Type::String;
        v->str = parseString();
        return v;
      }
      case 't': {
        literal("true");
        auto v = std::make_shared<Value>();
        v->type = Type::Bool;
        v->boolean = true;
        return v;
      }
      case 'f': {
        literal("false");
        auto v = std::make_shared<Value>();
        v->type = Type::Bool;
        v->boolean = false;
        return v;
      }
      case 'n': {
        literal("null");
        return std::make_shared<Value>();
      }
      default: {
        auto v = std::make_shared<Value>();
        v->type = Type::Number;
        v->number = parseNumber();
        return v;
      }
    }
  }

  ValuePtr parseObject() {
    auto v = std::make_shared<Value>();
    v->type = Type::Object;
    expect('{');
    skipWs();
    if (consume('}')) return v;
    while (true) {
      skipWs();
      std::string key = parseString();
      skipWs();
      expect(':');
      skipWs();
      v->object.emplace_back(std::move(key), parseValue());
      skipWs();
      if (consume(',')) continue;
      expect('}');
      break;
    }
    return v;
  }

  ValuePtr parseArray() {
    auto v = std::make_shared<Value>();
    v->type = Type::Array;
    expect('[');
    skipWs();
    if (consume(']')) return v;
    while (true) {
      skipWs();
      v->array.push_back(parseValue());
      skipWs();
      if (consume(',')) continue;
      expect(']');
      break;
    }
    return v;
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (true) {
      if (pos_ >= text_.size()) throw std::runtime_error("JSON 字符串未闭合");
      char c = text_[pos_++];
      if (c == '"') break;
      if (c == '\\') {
        if (pos_ >= text_.size()) throw std::runtime_error("JSON 转义未闭合");
        char e = text_[pos_++];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (pos_ + 4 > text_.size()) throw std::runtime_error("JSON \\u 转义不完整");
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              char h = text_[pos_++];
              code <<= 4;
              if (h >= '0' && h <= '9')
                code |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f')
                code |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F')
                code |= static_cast<unsigned>(h - 'A' + 10);
              else
                throw std::runtime_error("JSON \\u 转义非法");
            }
            // UTF-8 编码 (不考虑代理对: 资源索引里不会出现)
            if (code < 0x80) {
              out += static_cast<char>(code);
            } else if (code < 0x800) {
              out += static_cast<char>(0xC0 | (code >> 6));
              out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
              out += static_cast<char>(0xE0 | (code >> 12));
              out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              out += static_cast<char>(0x80 | (code & 0x3F));
            }
            break;
          }
          default: throw std::runtime_error("JSON 非法转义");
        }
      } else {
        out += c;
      }
    }
    return out;
  }

  double parseNumber() {
    size_t start = pos_;
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
        ++pos_;
      else
        break;
    }
    if (start == pos_) throw std::runtime_error("JSON 数字非法");
    return std::strtod(std::string(text_.substr(start, pos_ - start)).c_str(), nullptr);
  }
};

}  // namespace

ValuePtr parse(std::string_view text) {
  return Parser(text).parse();
}

}  // namespace m2n::util::json

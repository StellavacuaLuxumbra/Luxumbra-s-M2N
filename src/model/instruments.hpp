#pragma once
// 音符盒乐器: 枚举 / 方块状态 id / playsound 事件 / 装饰地板材质 / 音高基准
// 以及 GM (General MIDI) 音色 -> 乐器的映射

#include <cstdint>
#include <string_view>

namespace m2n::model {

// Java 1.20 音符盒支持的 16 种经典乐器 (不含 1.20 头颅/1.21 铜管扩展)
enum class Instrument : uint8_t {
  Harp,
  Bass,
  BaseDrum,
  Snare,
  Hat,
  Bell,
  Chime,
  Flute,
  Guitar,
  Xylophone,
  IronXylophone,
  CowBell,
  Didgeridoo,
  Bit,
  Banjo,
  Pling,
  Count
};

// 方块状态 id, 如 "harp" -> note_block[instrument=harp]
std::string_view instrumentId(Instrument ins);
// 中文名
std::string_view instrumentNameZh(Instrument ins);
// playsound 音效事件, 如 "minecraft:block.note_block.harp"
std::string_view instrumentSound(Instrument ins);
// 装饰地板方块 (同时是"方块下方材质决定乐器"的正确材质, 双保险)
std::string_view instrumentFloorBlock(Instrument ins);
// 该乐器 note=0 对应的绝对 MIDI 音高 (F#3=54 等)
// harp 系 54, bass/didgeridoo 30, guitar 42, flute 66, 钟琴类 90, 打击乐 54
int instrumentBaseMidi(Instrument ins);
// 是否有音高 (打击乐器为假)
bool instrumentPitched(Instrument ins);

// GM 音色 (0-127) -> 乐器
Instrument mapGmProgram(uint8_t program);
// GM 打击组 (channel 10) 音符 -> 乐器
Instrument mapDrumNote(uint8_t note);

}  // namespace m2n::model

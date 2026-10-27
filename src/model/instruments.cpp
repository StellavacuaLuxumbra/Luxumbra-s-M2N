#include "model/instruments.hpp"

namespace m2n::model {

namespace {

struct InstrumentInfo {
  std::string_view id;
  std::string_view nameZh;
  std::string_view sound;       // playsound 事件 (不含 minecraft: 前缀)
  std::string_view floorBlock;  // 决定乐器的下方材质 (兼装饰地板)
  int baseMidi;                 // note=0 的绝对 MIDI 音高
  bool pitched;
};

// 下方材质对照 minecraft.wiki/w/Note_Block
constexpr InstrumentInfo kTable[] = {
    {"harp",          "竖琴",     "block.note_block.harp",          "smooth_stone", 54, true},
    {"bass",          "贝斯(低音)", "block.note_block.bass",         "oak_planks",   30, true},
    {"basedrum",      "底鼓",     "block.note_block.basedrum",      "stone",        54, false},
    {"snare",         "小军鼓",   "block.note_block.snare",         "sand",         54, false},
    {"hat",           "踩镲",     "block.note_block.hat",           "glass",        54, false},
    {"bell",          "钟琴",     "block.note_block.bell",          "gold_block",   90, true},
    {"chime",         "冰铃声",   "block.note_block.chime",         "packed_ice",   90, true},
    {"flute",         "长笛",     "block.note_block.flute",         "clay",         66, true},
    {"guitar",        "吉他",     "block.note_block.guitar",        "white_wool",   42, true},
    {"xylophone",     "木琴",     "block.note_block.xylophone",     "bone_block",   90, true},
    {"iron_xylophone","铁木琴",   "block.note_block.iron_xylophone","iron_block",   90, true},
    {"cow_bell",      "牛铃",     "block.note_block.cow_bell",      "soul_sand",    54, true},
    {"didgeridoo",    "迪吉里杜管", "block.note_block.didgeridoo",  "pumpkin",      30, true},
    {"bit",           "方波",     "block.note_block.bit",           "emerald_block",54, true},
    {"banjo",         "班卓琴",   "block.note_block.banjo",         "hay_block",    54, true},
    {"pling",         "电钢琴",   "block.note_block.pling",         "glowstone",    54, true},
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Instrument::Count));

// GM 音色组 -> 乐器
constexpr Instrument kGmMap[128] = {
    /* 0 钢琴 */ Instrument::Harp,
    /* 1 */ Instrument::Pling,
    /* 2 */ Instrument::Pling,
    /* 3 */ Instrument::Harp,
    /* 4 */ Instrument::Pling,
    /* 5 */ Instrument::Pling,
    /* 6 */ Instrument::Banjo,
    /* 7 */ Instrument::Pling,
    /* 8-15 钟琴/颤音琴等 */
    Instrument::Chime, Instrument::Bell, Instrument::Chime, Instrument::Chime,
    Instrument::Xylophone, Instrument::Xylophone, Instrument::Bell, Instrument::Banjo,
    /* 16-23 风琴 */
    Instrument::Bit, Instrument::Bit, Instrument::Bit, Instrument::Bit,
    Instrument::Bit, Instrument::Bit, Instrument::Bit, Instrument::Bit,
    /* 24-31 吉他 */
    Instrument::Guitar, Instrument::Guitar, Instrument::Banjo, Instrument::Guitar,
    Instrument::Banjo, Instrument::Guitar, Instrument::Guitar, Instrument::Chime,
    /* 32-39 贝斯 */
    Instrument::Bass, Instrument::Bass, Instrument::Bass, Instrument::Bass,
    Instrument::Bass, Instrument::Bass, Instrument::Bass, Instrument::Bass,
    /* 40-47 弦乐 */
    Instrument::Harp, Instrument::Harp, Instrument::Harp, Instrument::Bass,
    Instrument::Harp, Instrument::Guitar, Instrument::Harp, Instrument::BaseDrum,
    /* 48-55 合唱/合奏 */
    Instrument::Chime, Instrument::Chime, Instrument::Chime, Instrument::Chime,
    Instrument::Chime, Instrument::Chime, Instrument::Chime, Instrument::Chime,
    /* 56-63 铜管 */
    Instrument::Didgeridoo, Instrument::Didgeridoo, Instrument::Didgeridoo,
    Instrument::Didgeridoo, Instrument::Didgeridoo, Instrument::Didgeridoo,
    Instrument::Didgeridoo, Instrument::Didgeridoo,
    /* 64-71 簧管 */
    Instrument::Flute, Instrument::Flute, Instrument::Flute, Instrument::Flute,
    Instrument::Flute, Instrument::Flute, Instrument::Flute, Instrument::Flute,
    /* 72-79 管乐 */
    Instrument::Flute, Instrument::Flute, Instrument::Flute, Instrument::Flute,
    Instrument::Flute, Instrument::Flute, Instrument::Flute, Instrument::Flute,
    /* 80-87 合成主音 */
    Instrument::Bit, Instrument::Bit, Instrument::Bit, Instrument::Chime,
    Instrument::Bit, Instrument::Bit, Instrument::Bit, Instrument::Bass,
    /* 88-95 合成铺底 */
    Instrument::Chime, Instrument::Chime, Instrument::Bit, Instrument::Chime,
    Instrument::Chime, Instrument::Chime, Instrument::Chime, Instrument::Chime,
    /* 96-103 音效 */
    Instrument::Chime, Instrument::Guitar, Instrument::Chime, Instrument::Chime,
    Instrument::Chime, Instrument::Didgeridoo, Instrument::Chime, Instrument::Bit,
    /* 104-111 民族 */
    Instrument::Banjo, Instrument::Banjo, Instrument::Banjo, Instrument::Guitar,
    Instrument::Chime, Instrument::Didgeridoo, Instrument::Harp, Instrument::Didgeridoo,
    /* 112-119 打击乐器主音 */
    Instrument::Bell, Instrument::IronXylophone, Instrument::IronXylophone,
    Instrument::Xylophone, Instrument::BaseDrum, Instrument::BaseDrum,
    Instrument::BaseDrum, Instrument::Snare,
    /* 120-127 音效 */
    Instrument::Banjo, Instrument::Flute, Instrument::Flute, Instrument::Flute,
    Instrument::Bit, Instrument::Snare, Instrument::Snare, Instrument::BaseDrum,
};

}  // namespace

std::string_view instrumentId(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].id;
}
std::string_view instrumentNameZh(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].nameZh;
}
std::string_view instrumentSound(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].sound;
}
std::string_view instrumentFloorBlock(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].floorBlock;
}
int instrumentBaseMidi(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].baseMidi;
}
bool instrumentPitched(Instrument ins) {
  return kTable[static_cast<size_t>(ins)].pitched;
}

Instrument mapGmProgram(uint8_t program) {
  return kGmMap[program];
}

std::optional<Instrument> instrumentFromId(std::string_view id) {
  for (size_t i = 0; i < std::size(kTable); ++i)
    if (id == kTable[i].id) return static_cast<Instrument>(i);
  return std::nullopt;
}

Instrument mapDrumNote(uint8_t note) {
  // GM 打击组常用映射
  switch (note) {
    // 底鼓 / 中低音鼓
    case 35: case 36: case 41: case 43: case 45: case 47: case 48: case 50:
      return Instrument::BaseDrum;
    // 小军鼓 / 拍手 / 邦戈与康加鼓类
    case 37: case 38: case 39: case 40: case 60: case 61: case 62: case 63:
    case 64: case 65: case 66: case 67: case 68: case 69: case 80: case 81:
    case 82: case 83: case 84: case 85: case 86: case 87: case 91: case 92:
      return Instrument::Snare;
    // 镲 / 铃鼓 / 沙锤
    case 42: case 44: case 46: case 49: case 51: case 52: case 55: case 57:
    case 70: case 71: case 88: case 89: case 90:
      return Instrument::Hat;
    // 牛铃
    case 53: case 56:
      return Instrument::CowBell;
    // 颤音滑奏
    case 58:
      return Instrument::IronXylophone;
    // 口哨
    case 72: case 73:
      return Instrument::Flute;
    default:
      if (note >= 74 && note <= 79) return Instrument::IronXylophone;  // 木鱼/咔哒
      return Instrument::BaseDrum;
  }
}

}  // namespace m2n::model

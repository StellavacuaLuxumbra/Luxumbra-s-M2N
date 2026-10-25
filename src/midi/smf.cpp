#include "midi/smf.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace m2n::midi {

namespace {

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  size_t pos() const { return pos_; }
  size_t remaining() const { return size_ - pos_; }
  void seek(size_t pos) { pos_ = pos; }

  uint8_t u8() {
    if (pos_ >= size_) throw std::runtime_error("MIDI 文件意外结束");
    return data_[pos_++];
  }

  void unread() {
    // running status 时把误读的数据字节退回
    if (pos_ > 0) --pos_;
  }

  uint32_t u32() {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | u8();
    return v;
  }

  void expect(const char* magic) {
    for (const char* p = magic; *p; ++p)
      if (u8() != static_cast<uint8_t>(*p))
        throw std::runtime_error(std::string("MIDI 块标识不符, 期望 ") + magic);
  }

  uint32_t vlq() {  // 变长数
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      uint8_t b = u8();
      v = (v << 7) | (b & 0x7F);
      if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("MIDI 变长数过长");
  }

  void skip(size_t n) {
    if (pos_ + n > size_) throw std::runtime_error("MIDI 文件意外结束");
    pos_ += n;
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
};

struct NoteOn {
  int tick;
  uint8_t velocity;
  uint8_t program;
};

}  // namespace

double MidiData::tickToSec(int tick) const {
  if (smpte) return tick * secPerTick;
  if (tempoMap.empty()) return tick * 0.5 / division;
  double sec = 0;
  int lastTick = 0;
  int lastUs = tempoMap.front().second;
  for (const auto& [t, us] : tempoMap) {
    if (t >= tick) break;
    sec += (t - lastTick) * static_cast<double>(lastUs) / 1e6 / division;
    lastTick = t;
    lastUs = us;
  }
  sec += (tick - lastTick) * static_cast<double>(lastUs) / 1e6 / division;
  return sec;
}

MidiData parseSmf(const uint8_t* data, size_t size) {
  Reader r(data, size);

  r.expect("MThd");
  uint32_t headerLen = r.u32();
  if (headerLen < 6) throw std::runtime_error("MIDI 头部长度非法");
  MidiData md;
  md.format = r.u16();
  int ntrks = r.u16();
  int division = static_cast<int16_t>(r.u16());
  r.skip(headerLen - 6);
  if (md.format > 2) throw std::runtime_error("不支持的 MIDI 格式编号");
  if (ntrks <= 0) throw std::runtime_error("MIDI 无轨道");

  if (division & 0x8000) {
    md.smpte = true;
    int fps = 256 - (division >> 8);  // 24/25/29.97/30 (29.97 存成 29, 误差可接受)
    int tpf = division & 0xFF;
    if (tpf <= 0 || fps <= 0) throw std::runtime_error("MIDI SMPTE 时间非法");
    md.secPerTick = 1.0 / (fps * static_cast<double>(tpf));
  } else {
    if (division <= 0) throw std::runtime_error("MIDI division 非法");
    md.division = division;
  }

  std::vector<std::pair<int, int>> tempos;  // (tick, usPerQuarter)

  struct OpenNoteEvent {
    int endTick;
    int startTickIdx;  // 下标到 noteOns
  };
  // 先以 tick 记录, tempo map 完成后再换算秒
  struct PendingNote {
    int startTick;
    int endTick;
    uint8_t pitch;
    uint8_t velocity;
    uint8_t program;
    uint8_t channel;
    int trackIndex;
    bool drum;
  };
  std::vector<PendingNote> pending;

  size_t headerEnd = r.pos();

  for (int trackIndex = 0; trackIndex < ntrks; ++trackIndex) {
    // 找到下一个 MTrk 块 (忽略其他块)
    bool found = false;
    uint32_t len = 0;
    while (r.remaining() >= 8) {
      uint8_t m[4];
      for (auto& b : m) b = r.u8();
      len = r.u32();
      if (m[0] == 'M' && m[1] == 'T' && m[2] == 'r' && m[3] == 'k') {
        found = true;
        break;
      }
      r.skip(len);
    }
    if (!found) break;
    size_t chunkEnd = r.pos() + len;

    TrackInfo info;
    info.index = trackIndex;

    uint8_t programPerChannel[16] = {};
    // (channel, pitch) -> 打开音符栈
    std::array<std::array<std::vector<NoteOn>, 128>, 16> open;
    int noteCount = 0;
    int tick = 0;
    uint8_t status = 0;

    auto closeNote = [&](uint8_t ch, uint8_t pitch, int endTick) {
      auto& q = open[ch][pitch];
      if (q.empty()) return;
      NoteOn n = q.front();
      q.erase(q.begin());
      pending.push_back({n.tick, endTick, pitch, n.velocity, n.program, ch,
                         static_cast<uint8_t>(trackIndex), ch == 9});
      noteCount++;
    };

    while (r.pos() < chunkEnd) {
      tick += static_cast<int>(r.vlq());
      uint8_t b = r.u8();
      if (b == 0xFF) {  // meta
        uint8_t type = r.u8();
        uint32_t l = r.vlq();
        if (type == 0x2F) {  // End of Track
          break;
        }
        if (type == 0x03) {  // 轨道名
          std::string name;
          for (uint32_t i = 0; i < l; ++i) name += static_cast<char>(r.u8());
          if (info.name.empty()) info.name = name;
        } else if (type == 0x51 && l == 3) {  // Set Tempo
          uint32_t us = 0;
          for (int i = 0; i < 3; ++i) us = (us << 8) | r.u8();
          if (!md.smpte) tempos.push_back({tick, static_cast<int>(us)});
        } else {
          r.skip(l);
        }
      } else if (b == 0xF0 || b == 0xF7) {  // SysEx
        uint32_t l = r.vlq();
        r.skip(l);
      } else {
        if (b & 0x80)
          status = b;
        else
          r.unread();  // running status
        uint8_t kind = status & 0xF0;
        uint8_t ch = status & 0x0F;
        switch (kind) {
          case 0x80: {  // Note Off
            uint8_t pitch = r.u8();
            r.u8();
            closeNote(ch, pitch, tick);
            break;
          }
          case 0x90: {  // Note On (vel 0 = Note Off)
            uint8_t pitch = r.u8();
            uint8_t vel = r.u8();
            if (vel == 0) {
              closeNote(ch, pitch, tick);
            } else {
              open[ch][pitch].push_back({tick, vel, programPerChannel[ch]});
            }
            break;
          }
          case 0xA0:
          case 0xB0:
          case 0xE0:
            r.skip(2);
            break;
          case 0xC0:  // Program Change
            programPerChannel[ch] = r.u8();
            break;
          case 0xD0:
            r.skip(1);
            break;
          default:
            throw std::runtime_error("MIDI 轨道数据损坏");
        }
      }
    }
    // 轨道结束仍未关闭的音符: 补 0.25 拍时值
    for (int ch = 0; ch < 16; ++ch) {
      for (int p = 0; p < 128; ++p) {
        while (!open[ch][p].empty()) closeNote(static_cast<uint8_t>(ch), static_cast<uint8_t>(p),
                                               tick + division / 4);
      }
    }
    info.noteCount = noteCount;
    md.tracks.push_back(info);

    r.seek(chunkEnd);
  }

  // tempo map: 排序, 同 tick 取最后一个
  std::sort(tempos.begin(), tempos.end());
  for (const auto& [t, us] : tempos) {
    if (!md.tempoMap.empty() && md.tempoMap.back().first == t)
      md.tempoMap.back().second = us;
    else
      md.tempoMap.push_back({t, us});
  }
  if (md.tempoMap.empty() || md.tempoMap.front().first > 0)
    md.tempoMap.insert(md.tempoMap.begin(), {0, 500000});

  // tick -> 秒, 生成最终音符
  md.notes.reserve(pending.size());
  for (const auto& p : pending) {
    RawNote n;
    n.startSec = md.tickToSec(p.startTick);
    double endSec = md.tickToSec(p.endTick);
    n.durationSec = endSec > n.startSec ? endSec - n.startSec : 0.05;
    n.pitch = p.pitch;
    n.velocity = p.velocity;
    n.program = p.program;
    n.channel = p.channel;
    n.trackIndex = p.trackIndex;
    n.drum = p.drum;
    md.notes.push_back(n);
    double end = n.startSec + n.durationSec;
    if (end > md.durationSec) md.durationSec = end;
    if (md.firstNoteSec < 0 || n.startSec < md.firstNoteSec) md.firstNoteSec = n.startSec;
  }
  std::sort(md.notes.begin(), md.notes.end(),
            [](const RawNote& a, const RawNote& b) { return a.startSec < b.startSec; });
  return md;
}

}  // namespace m2n::midi

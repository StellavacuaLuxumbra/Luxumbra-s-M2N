#include "model/convert.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <sstream>

namespace m2n::model {

namespace {

// 八度适配: 为一组音高选择 12 的倍数位移, 使尽量多的音落在 [lo, lo+24] 窗口内
// 返回 (位移, 被钳制的音符数)
std::pair<int, int> fitOctaves(const std::vector<uint8_t>& pitches, int lo) {
  int hi = lo + 24;
  long bestCost = -1;
  int bestShift = 0;
  for (int shift = -60; shift <= 60; shift += 12) {
    long cost = 0;
    for (uint8_t p : pitches) {
      int q = p + shift;
      if (q < lo) cost += lo - q;
      else if (q > hi) cost += q - hi;
    }
    // 平局时偏向小位移 (听感改变少)
    if (bestCost < 0 || cost < bestCost ||
        (cost == bestCost && std::abs(shift) < std::abs(bestShift))) {
      bestCost = cost;
      bestShift = shift;
    }
  }
  int clamped = 0;
  for (uint8_t p : pitches) {
    int q = p + bestShift;
    if (q < lo || q > lo + 24) ++clamped;
  }
  return {bestShift, clamped};
}

}  // namespace

ConvertResult convert(const midi::MidiData& midi, ConvertOptions options) {
  ConvertResult result;
  Song& song = result.song;
  song.gridTicks = std::max(1, options.gridTicks);
  song.durationSec = midi.durationSec;
  song.name = midi.notes.empty() ? "untitled" : song.name;

  // 先按乐器分组 (保留原始音高供八度适配)
  struct Group {
    Instrument instrument;
    std::vector<const midi::RawNote*> notes;
  };
  std::map<Instrument, Group> groups;
  for (const auto& n : midi.notes) {
    Instrument ins = n.drum ? mapDrumNote(n.pitch) : mapGmProgram(n.program);
    groups[ins].instrument = ins;
    groups[ins].notes.push_back(&n);
  }

  // 超过轨道上限: 保留音符数最多的 maxLanes 条, 其余丢弃并告警
  std::vector<Group> kept;
  for (auto& [ins, g] : groups) kept.push_back(std::move(g));
  std::sort(kept.begin(), kept.end(),
            [](const Group& a, const Group& b) { return a.notes.size() > b.notes.size(); });
  if (static_cast<int>(kept.size()) > options.maxLanes) {
    size_t dropped = 0;
    for (size_t i = options.maxLanes; i < kept.size(); ++i) dropped += kept[i].notes.size();
    kept.resize(options.maxLanes);
    std::ostringstream oss;
    oss << "乐器轨数超过上限 " << options.maxLanes << ", 丢弃了 " << dropped << " 个音符";
    result.warnings.push_back({oss.str()});
  }

  // 确定首个音符时间偏移
  double leadIn = 0;
  if (options.trimLeadIn && midi.firstNoteSec >= 0) leadIn = midi.firstNoteSec;

  int totalNotes = 0;
  for (auto& g : kept) {
    Lane lane;
    lane.instrument = g.instrument;
    lane.sourceDesc = std::string(instrumentNameZh(g.instrument));

    std::vector<uint8_t> pitches;
    pitches.reserve(g.notes.size());
    for (const auto* n : g.notes) pitches.push_back(n->pitch);

    int base = instrumentBaseMidi(g.instrument);
    if (instrumentPitched(g.instrument)) {
      auto [shift, clamped] = fitOctaves(pitches, base);
      lane.octaveShift = shift;
      lane.clampedNotes = clamped;
      if (clamped > 0) {
        std::ostringstream oss;
        oss << "乐器 [" << lane.sourceDesc << "] 音域超过 2 个八度, "
            << clamped << " 个音符被钳制到边界";
        result.warnings.push_back({oss.str()});
      }
    }

    for (const auto* n : g.notes) {
      int gameTick = static_cast<int>(std::lround((n->startSec - leadIn) * 20.0));
      int step = static_cast<int>(std::llround(static_cast<double>(gameTick) / song.gridTicks));
      if (step < 0) step = 0;
      int absPitch = static_cast<int>(n->pitch) + lane.octaveShift;
      int rel = absPitch - base;
      if (rel < 0) rel = 0;
      if (rel > 24) rel = 24;
      StepNote sn;
      sn.step = step;
      sn.nbNote = static_cast<uint8_t>(rel);
      sn.midiPitch = base + rel;
      sn.velocity = n->velocity;
      sn.durationSec = n->durationSec;
      lane.notes.push_back(sn);
    }

    // 去重: 同一步同音高只保留一个
    std::sort(lane.notes.begin(), lane.notes.end(), [](const StepNote& a, const StepNote& b) {
      return a.step != b.step ? a.step < b.step : a.nbNote < b.nbNote;
    });
    lane.notes.erase(std::unique(lane.notes.begin(), lane.notes.end(),
                                 [](const StepNote& a, const StepNote& b) {
                                   return a.step == b.step && a.nbNote == b.nbNote;
                                 }),
                     lane.notes.end());

    totalNotes += static_cast<int>(lane.notes.size());
    if (!lane.notes.empty())
      song.totalSteps = std::max(song.totalSteps, lane.notes.back().step + 1);
    song.lanes.push_back(std::move(lane));
  }

  song.totalNotes = totalNotes;
  return result;
}

}  // namespace m2n::model

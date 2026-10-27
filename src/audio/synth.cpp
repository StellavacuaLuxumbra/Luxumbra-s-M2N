#include "audio/synth.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace m2n::audio {

constexpr double kPi = 3.14159265358979323846;

namespace {

// 简易音色模型: 谐波叠加 + 包络
enum class Osc { Sine, Square, Saw, Triangle, Noise, Sweep };

struct Partial {
  Osc osc;
  double ratio;   // 相对基频的倍率 (非整数可做出金属感)
  double gain;
};

struct VoiceParams {
  const char* name;
  std::vector<Partial> partials;
  double attackSec;   // 起音
  double decaySec;    // 衰减到 1e-3
  bool sustained;     // 按音符时值持续 (长笛/迪吉里杜管)
  double releaseSec;  // sustained 时的收尾
  double pan;         // -1 左 .. 1 右
};

// 各乐器合成参数 (凭听感近似原版音符盒)
std::vector<VoiceParams> makeParams() {
  return {
      {"harp", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 2.0, 0.32}}, 0.003, 0.95, false, 0.05, -0.25},
      {"bass", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 2.0, 0.18}}, 0.005, 0.5, false, 0.05, 0.15},
      {"basedrum", {{Osc::Sweep, 1.0, 1.0}, {Osc::Noise, 1.0, 0.12}}, 0.001, 0.28, false, 0.02, 0.0},
      {"snare", {{Osc::Noise, 1.0, 0.8}, {Osc::Sine, 1.0, 0.30}}, 0.001, 0.19, false, 0.02, 0.0},
      {"hat", {{Osc::Noise, 1.0, 0.45}}, 0.001, 0.09, false, 0.01, 0.3},
      {"bell", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 2.76, 0.24}, {Osc::Sine, 5.40, 0.08}},
       0.002, 1.8, false, 0.1, -0.4},
      {"chime", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 3.01, 0.14}}, 0.002, 1.4, false, 0.1, 0.45},
      {"flute", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 2.0, 0.12}}, 0.04, 0.35, true, 0.18, 0.35},
      {"guitar", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 2.0, 0.45}, {Osc::Sine, 3.0, 0.18}},
       0.004, 0.85, false, 0.06, -0.1},
      {"xylophone", {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 3.93, 0.35}}, 0.002, 0.55, false, 0.04, 0.2},
      {"iron_xylophone",
       {{Osc::Sine, 1.0, 1.0}, {Osc::Sine, 4.02, 0.18}, {Osc::Sine, 1.0, 0.25}},
       0.002, 1.1, false, 0.06, -0.2},
      {"cow_bell", {{Osc::Square, 1.0, 0.7}, {Osc::Square, 1.49, 0.55}}, 0.002, 0.45, false, 0.03, 0.1},
      {"didgeridoo",
       {{Osc::Saw, 1.0, 0.8}, {Osc::Sine, 1.0, 0.6}, {Osc::Sine, 2.0, 0.3}, {Osc::Sine, 3.0, 0.14}},
       0.03, 0.4, true, 0.15, -0.05},
      {"bit", {{Osc::Square, 1.0, 0.85}}, 0.002, 0.5, false, 0.03, 0.25},
      {"banjo", {{Osc::Saw, 1.0, 0.6}, {Osc::Sine, 2.0, 0.4}}, 0.003, 0.55, false, 0.04, 0.4},
      {"pling", {{Osc::Sine, 1.0, 1.0}, {Osc::Triangle, 2.0, 0.4}}, 0.003, 0.8, false, 0.05, 0.0},
  };
}

const VoiceParams& paramsFor(model::Instrument ins) {
  static const std::vector<VoiceParams> kParams = makeParams();
  return kParams[static_cast<size_t>(ins)];
}

double midiFreq(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

double oscSample(Osc osc, double phase, double freq, double t, std::mt19937& rng,
                 std::uniform_real_distribution<double>& noiseDist) {
  switch (osc) {
    case Osc::Sine: return std::sin(phase);
    case Osc::Square: return std::sin(phase) >= 0 ? 1.0 : -1.0;
    case Osc::Saw: {
      double x = std::fmod(phase, 2.0 * kPi) / (2.0 * kPi);
      return 2.0 * x - 1.0;
    }
    case Osc::Triangle: {
      double x = std::fmod(phase, 2.0 * kPi) / (2.0 * kPi);
      return 4.0 * std::fabs(x - 0.5) - 1.0;
    }
    case Osc::Noise: return noiseDist(rng);
    case Osc::Sweep: {
      // 底鼓: 频率从 3f 快速滑到 f
      double k = std::min(1.0, t * 12.0);
      return std::sin(phase * (1.0 + 2.0 * (1.0 - k * k)));
    }
  }
  return 0;
}

}  // namespace

std::vector<float> renderSong(const model::Song& song, const SynthOptions& options) {
  const double sr = options.sampleRate;
  const double lengthSec = song.durationSec + options.tailSec;
  const size_t totalFrames = static_cast<size_t>(lengthSec * sr) + 1;
  std::vector<double> left(totalFrames, 0.0), right(totalFrames, 0.0);

  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> noiseDist(-1.0, 1.0);

  for (const auto& lane : song.lanes) {
    const VoiceParams& vp = paramsFor(lane.instrument);
    // 声像
    double l = std::cos((vp.pan + 1.0) * kPi / 4.0);
    double r = std::sin((vp.pan + 1.0) * kPi / 4.0);

    for (const auto& note : lane.notes) {
      double freq = midiFreq(note.midiPitch);
      double start = note.step * song.gridTicks / 20.0;
      double amp = 0.16 * (0.3 + 0.7 * note.velocity / 127.0);

      double sustainSec = note.durationSec;
      double voiceSec = vp.sustained ? std::max(0.25, sustainSec) + vp.releaseSec : vp.decaySec;
      size_t startFrame = static_cast<size_t>(start * sr);
      size_t frames = static_cast<size_t>(voiceSec * sr);
      double totalGain = 0;
      for (const auto& p : vp.partials) totalGain += p.gain;
      if (totalGain <= 0) continue;

      for (size_t f = 0; f < frames; ++f) {
        size_t idx = startFrame + f;
        if (idx >= totalFrames) break;
        double t = static_cast<double>(f) / sr;

        // 包络
        double env;
        if (vp.sustained) {
          double rel = std::max(0.0, t - sustainSec);
          double attack = std::min(1.0, t / vp.attackSec);
          double release = rel >= vp.releaseSec ? 0.0 : 1.0 - rel / vp.releaseSec;
          env = attack * release;
        } else {
          env = std::exp(-t * 6.907755 / vp.decaySec);  // decaySec 内衰减到 1e-3
          if (t < vp.attackSec) env *= t / vp.attackSec;
        }
        if (env <= 1e-4) continue;

        double sample = 0;
        for (const auto& p : vp.partials)
          sample += p.gain * oscSample(p.osc, 2.0 * kPi * freq * p.ratio * t,
                                       freq * p.ratio, t, rng, noiseDist);
        sample = sample / totalGain * amp * env;
        left[idx] += sample * l;
        right[idx] += sample * r;
      }
    }
  }

  // 归一化 + 软限幅
  double peak = 1e-9;
  for (size_t i = 0; i < totalFrames; ++i)
    peak = std::max(peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
  double norm = options.maxPeak / peak;

  std::vector<float> out(totalFrames * 2);
  for (size_t i = 0; i < totalFrames; ++i) {
    double a = left[i] * norm, b = right[i] * norm;
    // tanh 软限幅, 超过 0.7 才开始压
    a = a > 0.7 ? 0.7 + std::tanh((a - 0.7) / 0.3) * 0.3 : a;
    b = b > 0.7 ? 0.7 + std::tanh((b - 0.7) / 0.3) * 0.3 : b;
    out[2 * i] = static_cast<float>(a);
    out[2 * i + 1] = static_cast<float>(b);
  }
  return out;
}

}  // namespace m2n::audio

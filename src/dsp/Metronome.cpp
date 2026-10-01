#include "Metronome.h"

#include <algorithm>
#include <cmath>

static constexpr double kTwoPi = 6.283185307179586;

Metronome::Metronome()
    : mAccents(defaultAccents(4, 4)) {
    buildSounds(mSr);
}

void Metronome::setSampleRate(double sr) {
    if (sr <= 0.0 || sr == mSr) {
        return;
    }
    // Called with processing suspended (onBufferSizeChanged); drop voices
    // that point into the buffers about to be replaced.
    for (auto& v : mVoices) {
        v = {};
    }
    mSr = sr;
    buildSounds(sr);
}

void Metronome::setMeter(int beats, int unit) {
    mBeats.store(std::clamp(beats, 1, kMaxBeats));
    mUnit.store(unit == 2 || unit == 8 || unit == 16 ? unit : 4);
}

uint32_t Metronome::defaultAccents(int beats, int unit) {
    beats = std::clamp(beats, 1, kMaxBeats);
    const bool compound = unit >= 8 && beats > 3 && beats % 3 == 0;
    uint32_t p = 0;
    for (int b = 0; b < beats; ++b) {
        Accent a = b == 0 ? Strong : compound ? (b % 3 == 0 ? Normal : Soft) : Normal;
        p = withAccent(p, b, a);
    }
    return p;
}

// ---------------------------------------------------------------------------
// Sound synthesis (non-RT)
// ---------------------------------------------------------------------------

namespace
{

// Peak-normalize to 0.9 so every set sits at the same level.
void normalize(std::vector<float>& s) {
    float peak = 0.0f;
    for (float v : s) {
        peak = std::max(peak, std::abs(v));
    }
    if (peak > 0.0f) {
        for (float& v : s) {
            v *= 0.9f / peak;
        }
    }
}

std::vector<float> sineBlip(double sr, double freq, double lenS, double tau, double attackS) {
    std::vector<float> s(static_cast<size_t>(lenS * sr));
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = i / sr;
        const double env = std::min(1.0, t / attackS) * std::exp(-t / tau);
        s[i] = static_cast<float>(std::sin(kTwoPi * freq * t) * env);
    }
    normalize(s);
    return s;
}

// Noise burst into a resonant band-pass (RBJ): a woodblock-ish knock.
std::vector<float> woodKnock(double sr, double freq, double lenS) {
    std::vector<float> s(static_cast<size_t>(lenS * sr));
    const double w0 = kTwoPi * freq / sr, q = 9.0, alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double b0 = alpha / a0, b2 = -alpha / a0;
    const double a1 = -2.0 * std::cos(w0) / a0, a2 = (1.0 - alpha) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    uint32_t seed = 0x1234567u;
    for (size_t i = 0; i < s.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        const double noise =
            (static_cast<double>(seed >> 8) / 8388608.0 - 1.0) * std::exp(-(i / sr) / 0.0015);
        const double y = b0 * noise + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = noise;
        y2 = y1;
        y1 = y;
        s[i] = static_cast<float>(y);
    }
    normalize(s);
    return s;
}

// Two detuned square waves through a soft band-limit — the TR-808 recipe.
std::vector<float> cowbell(double sr, double f1, double f2, double lenS) {
    std::vector<float> s(static_cast<size_t>(lenS * sr));
    double lp = 0.0;
    const double k = 1.0 - std::exp(-kTwoPi * 3500.0 / sr);
    for (size_t i = 0; i < s.size(); ++i) {
        const double t = i / sr;
        const double sq = (std::sin(kTwoPi * f1 * t) >= 0 ? 1.0 : -1.0) +
                          (std::sin(kTwoPi * f2 * t) >= 0 ? 1.0 : -1.0);
        const double env =
            std::min(1.0, t / 0.001) * (0.7 * std::exp(-t / 0.012) + 0.3 * std::exp(-t / 0.09));
        lp += k * (sq * env - lp);
        s[i] = static_cast<float>(lp);
    }
    normalize(s);
    return s;
}

} // namespace

void Metronome::buildSounds(double sr) {
    mSamples[Click] = {sineBlip(sr, 2000.0, 0.030, 0.006, 0.0005),
                       sineBlip(sr, 1500.0, 0.030, 0.006, 0.0005),
                       sineBlip(sr, 1000.0, 0.020, 0.004, 0.0005)};
    mSamples[Wood] = {woodKnock(sr, 1700.0, 0.060), woodKnock(sr, 1150.0, 0.060),
                      woodKnock(sr, 850.0, 0.045)};
    mSamples[Beep] = {sineBlip(sr, 1760.0, 0.070, 0.050, 0.002),
                      sineBlip(sr, 880.0, 0.070, 0.050, 0.002),
                      sineBlip(sr, 660.0, 0.045, 0.030, 0.002)};
    mSamples[Cowbell] = {cowbell(sr, 587.0, 845.0, 0.200), cowbell(sr, 540.0, 800.0, 0.200),
                         cowbell(sr, 540.0, 800.0, 0.090)};
}

// ---------------------------------------------------------------------------
// Audio thread
// ---------------------------------------------------------------------------

void Metronome::trigger(const std::vector<float>& sample, float gain) {
    // Free voice, else steal the older one.
    Voice* v = &mVoices[0];
    if (mVoices[0].data && (!mVoices[1].data || mVoices[1].pos > mVoices[0].pos)) {
        v = &mVoices[1];
    }
    v->data = sample.data();
    v->len = static_cast<int>(sample.size());
    v->pos = 0;
    v->gain = gain;
}

void Metronome::render(float* L, float* R, int from, int to, float gL, float gR) {
    for (auto& v : mVoices) {
        if (!v.data) {
            continue;
        }
        for (int i = from; i < to; ++i) {
            const float s = v.data[v.pos] * v.gain;
            L[i] += s * gL;
            R[i] += s * gR;
            if (++v.pos >= v.len) {
                v.data = nullptr;
                break;
            }
        }
    }
}

void Metronome::process(float* L, float* R, int n) {
    const bool run = mRunning.load(std::memory_order_relaxed);
    if (run && !mWasRunning) {
        mUntilNext = 0.0; // first click on the first sample
        mTick = 0;
        mBeat = 0;
        mCountInLeft = mCountInBars.load(std::memory_order_relaxed);
    } else if (!run && mWasRunning) {
        mBeatNow.store(-1, std::memory_order_relaxed);
        mCountInNow.store(false, std::memory_order_relaxed);
    }
    mWasRunning = run;
    if (!run && !mVoices[0].data && !mVoices[1].data) {
        return;
    }

    const float vol = std::pow(10.0f, mVolumeDb.load(std::memory_order_relaxed) / 20.0f);
    const float pan = std::clamp(mPan.load(std::memory_order_relaxed), -1.0f, 1.0f);
    const float gL = vol * std::cos((pan + 1.0f) * 0.25f * 3.14159265f); // constant power
    const float gR = vol * std::sin((pan + 1.0f) * 0.25f * 3.14159265f);

    if (!run) {
        render(L, R, 0, n, gL, gR); // let the last click ring out
        return;
    }

    const int beats = mBeats.load(std::memory_order_relaxed);
    const int subdiv = mSubdiv.load(std::memory_order_relaxed);
    const double bpm = std::clamp(mBpm.load(std::memory_order_relaxed), 20.0, 400.0);
    const double tick = mSr * 60.0 / bpm * (4.0 / mUnit.load(std::memory_order_relaxed)) / subdiv;
    const uint32_t acc = mAccents.load(std::memory_order_relaxed);
    const auto& set = mSamples[mSound.load(std::memory_order_relaxed)];
    const bool silentAfter = mSilentAfter.load(std::memory_order_relaxed) &&
                             mCountInBars.load(std::memory_order_relaxed) > 0;

    int i = 0;
    double until = mUntilNext;
    while (until < n) {
        const int at = std::max(i, static_cast<int>(until));
        render(L, R, i, at, gL, gR);
        i = at;

        if (mBeat >= beats) {
            mBeat = 0; // meter shortened while running
        }
        const bool counting = mCountInLeft > 0;
        const bool audible = counting || !silentAfter;
        const Accent a = counting ? (mBeat == 0 ? Strong : Normal) : accentOf(acc, mBeat);
        if (mTick == 0) {
            mBeatNow.store(mBeat, std::memory_order_relaxed);
            mCountInNow.store(counting, std::memory_order_relaxed);
            if (audible && a != Mute) {
                trigger(set[a == Strong ? 0 : 1], a == Soft ? 0.45f : 1.0f);
            }
        } else if (audible && a != Mute) {
            trigger(set[2], 0.55f);
        }

        if (++mTick >= subdiv) {
            mTick = 0;
            if (++mBeat >= beats) {
                mBeat = 0;
                if (mCountInLeft > 0) {
                    --mCountInLeft;
                }
            }
        }
        until += tick;
    }
    render(L, R, i, n, gL, gR);
    mUntilNext = until - n;
}

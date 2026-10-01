#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/// Delay-line pitch shifter (mono, one per channel): two read taps sweep a
/// window through a short delay line at a rate set by the pitch ratio, each
/// faded by a Hann window half a period apart (the gains always sum to 1).
///
/// WSOLA alignment: when a tap restarts its sweep (silent at that moment),
/// its new start is moved by up to ±12 ms to the position whose recent
/// waveform best matches what the other tap is playing, so the crossfade
/// joins two in-phase signals instead of beating (plain two-tap shifters
/// cancel whole frequencies). Latency ≈ half the window (25 ms at 50 ms).
/// Allocates only in prepare() (non-RT).
class PitchShifter
{
public:
    void prepare(double sr, float windowMs = 50.0f)
    {
        mWindow = std::max(64.0f, static_cast<float>(sr) * windowMs / 1000.0f);
        mSearch = std::max(8, static_cast<int>(sr * 0.012));
        mCorrLen = std::max(32, static_cast<int>(sr * 0.005));
        mBuf.assign(static_cast<size_t>(mWindow) * 2 + mSearch * 2 + mCorrLen + 8, 0.0f);
        for (size_t i = 0; i < kTable; ++i)
        {
            const double s = std::sin(3.14159265358979323846 * static_cast<double>(i) / kTable);
            mHann[i] = static_cast<float>(s * s); // sin²: tap A + tap B (½ period apart) = 1
        }
        reset();
    }

    void reset()
    {
        std::fill(mBuf.begin(), mBuf.end(), 0.0f);
        mWrite = 0;
        mPhase = 0.0f;
        mOff[0] = mOff[1] = 0.0f;
        mPrev[0] = 0.0f;
        mPrev[1] = 0.5f;
    }

    /// Semitones (fractional allowed); 0 = passthrough delay.
    void setSemitones(float st) { mRatio = std::pow(2.0f, st / 12.0f); }

    float process(float x)
    {
        if (mBuf.empty()) return x;
        const int sz = static_cast<int>(mBuf.size());
        mBuf[mWrite] = x;

        // The delay changes at (1 − ratio) samples per sample: shrinking
        // delay = reading faster = pitch up.
        mPhase += (1.0f - mRatio) / mWindow;
        mPhase -= std::floor(mPhase);
        const float pB = mPhase + 0.5f >= 1.0f ? mPhase - 0.5f : mPhase + 0.5f;

        // A wrap = the tap just jumped to the other end of its sweep (at
        // zero gain): re-align it against the other tap.
        const float p[2] = {mPhase, pB};
        for (int k = 0; k < 2; ++k)
        {
            if (std::abs(p[k] - mPrev[k]) > 0.5f)
                mOff[k] = align(p[k], p[1 - k], mOff[1 - k], sz);
            mPrev[k] = p[k];
        }

        const float out = tap(p[0], mOff[0], sz) * hann(p[0]) + tap(p[1], mOff[1], sz) * hann(p[1]);
        if (++mWrite == sz) mWrite = 0;
        return out;
    }

private:
    static constexpr size_t kTable = 1024;

    float hann(float p) const { return mHann[static_cast<size_t>(p * kTable) & (kTable - 1)]; }

    float delayOf(float p, float off) const { return 1.0f + p * mWindow + off; }

    // Best offset in [−search, +search] (kept ≥ 0 delay) for a restarting
    // tap: maximise the normalised correlation between the last mCorrLen
    // samples behind its new read point and behind the other tap's.
    float align(float p, float pOther, float offOther, int sz) const
    {
        const float dOther = delayOf(pOther, offOther);
        const float dBase  = delayOf(p, 0.0f);
        auto at = [&](float d, int j) {
            int r = mWrite - static_cast<int>(d) - j;
            while (r < 0) r += sz;
            return mBuf[r % sz];
        };
        auto score = [&](int off) {
            const float d = dBase + off;
            if (d < 1.0f || d + mCorrLen + 2 >= sz) return -1e30f;
            float xy = 0.0f, yy = 1e-9f;
            for (int j = 0; j < mCorrLen; j += 2) // every other sample: plenty for alignment
            {
                const float a = at(d, j), b = at(dOther, j);
                xy += a * b;
                yy += a * a;
            }
            return xy / std::sqrt(yy);
        };
        int   best = 0;
        float bestS = score(0);
        for (int off = -mSearch; off <= mSearch; off += 4) // coarse
        {
            const float sc = score(off);
            if (sc > bestS) { bestS = sc; best = off; }
        }
        const int c = best;
        for (int off = c - 3; off <= c + 3; ++off) // refine
        {
            const float sc = score(off);
            if (sc > bestS) { bestS = sc; best = off; }
        }
        return static_cast<float>(best);
    }

    float tap(float p, float off, int sz) const
    {
        float r = static_cast<float>(mWrite) - delayOf(p, off);
        if (r < 0.0f) r += sz;
        int r0 = static_cast<int>(r);
        if (r0 >= sz) r0 -= sz;
        const int   r1 = r0 + 1 == sz ? 0 : r0 + 1;
        const float f  = r - std::floor(r);
        return mBuf[r0] + (mBuf[r1] - mBuf[r0]) * f;
    }

    std::vector<float> mBuf;
    std::array<float, kTable> mHann{};
    int   mWrite  = 0;
    float mPhase  = 0.0f;
    float mWindow = 2400.0f;
    float mRatio  = 1.0f;
    int   mSearch = 576;
    int   mCorrLen = 240;
    float mOff[2]{};             // per-tap alignment offset (samples)
    float mPrev[2]{0.0f, 0.5f};  // last phase per tap (wrap detection)
};

#pragma once

#include <algorithm>
#include <atomic>

/// Abstract base for all audio effect processors.
///
/// Provides: enable/bypass, stereo-mode flag, sample rate, and a unified
/// process() dispatch that encodes all four routing cases:
///
///   enabled  stereoMode+supportsStereoOut  inputIsStereo  action
///   -------  ----------------------------  -------------  ------
///   false    -                             any            pass-through (no-op)
///   true     false                         false          processBlockMono(L), copy L→R
///   true     false                         true           processBlockMono(L), processBlockMono(R)
///   true     true                          false          processBlockStereoOut(L, R)  ← true stereo
///   true     true                          true           processBlockMono(L), processBlockMono(R) (fallback)
///
/// Returns true if the output buffers now contain different content (stereo).
class EffectBase
{
public:
    virtual ~EffectBase() = default;

    // Non-virtual: stores SR then calls onSampleRateChanged() hook.
    void setSampleRate(double sr) { mSampleRate = sr; onSampleRateChanged(sr); }

    void setEnabled(bool e)    { mEnabled.store(e);    }
    void setStereoMode(bool s) { mStereoMode.store(s); }

    bool isEnabled()    const { return mEnabled.load();    }
    bool isStereoMode() const { return mStereoMode.load(); }

    /// Returns true if this effect can produce L≠R output from mono input.
    virtual bool supportsStereoOut() const { return false; }

    virtual void reset() = 0;

    /// Unified dispatch entry point. L and R are always valid float buffers of length n.
    /// inputIsStereo: caller signals whether L and R already contain different content.
    /// Returns: true if L and R now contain different content.
    virtual bool process(float* L, float* R, int n, bool inputIsStereo)
    {
        if (!mEnabled.load())
            return inputIsStereo;

        const bool wantStereoOut = mStereoMode.load() && supportsStereoOut();

        if (wantStereoOut && !inputIsStereo) {
            // Mono in + stereo mode → true stereo output
            processBlockStereoOut(L, R, n);
            return true;
        }

        if (inputIsStereo) {
            // Stereo in → process each channel independently (preserves existing stereo)
            processBlockMono(L, n);
            processBlockMono(R, n);
            return true;
        }

        // Mono in + mono mode → process once, copy to R
        processBlockMono(L, n);
        std::copy(L, L + n, R);
        return false;
    }

protected:
    /// Hook called whenever setSampleRate() is invoked (instead of overriding setSampleRate).
    virtual void onSampleRateChanged(double /*sr*/) {}

    /// Mono in-place processing. Must be stateless with respect to channel identity:
    /// it may be called twice in a row for L then R on separate channel states.
    virtual void processBlockMono(float* /*buf*/, int /*n*/) {}

    /// True stereo output from mono input.
    /// Called only when supportsStereoOut() == true && !inputIsStereo.
    /// Default falls back to calling processBlockMono on each channel independently.
    virtual void processBlockStereoOut(float* L, float* R, int n)
    {
        processBlockMono(L, n);
        processBlockMono(R, n);
    }

    double            mSampleRate{44100.0};
    std::atomic<bool> mEnabled   {true};
    std::atomic<bool> mStereoMode{false};
};

#pragma once

/// Abstract base for all audio effect processors.
/// All effects support both mono and stereo block processing.
/// Default stereo implementation calls processBlock on each channel independently.
class EffectBase
{
public:
    virtual ~EffectBase() = default;

    virtual void setSampleRate(double sr) = 0;
    virtual void reset() = 0;

    /// Mono in-place processing
    virtual void processBlock(float* buf, int nframes) { (void)buf; (void)nframes; }

    /// Stereo in-place processing (L and R are separate arrays of length nframes)
    virtual void processBlockStereo(float* L, float* R, int nframes)
    {
        processBlock(L, nframes);
        processBlock(R, nframes);
    }
};

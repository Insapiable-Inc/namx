#include "VolumeProcessor.h"

#include <algorithm>
#include <cmath>

float VolumeProcessor::targetGain() const {
    const float p = std::clamp(mPosition.load(), 0.0f, 100.0f) / 100.0f;
    if (p <= 0.0f) {
        return 0.0f;
    }
    const float mid = std::clamp(mMidDb.load(), -40.0f, 0.0f);
    // dB linear in travel on each half: −60 dB → Mid → 0 dB.
    const float dB =
        p < 0.5f ? -60.0f + (mid + 60.0f) * (p / 0.5f) : mid + (0.0f - mid) * ((p - 0.5f) / 0.5f);
    return p < 0.02f
               ? std::pow(10.0f, dB / 20.0f) * (p / 0.02f) // fade the last bit to true silence
               : std::pow(10.0f, dB / 20.0f);
}

bool VolumeProcessor::process(float* L, float* R, int n, bool inputIsStereo) {
    if (!mEnabled.load()) {
        return inputIsStereo;
    }
    const float target = targetGain();
    const float k = 1.0f - std::exp(-1.0f / (0.010f * static_cast<float>(mSampleRate)));
    for (int i = 0; i < n; ++i) {
        mGain += k * (target - mGain);
        L[i] *= mGain;
        if (inputIsStereo) {
            R[i] *= mGain;
        }
    }
    if (!inputIsStereo) {
        std::copy(L, L + n, R);
    }
    return inputIsStereo;
}

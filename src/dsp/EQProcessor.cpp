#include "EQProcessor.h"

#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

EQProcessor::EQProcessor() {
    for (int b = 0; b < kBands; ++b) {
        rebuild(Band(b));
    }
}

void EQProcessor::onSampleRateChanged(double) {
    // Same lock discipline as the setters: the audio thread try_locks this
    // mutex, so coefficient/state rewrites must never happen bare.
    std::lock_guard<std::mutex> lock(mMutex);
    for (int b = 0; b < kBands; ++b) {
        rebuild(Band(b));
    }
    for (int b = 0; b < kBands; ++b) {
        mL[b].reset();
        mR[b].reset();
    }
}

void EQProcessor::setGain(Band b, double dB) {
    std::lock_guard<std::mutex> lock(mMutex);
    mGain[b] = dB;
    rebuild(b);
}

void EQProcessor::setFreq(Band b, double hz) {
    std::lock_guard<std::mutex> lock(mMutex);
    mFreq[b] = hz;
    rebuild(b);
}

bool EQProcessor::active(Band b) const {
    switch (b) {
        case LowCut:
            return mFreq[b] > 20.5;
        case HighCut:
            return mFreq[b] < 19999.0 && mFreq[b] < 0.45 * mSampleRate;
        default:
            return std::abs(mGain[b]) > 0.01;
    }
}

void EQProcessor::rebuild(Band b) {
    const Biquad c = design(b);
    mL[b].setCoeffs(c);
    mR[b].setCoeffs(c);
    const bool on = active(b);
    if (on && !mOn[b]) {
        mL[b].reset();
        mR[b].reset();
    } // start a band from silence
    mOn[b] = on;
}

EQProcessor::Biquad EQProcessor::design(Band band) const {
    Biquad bq;
    const double fs = mSampleRate;
    const double f = std::clamp(mFreq[band], 10.0, 0.45 * fs);
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos(w0), sw = std::sin(w0);
    double b0, b1, b2, a0, a1, a2;

    switch (band) {
        case LowCut:
        case HighCut: {
            const double alpha = sw / (2.0 * 0.7071); // Butterworth Q
            if (band == LowCut) {
                b0 = (1 + cw) / 2;
                b1 = -(1 + cw);
                b2 = (1 + cw) / 2;
            } else {
                b0 = (1 - cw) / 2;
                b1 = (1 - cw);
                b2 = (1 - cw) / 2;
            }
            a0 = 1 + alpha;
            a1 = -2 * cw;
            a2 = 1 - alpha;
            break;
        }
        case Middle:
        case HighMid: {
            const double A = std::pow(10.0, mGain[band] / 40.0);
            const double alpha = sw / (2.0 * (band == Middle ? 0.707 : 1.0));
            b0 = 1 + alpha * A;
            b1 = -2 * cw;
            b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;
            a1 = -2 * cw;
            a2 = 1 - alpha / A;
            break;
        }
        case Bass:
        case Treble:
        default: {
            const double A = std::pow(10.0, mGain[band] / 40.0);
            const double alpha = sw / 2.0 * std::sqrt(2.0); // shelf slope S = 1
            const double sA = 2.0 * std::sqrt(A) * alpha;
            if (band == Bass) {
                b0 = A * ((A + 1) - (A - 1) * cw + sA);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sA);
                a0 = (A + 1) + (A - 1) * cw + sA;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sA;
            } else {
                b0 = A * ((A + 1) + (A - 1) * cw + sA);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sA);
                a0 = (A + 1) - (A - 1) * cw + sA;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sA;
            }
            break;
        }
    }
    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    return bq;
}

bool EQProcessor::process(float* L, float* R, size_t n, bool inputIsStereo) {
    if (!mEnabled.load()) {
        return inputIsStereo;
    }

    // Skip this block if the GUI thread is updating coefficients (inaudible at normal buffer sizes)
    if (!mMutex.try_lock()) {
        return inputIsStereo;
    }

    for (int b = 0; b < kBands; ++b) {
        if (!mOn[b]) {
            continue;
        }
        Biquad& l = mL[b];
        for (size_t i = 0; i < n; ++i) {
            L[i] = l.process(L[i]);
        }
        if (inputIsStereo) {
            Biquad& r = mR[b];
            for (size_t i = 0; i < n; ++i) {
                R[i] = r.process(R[i]);
            }
        }
    }
    if (!inputIsStereo) {
        std::copy(L, L + n, R);
    }

    mMutex.unlock();
    return inputIsStereo;
}

void EQProcessor::reset() {
    for (int b = 0; b < kBands; ++b) {
        mL[b].reset();
        mR[b].reset();
    }
}

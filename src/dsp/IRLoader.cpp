#include "IRLoader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Eigen/Dense>

#include "Common.h"
#include "Log.h"
#include "WavReader.h"

namespace namx
{

static void clear_kernel_buffers(vector<float> buffers[2], int write_pos[2]) {
    for (int ch = 0; ch < 2; ++ch) {
        std::fill(buffers[ch].begin(), buffers[ch].end(), 0.0f);
        write_pos[ch] = 0;
    }
}

// Windowed-sinc resampler. Produces at most maxOut output samples at fsOut
// from `in` at fsIn, band-limited to the lower of the two Nyquist rates.
static vector<float> resample_sinc(const vector<float>& in, double fs_in, double fs_out,
                                   size_t max_out) {
    if (in.empty()) {
        return {};
    }
    const double step = fs_in / fs_out; // input samples per output sample
    const double cutoff = 0.5 * std::min(1.0, 1.0 / step); // cycles per input sample
    const double half = std::ceil(32.0 * std::max(1.0, step)); // kernel half-width, samples
    // Positions are fractional (t), sample indices whole: the casts below convert between them.
    const size_t out_len =
        std::min(max_out, static_cast<size_t>(std::ceil(static_cast<double>(in.size()) / step)));

    vector<float> out(out_len);
    for (size_t i = 0; i < out_len; ++i) {
        const double t = static_cast<double>(i) * step;
        const size_t k_min = static_cast<size_t>(std::max(0.0, std::ceil(t - half)));
        const size_t k_max = std::min(in.size() - 1, static_cast<size_t>(std::floor(t + half)));
        double acc = 0.0;
        for (size_t k = k_min; k <= k_max; ++k) {
            const double u = t - static_cast<double>(k);
            const double x = 2.0 * cutoff * u;
            const double sinc = (std::abs(x) < 1e-9) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            // Blackman window over [-half, half]
            const double w =
                0.42 + 0.5 * std::cos(M_PI * u / half) + 0.08 * std::cos(2.0 * M_PI * u / half);
            acc += in[k] * 2.0 * cutoff * sinc * w;
        }
        out[i] = static_cast<float>(acc);
    }
    return out;
}

IRLoader::IRLoader() {
}

IRLoader::~IRLoader() {
    // Audio thread must be stopped before this is called.
    slot.publish(nullptr);
    slot.reclaim(true);
}

unique_ptr<IRLoader::Kernel> IRLoader::build_kernel() const {
    auto k = make_unique<Kernel>();

    vector<float> samples;
    const bool resample = target_rate > 0.0 && raw_rate > 0.0 && target_rate != raw_rate;
    if (resample) {
        samples = resample_sinc(raw, raw_rate, target_rate, MAX_IR_LENGTH);
    } else {
        samples = raw;
        samples.resize(std::min(samples.size(), MAX_IR_LENGTH));
    }

    k->ir_len = static_cast<int>(samples.size());
    k->built_rate = resample ? target_rate : raw_rate;
    k->ir = std::move(samples);

    // Normalize energy (L2) to 1.0 so the convolution is roughly unity-gain;
    // peak-sample normalization would leave several dB of broadband boost.
    float energy = 0.0f;
    for (float s : k->ir) {
        energy += s * s;
    }
    if (energy > 1e-12f) {
        const float scale = 1.0f / std::sqrt(energy);
        for (float& s : k->ir) {
            s *= scale;
        }
    }

    // Store reversed: the delay line is read oldest→newest, so a reversed IR
    // turns the convolution into a single forward dot product.
    std::reverse(k->ir.begin(), k->ir.end());

    for (int ch = 0; ch < 2; ++ch) {
        k->buffers[ch].assign(2 * MAX_IR_LENGTH, 0.0f);
    }
    return k;
}

bool IRLoader::load(const string& path) {
    expected<wav::MonoAudio, string> audio = wav::read_mono(path);
    std::lock_guard<std::mutex> lock(load_mutex);
    if (!audio) {
        last_error = audio.error();
        return false;
    }
    last_error.clear();

    const double sample_rate = audio->sample_rate;
    raw = std::move(audio->samples);
    raw_rate = sample_rate;
    if (target_rate > 0.0 && sample_rate > 0.0 && sample_rate != target_rate) {
        qCInfo(lc_audio, "[IRLoader] Resampling IR from %.0f Hz to %.0f Hz", sample_rate,
               target_rate);
    }

    slot.publish(build_kernel());
    file_path = path;
    return true;
}

void IRLoader::unload() {
    std::lock_guard<std::mutex> lock(load_mutex);
    slot.publish(nullptr);
    file_path = "";
    raw.clear();
    raw_rate = 0.0;
}

void IRLoader::set_target_sample_rate(double sr) {
    std::lock_guard<std::mutex> lock(load_mutex);
    target_rate = sr;
    Kernel* k = slot.current();
    if (!k || raw.empty() || sr <= 0.0 || k->built_rate == sr) {
        return;
    }
    qCInfo(lc_audio, "[IRLoader] Engine rate changed to %.0f Hz — rebuilding IR kernel", sr);
    slot.publish(build_kernel());
}

/*static*/ void IRLoader::push_sample(Kernel& k, int ch, float in) {
    float* buf = k.buffers[ch].data();
    const int wp = k.write_pos[ch];
    buf[wp] = in;
    buf[wp + k.ir_len] = in; // mirror write keeps the window below contiguous
}

/*static*/ float IRLoader::convolve_at(const Kernel& k, int ch) {
    const float* buf = k.buffers[ch].data();
    const int wp = k.write_pos[ch];

    // buf[wp+1 .. wp+irLen] is the last irLen samples, oldest→newest;
    // k.ir is reversed, so this dot product is the convolution sum.
    Eigen::Map<const Eigen::VectorXf> ir(k.ir.data(), k.ir_len);
    Eigen::Map<const Eigen::VectorXf> sig(buf + wp + 1, k.ir_len);
    return ir.dot(sig);
}

/*static*/ void IRLoader::advance(Kernel& k, int ch) {
    if (++k.write_pos[ch] >= k.ir_len) {
        k.write_pos[ch] = 0;
    }
}

void IRLoader::process_block(float* l, float* r, size_t num_frames, bool input_is_stereo,
                             bool enabled) {
    struct BlockGuard
    {
        RtSlot<Kernel>& s;
        ~BlockGuard() {
            s.end_block();
        }
    } guard{slot};

    // One snapshot per block; the kernel stays alive until endBlock() runs.
    Kernel* kp = slot.acquire();
    if (!enabled || !kp || kp->ir_len <= 0) {
        return;
    }
    Kernel& k = *kp;

    if (input_is_stereo) {
        for (size_t i = 0; i < num_frames; ++i) {
            push_sample(k, 0, l[i]);
            push_sample(k, 1, r[i]);
            l[i] = convolve_at(k, 0);
            r[i] = convolve_at(k, 1);
            advance(k, 0);
            advance(k, 1);
        }
        return;
    }

    // Mono: L == R, so the two delay lines hold identical history. Feed both
    // (a few stores) but run the 4096-tap dot product only once — the R line
    // stays valid for the moment an upstream effect goes stereo.
    for (size_t i = 0; i < num_frames; ++i) {
        const float in = l[i];
        push_sample(k, 0, in);
        push_sample(k, 1, in);
        const float out = convolve_at(k, 0);
        l[i] = out;
        r[i] = out;
        advance(k, 0);
        advance(k, 1);
    }
}

void IRLoader::reset() {
    // Audio thread (chain reorder): clear the delay lines of whatever kernel
    // is live. std::fill only — no allocation.
    if (Kernel* k = slot.acquire()) {
        clear_kernel_buffers(k->buffers, k->write_pos);
    }
}

} // namespace namx

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

    const bool resample = target_rate > 0.0 && raw_rate > 0.0 && target_rate != raw_rate;
    const double rate = resample ? target_rate : raw_rate;
    const size_t cap = static_cast<size_t>(MAX_IR_SECONDS * rate);
    const size_t full_length =
        resample ? static_cast<size_t>(std::ceil(static_cast<double>(raw.size()) * rate / raw_rate))
                 : raw.size();
    vector<float> ir = resample ? resample_sinc(raw, raw_rate, target_rate, cap) : raw;
    ir.resize(std::min(ir.size(), cap));
    k->built_rate = rate;

    // Longer than the cap: fade the end out (raised cosine) rather than cut it.
    if (full_length > cap && !ir.empty()) {
        const size_t fade = std::min(ir.size(), static_cast<size_t>(FADE_MS * 0.001 * rate));
        for (size_t i = 0; i < fade; ++i) {
            const double x = static_cast<double>(i + 1) / static_cast<double>(fade);
            ir[ir.size() - fade + i] *= static_cast<float>(0.5 + 0.5 * std::cos(M_PI * x));
        }
        qCInfo(lc_audio, "[IRLoader] IR is %.2f s long: faded out at %.1f s",
               static_cast<double>(full_length) / rate, MAX_IR_SECONDS);
    }

    // Normalize energy (L2) to 1.0 so the convolution is roughly unity-gain;
    // peak-sample normalization would leave several dB of broadband boost.
    double energy = 0.0; // a float sum drifts by ~1e-5 over a long IR
    for (float s : ir) {
        energy += static_cast<double>(s) * s;
    }
    if (energy > 1e-12) {
        const float scale = static_cast<float>(1.0 / std::sqrt(energy));
        for (float& s : ir) {
            s *= scale;
        }
    }

    // Head: the first HEAD taps, reversed (the delay line is read oldest → newest).
    k->head_ir.assign(HEAD, 0.0f);
    for (size_t i = 0; i < std::min(HEAD, ir.size()); ++i) {
        k->head_ir[HEAD - 1 - i] = ir[i];
    }
    // Tail: the rest in partitions of HEAD taps, each zero-padded to 2 × HEAD and
    // transformed once here.
    const size_t tail = ir.size() > HEAD ? ir.size() - HEAD : 0;
    k->partitions = (tail + HEAD - 1) / HEAD;
    k->tail_spectra.assign(k->partitions * HEAD, Complex{});
    vector<float> padded(2 * HEAD);
    for (size_t p = 0; p < k->partitions; ++p) {
        std::fill(padded.begin(), padded.end(), 0.0f);
        for (size_t i = 0; i < HEAD && HEAD + p * HEAD + i < ir.size(); ++i) {
            padded[i] = ir[HEAD + p * HEAD + i];
        }
        k->fft.fft(padded.data(), &k->tail_spectra[p * HEAD]);
    }

    k->spectrum.assign(HEAD, Complex{});
    k->time.assign(2 * HEAD, 0.0f);
    for (Channel& c : k->channels) {
        c.head_line.assign(2 * HEAD, 0.0f);
        c.frame.assign(2 * HEAD, 0.0f);
        c.fdl.assign(k->partitions * HEAD, Complex{});
        c.tail_out.assign(HEAD, 0.0f);
    }
    return k;
}

bool IRLoader::load(const string& path) {
    expected<wav::MonoAudio, string> audio = wav::read_mono(utf8_path(path));
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

/*static*/ float IRLoader::process_sample(Kernel& k, Channel& c, float in) {
    // Head: the last HEAD samples (oldest → newest) dotted with the reversed taps.
    c.head_line[c.head_pos] = in;
    c.head_line[c.head_pos + HEAD] = in;
    Eigen::Map<const Eigen::VectorXf> taps(k.head_ir.data(), HEAD);
    Eigen::Map<const Eigen::VectorXf> recent(c.head_line.data() + c.head_pos + 1, HEAD);
    float out = taps.dot(recent);
    if (++c.head_pos == HEAD) {
        c.head_pos = 0;
    }
    if (k.partitions == 0) {
        return out;
    }
    // Tail: computed a block ahead (it starts HEAD samples in).
    out += c.tail_out[c.pos];
    c.frame[HEAD + c.pos] = in;
    if (++c.pos == HEAD) {
        compute_tail(k, c);
        c.pos = 0;
    }
    return out;
}

// One block in: its frame's spectrum joins the delay line, every partition's product is
// summed, and the inverse transform's second half (overlap-save) is the tail's output
// for the next HEAD samples.
/*static*/ void IRLoader::compute_tail(Kernel& k, Channel& c) {
    const size_t parts = k.partitions;
    c.fdl_newest = c.fdl_newest + 1 == parts ? 0 : c.fdl_newest + 1;
    k.fft.fft(c.frame.data(), &c.fdl[c.fdl_newest * HEAD]);
    std::copy(c.frame.begin() + HEAD, c.frame.end(), c.frame.begin());

    std::fill(k.spectrum.begin(), k.spectrum.end(), Complex{});
    float* const acc = reinterpret_cast<float*>(k.spectrum.data());
    size_t frame_index = c.fdl_newest;
    for (size_t p = 0; p < parts; ++p) {
        const float* const x = reinterpret_cast<const float*>(&c.fdl[frame_index * HEAD]);
        const float* const h = reinterpret_cast<const float*>(&k.tail_spectra[p * HEAD]);
        // Bin 0 packs DC (real) and Nyquist (imaginary): two real products.
        acc[0] += x[0] * h[0];
        acc[1] += x[1] * h[1];
        for (size_t b = 1; b < HEAD; ++b) { // complex multiply-accumulate, spelled out
            const float xr = x[2 * b], xi = x[2 * b + 1], hr = h[2 * b], hi = h[2 * b + 1];
            acc[2 * b] += xr * hr - xi * hi;
            acc[2 * b + 1] += xr * hi + xi * hr;
        }
        frame_index = frame_index == 0 ? parts - 1 : frame_index - 1;
    }
    k.fft.ifft(k.spectrum.data(), k.time.data());
    const float scale = 1.0f / static_cast<float>(2 * HEAD);
    for (size_t i = 0; i < HEAD; ++i) {
        c.tail_out[i] = k.time[HEAD + i] * scale;
    }
}

/*static*/ void IRLoader::clear(Channel& c) {
    for (auto* v : {&c.head_line, &c.frame, &c.tail_out}) {
        std::fill(v->begin(), v->end(), 0.0f);
    }
    std::fill(c.fdl.begin(), c.fdl.end(), Complex{});
    c.head_pos = 0;
    c.fdl_newest = 0;
    c.pos = 0;
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
    if (!enabled || !kp) {
        return;
    }
    Kernel& k = *kp;
    Channel& left = k.channels[0];
    Channel& right = k.channels[1];

    if (input_is_stereo) {
        if (!k.right_in_sync) {
            // Mono until now: L and R had the same history, so R starts as a copy of L
            // (same sizes — copies, no allocation).
            std::copy(left.head_line.begin(), left.head_line.end(), right.head_line.begin());
            std::copy(left.frame.begin(), left.frame.end(), right.frame.begin());
            std::copy(left.fdl.begin(), left.fdl.end(), right.fdl.begin());
            std::copy(left.tail_out.begin(), left.tail_out.end(), right.tail_out.begin());
            right.head_pos = left.head_pos;
            right.fdl_newest = left.fdl_newest;
            right.pos = left.pos;
            k.right_in_sync = true;
        }
        for (size_t i = 0; i < num_frames; ++i) {
            l[i] = process_sample(k, left, l[i]);
            r[i] = process_sample(k, right, r[i]);
        }
        return;
    }

    // Mono: L == R, so only L runs; R catches up by copy when a stereo block arrives.
    k.right_in_sync = false;
    for (size_t i = 0; i < num_frames; ++i) {
        const float out = process_sample(k, left, l[i]);
        l[i] = out;
        r[i] = out;
    }
}

void IRLoader::reset() {
    // Audio thread (chain reorder): clear the state of whatever kernel is live. std::fill
    // only — no allocation.
    if (Kernel* k = slot.acquire()) {
        clear(k->channels[0]);
        clear(k->channels[1]);
        k->right_in_sync = false;
    }
}

} // namespace namx

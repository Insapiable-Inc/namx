#include "NamProcessor.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>

#include <NAM/get_dsp.h>

#include "Common.h"
#include "Log.h"
#include "ModelResampler.h" // only here: it brings <windows.h> on Windows

namespace namx
{

// The official plugin's "Normalized" output level. Our own clamp keeps a model with a
// nonsense loudness value from jumping by more than this either way.
static constexpr double NORMALIZE_TARGET_DB = -18.0;
static constexpr double NORMALIZE_MAX_DB = 24.0;

NamProcessor::NamProcessor() {
}

// Out of line: ModelResampler is complete only in this file.
NamProcessor::ModelPair::ModelPair() = default;
NamProcessor::ModelPair::~ModelPair() = default;

void NamProcessor::ModelPair::reset(double rate, size_t max_frames) {
    const double native = native_rate();
    const bool resample = rate > 0.0 && std::abs(rate - native) > 0.5;
    // Behind the resampler the model sees up to ceil(n × native / rate) frames per call.
    const size_t model_frames =
        resample
            ? static_cast<size_t>(std::ceil(static_cast<double>(max_frames) * native / rate)) + 1
            : max_frames;
    const double model_rate = resample ? native : rate;
    for (nam::DSP* m : {l.get(), r.get()}) {
        // Reset() prewarms too (the core's default); a second prewarm() was just
        // repeated work on the GUI thread (review item 21).
        m->Reset(model_rate, static_cast<int>(model_frames)); // the NAM core counts in int
    }
    if (resample) {
        for (unique_ptr<Resampler>* rs : {&resampler_l, &resampler_r}) {
            *rs = make_unique<Resampler>(native);
            (*rs)->Reset(rate, static_cast<int>(max_frames));
        }
        qCInfo(lc_audio, "[NamProcessor] Model trained at %.0f Hz, engine at %.0f Hz: resampling",
               native, rate);
    } else {
        resampler_l.reset();
        resampler_r.reset();
    }
    reset_buf = max_frames;
    reset_rate = rate;
}

NamProcessor::~NamProcessor() {
    // Audio thread must be stopped before this is called.
    std::lock_guard<std::mutex> lock(model_mutex);
    slot.publish(nullptr);
    slot.reclaim(true);
}

bool NamProcessor::load_model(const string& path) {
    // Load two independent instances from the same file — one per channel.
    // They apply identical weights but maintain separate recurrent states
    // so L and R can diverge (e.g. after a pre-NAM stereo effect).
    auto pair = make_unique<ModelPair>();
    try {
        // Read and parse the file once; build both instances from it (review item 21).
        nam::dspData parsed;
        pair->l = nam::get_dsp(std::filesystem::path(path), parsed);
        pair->r = nam::get_dsp(parsed);
    } catch (const std::exception& e) {
        last_error = e.what();
        qCWarning(lc_audio, "[NamProcessor] Failed to load model: %s", e.what());
        return false;
    }
    if (!pair->l || !pair->r) {
        last_error = "Model loader returned no DSP object.";
        return false;
    }

    // The audio path is strictly mono-in/mono-out per channel instance: the
    // callback hands the core exactly one input and one output pointer. A
    // multichannel model would read/write past those — reject it up front.
    const int in_ch = pair->l->NumInputChannels();
    const int out_ch = pair->l->NumOutputChannels();
    if (in_ch != 1 || out_ch != 1) {
        last_error = "Model has " + std::to_string(in_ch) + " input / " + std::to_string(out_ch) +
                     " output channels; only 1-in/1-out models are supported.";
        qCWarning(lc_audio, "[NamProcessor] %s (%s)", last_error.c_str(), path.c_str());
        return false;
    }
    last_error.clear();

    std::optional<double> normalization_db;
    if (pair->l->HasLoudness()) {
        normalization_db = std::clamp(NORMALIZE_TARGET_DB - pair->l->GetLoudness(),
                                      -NORMALIZE_MAX_DB, NORMALIZE_MAX_DB);
        pair->loudness_gain = static_cast<float>(std::pow(10.0, *normalization_db / 20.0));
    }

    // Prewarm outside the lock (slow), then re-check inside it: the buffer
    // size or sample rate may have moved meanwhile.
    size_t current_max_buf;
    double rate;
    {
        std::lock_guard<std::mutex> lock(model_mutex);
        current_max_buf = max_buf.load();
        rate = model_rate > 0.0 ? model_rate : sample_rate;
    }
    pair->reset(rate, current_max_buf);

    {
        std::lock_guard<std::mutex> lock(model_mutex);
        const size_t cur_buf = max_buf.load();
        const double cur_rate = model_rate > 0.0 ? model_rate : sample_rate;
        if (cur_buf > pair->reset_buf || cur_rate != pair->reset_rate) {
            pair->reset(cur_rate, std::max(cur_buf, pair->reset_buf));
        }
        publish_resampling(pair.get());
        slot.publish(std::move(pair));
        model_path = path;
        model_normalization_db = normalization_db;
    }
    fade_pending.store(true, std::memory_order_release);
    return true;
}

void NamProcessor::unload_model() {
    {
        std::lock_guard<std::mutex> lock(model_mutex);
        slot.publish(nullptr);
        model_path = "";
        model_normalization_db.reset();
        publish_resampling(nullptr);
    }
    fade_pending.store(true, std::memory_order_release);
}

void NamProcessor::prepare_buffer(size_t new_max_buf) {
    // Safe to block and mutate the live pair here: both backends invoke
    // buffer-size changes on a non-RT thread with no process callback in
    // flight, and the scratch buffers are only touched by that callback.
    std::lock_guard<std::mutex> lock(model_mutex);
    max_buf.store(new_max_buf);
    scratch_l.resize(new_max_buf);
    scratch_r.resize(new_max_buf);

    // A model Reset() with a smaller max buffer will overrun the NAM core's
    // preallocated storage on a larger block (the core only assert()s, and
    // Release strips asserts) — re-Reset the live pair when we grow, or when
    // the sample rate changed since it was prepared.
    ModelPair* p = slot.current();
    if (!p) {
        return;
    }
    const double rate = model_rate > 0.0 ? model_rate : sample_rate;
    if (new_max_buf > p->reset_buf || rate != p->reset_rate) {
        p->reset(rate, std::max(new_max_buf, p->reset_buf));
    }
    publish_resampling(p);
}

void NamProcessor::publish_resampling(const ModelPair* p) {
    const bool resampling = p && p->resampler_l;
    resampled_model_rate.store(resampling ? p->native_rate() : 0.0);
    resample_latency_ms.store(resampling && p->reset_rate > 0.0
                                  ? p->resampler_l->GetLatency() / p->reset_rate * 1000.0
                                  : 0.0);
}

void NamProcessor::on_sample_rate_changed(double sr) {
    // Record the rate; the following prepareBuffer() (AudioEngine always
    // calls it right after propagating a sample-rate change) re-Resets the
    // live pair with it.
    std::lock_guard<std::mutex> lock(model_mutex);
    model_rate = sr;
}

/*static*/ void NamProcessor::run_model(nam::DSP* model, Resampler* resampler, float* buf, size_t n,
                                        vector<float>& scratch) {
    // Sized by prepareBuffer(); process() bypasses oversized blocks, so this
    // must never allocate on the audio thread.
    assert(scratch.size() >= n);

    NAM_SAMPLE* in_ptr = buf;
    NAM_SAMPLE* out_ptr = scratch.data();
    NAM_SAMPLE** in_ptrs = &in_ptr;
    NAM_SAMPLE** out_ptrs = &out_ptr;
    const int frames = static_cast<int>(n); // the NAM core counts in int
    if (resampler) {
        // The lambda captures one pointer: std::function keeps it inline, no allocation.
        resampler->ProcessBlock(in_ptrs, out_ptrs, frames,
                                [model](float** in, float** out, int count) {
                                    model->process(in, out, count);
                                });
    } else {
        model->process(in_ptrs, out_ptrs, frames);
    }
    std::copy(scratch.data(), scratch.data() + n, buf);
}

// Loudness normalization. A newly bound pair takes its gain at once (the load fade-in
// hides the step); toggling normalization glides over ~20 ms.
void NamProcessor::apply_output_gain(const ModelPair* p, float* l, float* r, size_t n) {
    const float target = normalize.load(std::memory_order_relaxed) ? p->loudness_gain : 1.0f;
    if (p != gain_pair) {
        gain_pair = p;
        output_gain = target;
    }
    if (output_gain == target) {
        if (target != 1.0f) {
            for (size_t i = 0; i < n; ++i) {
                l[i] *= target;
            }
            if (r) {
                for (size_t i = 0; i < n; ++i) {
                    r[i] *= target;
                }
            }
        }
        return;
    }
    const float coeff = 1.0f - std::exp(-1.0f / (0.02f * static_cast<float>(sample_rate)));
    for (size_t i = 0; i < n; ++i) {
        output_gain += (target - output_gain) * coeff;
        l[i] *= output_gain;
        if (r) {
            r[i] *= output_gain;
        }
    }
    if (std::abs(output_gain - target) < 1e-4f * target) {
        output_gain = target;
    }
}

bool NamProcessor::process(float* l, float* r, size_t n, bool input_is_stereo) {
    // Every exit path must count the block so retired pairs get reclaimed.
    struct BlockGuard
    {
        RtSlot<ModelPair>& s;
        ~BlockGuard() {
            s.end_block();
        }
    } guard{slot};

    if (!enabled.load()) {
        return input_is_stereo;
    }

    // A stereo input stays stereo: one model per channel, never summed (user, 2026-10-03).

    // One snapshot per block: both channels run the same pair even if a load
    // lands mid-block.
    ModelPair* p = slot.acquire();

    // RT-safety guard: a block larger than what the pair was prepared for
    // would overrun the core's buffers — bypass instead (see prepareBuffer).
    const bool can_run = p && n <= p->reset_buf && n <= scratch_l.size();
    if (can_run) {
        run_model(p->l.get(), p->resampler_l.get(), l, n, scratch_l);
        if (input_is_stereo) {
            run_model(p->r.get(), p->resampler_r.get(), r, n, scratch_r);
        }
        apply_output_gain(p, l, input_is_stereo ? r : nullptr, n);
    }
    if (!input_is_stereo) {
        std::copy(l, l + n, r);
    }

    // ~15 ms fade-in after a model load/unload masks the discontinuity
    // between the old and new amp (or between amp and dry passthrough).
    if (fade_pending.exchange(false, std::memory_order_acq_rel)) {
        fade_total = std::max<size_t>(1, static_cast<size_t>(sample_rate * 0.015));
        fade_remaining = fade_total;
    }
    for (size_t i = 0; i < n && fade_remaining > 0; ++i, --fade_remaining) {
        const float g = 1.0f - static_cast<float>(fade_remaining) / static_cast<float>(fade_total);
        l[i] *= g;
        r[i] *= g;
    }

    return input_is_stereo;
}

} // namespace namx

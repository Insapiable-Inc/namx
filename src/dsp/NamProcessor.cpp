#include "NamProcessor.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include <NAM/get_dsp.h>

#include "Common.h"
#include "Log.h"

namespace npl
{

NamProcessor::NamProcessor() {
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
        pair->l = nam::get_dsp(std::filesystem::path(path));
        pair->r = nam::get_dsp(std::filesystem::path(path));
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
        slot.publish(std::move(pair));
        model_path = path;
    }
    fade_pending.store(true, std::memory_order_release);
    return true;
}

void NamProcessor::unload_model() {
    {
        std::lock_guard<std::mutex> lock(model_mutex);
        slot.publish(nullptr);
        model_path = "";
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
}

void NamProcessor::on_sample_rate_changed(double sr) {
    // Record the rate; the following prepareBuffer() (AudioEngine always
    // calls it right after propagating a sample-rate change) re-Resets the
    // live pair with it.
    std::lock_guard<std::mutex> lock(model_mutex);
    model_rate = sr;
}

/*static*/ void NamProcessor::run_model(nam::DSP* model, float* buf, size_t n,
                                        vector<float>& scratch) {
    // Sized by prepareBuffer(); process() bypasses oversized blocks, so this
    // must never allocate on the audio thread.
    assert(scratch.size() >= n);

    NAM_SAMPLE* in_ptr = buf;
    NAM_SAMPLE* out_ptr = scratch.data();
    NAM_SAMPLE** in_ptrs = &in_ptr;
    NAM_SAMPLE** out_ptrs = &out_ptr;
    model->process(in_ptrs, out_ptrs, static_cast<int>(n)); // the NAM core counts in int
    std::copy(scratch.data(), scratch.data() + n, buf);
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

    // A stereo pre-effect feeding a mono amp: sum, run one model.
    if (input_is_stereo && !stereo_amp.load(std::memory_order_relaxed)) {
        for (size_t i = 0; i < n; ++i) {
            l[i] = 0.5f * (l[i] + r[i]);
        }
        input_is_stereo = false;
    }

    // One snapshot per block: both channels run the same pair even if a load
    // lands mid-block.
    ModelPair* p = slot.acquire();

    // RT-safety guard: a block larger than what the pair was prepared for
    // would overrun the core's buffers — bypass instead (see prepareBuffer).
    const bool can_run = p && n <= p->reset_buf && n <= scratch_l.size();
    if (can_run) {
        run_model(p->l.get(), l, n, scratch_l);
        if (input_is_stereo) {
            run_model(p->r.get(), r, n, scratch_r);
        }
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

} // namespace npl

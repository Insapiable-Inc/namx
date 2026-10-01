#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <NAM/activations.h>

#include "Log.h"

AudioEngine::AudioEngine() {
    // NAM's fast tanh approximation: ~2.4× on Tanh WaveNets (the common
    // architecture), inaudible — the official plugin enables it too. Global
    // to the NAM core, so it covers every model loaded later.
    nam::activations::Activation::enable_fast_tanh();

    // Effects that are naturally stereo default to stereo mode on.
    // Flanger/Phaser stereo mode is user-controlled (default off).
    chorus.set_stereo_mode(true);
    tremolo.set_stereo_mode(true);
    reverb.set_stereo_mode(true);
    // Delay stereo (ping-pong) is on by default to match original behaviour.
    delay.set_stereo_mode(true);

    // NAM and Tuner start enabled; all others start disabled.
    nam.set_enabled(true);
    tuner.set_enabled(false);
    eq.set_enabled(true);
    ir.set_enabled(true);

    // Stable effect IDs in default processing order — the contract for chain
    // reordering and for session/preset persistence. Never rename an ID.
    effect_table = {
        {"tuner", &tuner},
        {"transpose", &transpose}, // after the tuner: it reads the real string
        {"gate", &gate},
        {"comp", &comp},
        {"wah", &wah},
        {"flanger", &flanger},
        {"phaser", &phaser},
        {"drive", &drive}, // Phase 17 — new IDs re-enter old sessions here
        {"nam", &nam},
        {"eq", &eq},
        {"doubler", &doubler},
        {"chorus", &chorus},
        {"tremolo", &tremolo},
        {"delay", &delay},
        {"reverb", &reverb},
        {"ir", &ir},
        {"volume", &volume},
    };
    auto chain = std::make_unique<Chain>();
    chain->reserve(effect_table.size());
    for (const auto& [id, fx] : effect_table) {
        chain->push_back(fx);
    }
    chain_slot.publish(std::move(chain));
}

// ---------------------------------------------------------------------------
// Model / IR wrappers
// ---------------------------------------------------------------------------

void AudioEngine::reclaim_retired() {
    // Only valid with no audio callback possible: after stop(), or before
    // the backend was ever started.
    const bool stopped = !is_running();
    chain_slot.reclaim(stopped);
    nam.reclaim_retired(stopped);
    ir.reclaim_retired(stopped);
}

bool AudioEngine::load_model(const std::string& path) {
    const bool ok = nam.load_model(path);
    reclaim_retired();
    return ok;
}

void AudioEngine::unload_model() {
    nam.unload_model();
    reclaim_retired();
}

bool AudioEngine::load_ir(const std::string& path) {
    const bool ok = ir.load(path);
    reclaim_retired();
    return ok;
}

void AudioEngine::unload_ir() {
    ir.unload();
    reclaim_retired();
}

bool AudioEngine::load_irb(const std::string& path) {
    const bool ok = ir.load_b(path);
    reclaim_retired();
    return ok;
}

void AudioEngine::unload_irb() {
    ir.unload_b();
    reclaim_retired();
}

// ---------------------------------------------------------------------------
// Chain order
// ---------------------------------------------------------------------------

std::vector<std::string> AudioEngine::default_chain_ids() const {
    std::vector<std::string> ids;
    ids.reserve(effect_table.size());
    for (const auto& [id, fx] : effect_table) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> AudioEngine::chain_ids() const {
    std::vector<std::string> ids;
    ids.reserve(effect_table.size());
    for (const auto* fx : *chain_slot.current()) {
        ids.push_back(id_for_effect(fx));
    }
    return ids;
}

EffectBase* AudioEngine::effect_by_id(const std::string& id) const {
    for (const auto& [eid, fx] : effect_table) {
        if (eid == id) {
            return fx;
        }
    }
    return nullptr;
}

std::string AudioEngine::id_for_effect(const EffectBase* fx) const {
    for (const auto& [eid, e] : effect_table) {
        if (e == fx) {
            return eid;
        }
    }
    return {};
}

void AudioEngine::set_chain_order(const std::vector<std::string>& ids) {
    // Sanitize: known IDs only, no duplicates.
    std::vector<EffectBase*> new_chain;
    new_chain.reserve(effect_table.size());
    for (const auto& id : ids) {
        EffectBase* fx = effect_by_id(id);
        if (!fx) {
            continue;
        }
        if (std::find(new_chain.begin(), new_chain.end(), fx) != new_chain.end()) {
            continue;
        }
        new_chain.push_back(fx);
    }

    // Effects missing from the list re-enter at their default position.
    size_t default_idx = 0;
    for (const auto& [id, fx] : effect_table) {
        if (std::find(new_chain.begin(), new_chain.end(), fx) == new_chain.end()) {
            const auto slot =
                static_cast<Chain::difference_type>(std::min(default_idx, new_chain.size()));
            new_chain.insert(new_chain.begin() + slot, fx); // iterator offsets are signed
        }
        ++default_idx;
    }

    const Chain& cur = *chain_slot.current();
    if (new_chain == cur) {
        return;
    }

    // Moved effects carry state from their old slot (delay tails, diverged
    // per-channel filters) — have the audio thread clear them.
    for (size_t i = 0; i < new_chain.size(); ++i) {
        if (i >= cur.size() || cur[i] != new_chain[i]) {
            new_chain[i]->request_reset();
        }
    }

    // Publish a fresh vector; the one the audio thread may still be iterating
    // is retired and freed only once it has provably moved on. No waiting.
    chain_slot.publish(std::make_unique<Chain>(std::move(new_chain)));
    reclaim_retired();
}

AudioEngine::~AudioEngine() {
    stop();
}

bool AudioEngine::start(const std::string& client_name) {
    if (backend && backend->is_running()) {
        return true;
    }

    backend = IAudioBackend::create();
    if (!backend) {
        qCWarning(lc_audio, "[AudioEngine] No audio backend available for this platform");
        return false;
    }

    // Sample rate and buffer size reach the effects via on_buffer_size_changed,
    // which the backend invokes BEFORE activating the stream — configuring
    // effects after activation would race the audio thread.
    if (!backend->start(client_name, this)) {
        backend.reset();
        return false;
    }

    return true;
}

bool AudioEngine::start_with_devices(const std::string& input_device_id,
                                     const std::string& output_device_id,
                                     const std::string& client_name) {
    stop();

    backend = IAudioBackend::create();
    if (!backend) {
        qCWarning(lc_audio, "[AudioEngine] No audio backend available for this platform");
        return false;
    }

    if (!backend->start_with_devices(client_name, this, input_device_id, output_device_id)) {
        backend.reset();
        return false;
    }

    return true;
}

void AudioEngine::stop() {
    if (backend) {
        backend->stop();
        backend.reset();
    }
    // No callback can run now — drop everything retired while it was live.
    reclaim_retired();
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerate_input_devices() const {
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerate_input_devices() : std::vector<IAudioBackend::DeviceInfo>{};
}

std::vector<IAudioBackend::DeviceInfo> AudioEngine::enumerate_output_devices() const {
    auto temp = IAudioBackend::create();
    return temp ? temp->enumerate_output_devices() : std::vector<IAudioBackend::DeviceInfo>{};
}

// ---- IAudioBackend::Callback ----------------------------------------

void AudioEngine::on_buffer_size_changed(int new_size) {
    if (new_size <= 0) {
        return;
    }
    const size_t n = static_cast<size_t>(new_size); // backends report int; positive here
    if (n > work_buf_l.size()) {
        work_buf_l.resize(n, 0.0f);
        work_buf_r.resize(n, 0.0f);
    }

    // Backends call this before activating the stream, and with processing
    // suspended on later changes — the one safe point to reconfigure effects.
    const double sr = backend ? backend->get_sample_rate() : 0.0;
    if (sr > 0.0 && sr != chain_sample_rate) {
        chain_sample_rate = sr;
        for (const auto& [id, fx] : effect_table) {
            fx->set_sample_rate(sr);
        }
        metronome.set_sample_rate(sr);
        limiter.set_sample_rate(sr);
        // ~20 ms one-pole gain smoothing, independent of sample rate.
        smooth_coeff = std::exp(-1.0f / (0.020f * static_cast<float>(sr)));
    }

    // After SR propagation, so a re-Reset() picks up the new rate.
    nam.prepare_buffer(std::max(n, nam.get_max_buf()));
}

void AudioEngine::on_audio(const float* in, float* out_l, float* out_r, size_t n) {
    // DSP load: wall time of the block vs its real-time duration. steady_clock
    // is a vDSO read — no syscall, RT-safe.
    const auto t0 = std::chrono::steady_clock::now();
    process_block(in, out_l, out_r, n);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (n > 0 && chain_sample_rate > 0.0) {
        const float load = static_cast<float>(secs * chain_sample_rate / static_cast<double>(n));
        if (load > dsp_load_peak.load(std::memory_order_relaxed)) {
            dsp_load_peak.store(load, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::process_block(const float* in, float* out_l, float* out_r, size_t n) {
    // Reorder fence: every exit counts the block so setChainOrder() knows
    // when a retired chain vector is safe to free.
    struct BlockGuard
    {
        RtSlot<Chain>& s;
        ~BlockGuard() {
            s.end_block();
        }
    } guard{chain_slot};

    // Defensive: if a process callback ever fires before on_buffer_size_changed
    // has grown the work buffers, emit silence rather than overrunning.
    if (static_cast<size_t>(n) > work_buf_l.size()) {
        std::fill_n(out_l, n, 0.0f);
        std::fill_n(out_r, n, 0.0f);
        return;
    }

    // 1. Input gain (smoothed) → both work buffers (identical at this point)
    const float target_in_gain = db_to_linear(input_gain_db.load());
    float in_peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        input_gain_smooth =
            smooth_coeff * input_gain_smooth + (1.0f - smooth_coeff) * target_in_gain;
        float s = in[i] * input_gain_smooth;
        work_buf_l[i] = s;
        work_buf_r[i] = s;
        in_peak = std::max(in_peak, std::abs(s));
    }
    // Peak-hold: accumulate the max until the GUI takes and resets it, so the
    // 50 ms poll can't miss transients between blocks.
    if (in_peak > input_peak.load(std::memory_order_relaxed)) {
        input_peak.store(in_peak, std::memory_order_relaxed);
    }

    // 2. Run the signal chain (chain vector bound once per callback; a
    //    reorder publishes a new vector and never touches this one)
    bool stereo = false;
    const Chain& chain = *chain_slot.acquire();
    for (auto* fx : chain) {
        fx->consume_pending_reset();
        stereo = fx->process(work_buf_l.data(), work_buf_r.data(), n, stereo);
    }

    (void)stereo; // consumed by downstream effects; output always written to both channels

    // 3. Output gain (smoothed) + write to output buffers
    const float target_out_gain = db_to_linear(output_gain_db.load());
    for (size_t i = 0; i < n; ++i) {
        output_gain_smooth =
            smooth_coeff * output_gain_smooth + (1.0f - smooth_coeff) * target_out_gain;
        out_l[i] = work_buf_l[i] * output_gain_smooth;
        out_r[i] = work_buf_r[i] * output_gain_smooth;
    }

    // 4. Output limiter (look-ahead; a no-op when off), then the meter, so
    //    it shows what actually leaves the app.
    limiter.process(out_l, out_r, n);
    float out_peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        out_peak = std::max(out_peak, std::max(std::abs(out_l[i]), std::abs(out_r[i])));
    }
    if (out_peak > output_peak.load(std::memory_order_relaxed)) {
        output_peak.store(out_peak, std::memory_order_relaxed);
    }

    // 5. Metronome on top: own volume/pan, after the meter so the VU shows
    //    the guitar only.
    metronome.process(out_l, out_r, n);
}

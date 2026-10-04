#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>

#include "Common.h"

namespace namx
{

// Abstract base for all audio effect processors.
//
// Provides: enable/bypass, stereo-mode flag, sample rate, and a unified
// process() dispatch that encodes all four routing cases:
//
//   enabled  stereo_mode + supports_stereo_out  input_is_stereo  action
//   -------  --------------------------------  ---------------  ------
//   false    -                                 any              pass-through (no-op)
//   true     false                             false            process_block_mono(l), copy l→r
//   true     false                             true             process_block_dual(l, r): per channel
//   true     true                              false            process_block_stereo_out(l, r)
//   true     true                              true             process_block_dual(l, r): toggle ignored
//
// Stereo input always wins: the effect is applied as a mono effect to each
// channel independently (process_block_dual), regardless of the stereo toggle.
//
// Returns true if the output buffers now contain different content (stereo).
class EffectBase
{
public:
    virtual ~EffectBase() = default;

    // Non-virtual: stores SR then calls on_sample_rate_changed() hook.
    void set_sample_rate(double sr) {
        sample_rate = sr;
        rate_for_any_thread.store(sr, std::memory_order_relaxed);
        on_sample_rate_changed(sr);
    }

    void set_enabled(bool e) {
        enabled.store(e);
    }
    void set_stereo_mode(bool s) {
        stereo_mode.store(s);
    }

    bool is_enabled() const {
        return enabled.load();
    }
    bool is_stereo_mode() const {
        return stereo_mode.load();
    }

    // Returns true if this effect can produce L≠R output from mono input.
    virtual bool supports_stereo_out() const {
        return false;
    }

    virtual void reset() = 0;

    // Processing latency this effect adds to the signal right now, in ms (0 for most).
    // Any thread: implementations read atomics only.
    virtual double latency_ms() const {
        return 0.0;
    }

    // Ask the audio thread to reset() this effect at the top of its next
    // block (used on chain reorder to drop stale per-channel/tail state).
    // reset() implementations must stay allocation-free for this to be RT-safe.
    void request_reset() {
        reset_pending.store(true, std::memory_order_release);
    }

    // Called by the engine on the audio thread before process().
    void consume_pending_reset() {
        if (reset_pending.exchange(false, std::memory_order_acq_rel)) {
            reset();
            right_fed = true; // both channels start over together
        }
    }

    // Unified dispatch entry point. L and R are always valid float buffers of length n.
    // inputIsStereo: caller signals whether L and R already contain different content.
    // Returns: true if L and R now contain different content.
    virtual bool process(float* l, float* r, size_t n, bool input_is_stereo) {
        if (!enabled.load()) {
            return input_is_stereo;
        }

        if (input_is_stereo) {
            // Stereo in → apply the mono effect to each channel independently,
            // ignoring the stereo toggle. After mono blocks the right channel's
            // state is whatever it last held (review item 19): line it up first.
            if (!right_fed) {
                sync_right_channel();
                right_fed = true;
            }
            process_block_dual(l, r, n);
            return true;
        }

        if (stereo_mode.load() && supports_stereo_out()) {
            // Mono in + stereo mode → true stereo output (effects with this path
            // keep both channels' state running; see each sync_right_channel).
            process_block_stereo_out(l, r, n);
            return true;
        }

        // Mono in + mono mode → process once, copy to R
        process_block_mono(l, n);
        std::copy(l, l + n, r);
        right_fed = false;
        return false;
    }

protected:
    // Hook called whenever setSampleRate() is invoked (instead of overriding setSampleRate).
    virtual void on_sample_rate_changed(double /*sr*/) {
    }

    // The input just turned stereo after mono blocks, in which only the left
    // channel's state advanced: bring the right channel's state in line with it
    // (copy where that is cheap, clear where it isn't) so it doesn't resume an
    // old tail or start cold. Audio thread, allocation-free. The default is for
    // effects without per-channel state.
    virtual void sync_right_channel() {
    }

    // Mono in-place processing. Called only with mono input (single channel).
    virtual void process_block_mono(float* /*buf*/, size_t /*n*/) {
    }

    // Stereo input → the mono effect applied to each channel independently.
    // Stateful effects (delay lines, filters, LFOs) MUST override this with
    // separate per-channel state; the default of calling process_block_mono
    // twice is only correct for effects that never see stereo input.
    virtual void process_block_dual(float* l, float* r, size_t n) {
        process_block_mono(l, n);
        process_block_mono(r, n);
    }

    // True stereo output from mono input.
    // Called only when supports_stereo_out() == true && !inputIsStereo.
    // Default falls back to calling process_block_mono on each channel independently.
    virtual void process_block_stereo_out(float* l, float* r, size_t n) {
        process_block_mono(l, n);
        process_block_mono(r, n);
    }

    double sample_rate{44100.0};
    // The same rate for latency_ms() and other GUI-thread readers: sample_rate itself
    // is written on the backend's notification thread (audit 2026-10-04, Fable item 18).
    atomic<double> rate_for_any_thread{44100.0};
    atomic<bool> enabled{false};
    atomic<bool> stereo_mode{false};
    atomic<bool> reset_pending{false};
    bool right_fed{
        true}; // audio thread: has the right channel's state run since the last mono block?
};

} // namespace namx

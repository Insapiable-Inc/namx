#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ChorusProcessor.h"
#include "Common.h"
#include "CompressorProcessor.h"
#include "DelayProcessor.h"
#include "DriveProcessor.h"
#include "DynamicWahProcessor.h"
#include "EQProcessor.h"
#include "FlangerProcessor.h"
#include "IAudioBackend.h"
#include "IRProcessor.h"
#include "Metronome.h"
#include "NamProcessor.h"
#include "NoiseGateProcessor.h"
#include "OutputLimiter.h"
#include "PhaserProcessor.h"
#include "ReverbProcessor.h"
#include "RtSlot.h"
#include "TransposeProcessor.h"
#include "TremoloProcessor.h"
#include "TunerProcessor.h"
#include "VolumeProcessor.h"

namespace namx
{

// Platform-agnostic audio engine. Owns the signal chain and all DSP
// processors. Delegates I/O to an IAudioBackend (JACK on Linux, WASAPI on
// Windows) created via IAudioBackend::create().
class AudioEngine : public IAudioBackend::Callback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool start(const string& client_name = "NAM-X");
    void stop();

    bool start_with_devices(const string& input_device_id, const string& output_device_id,
                            const string& client_name = "NAM-X");

    // Exclusive device access (see IAudioBackend); applies on the next start.
    bool supports_exclusive_mode() const;
    void set_exclusive_mode(bool on) {
        exclusive_wanted = on;
    }
    bool is_exclusive_wanted() const {
        return exclusive_wanted;
    }
    bool exclusive_active() const {
        return backend && backend->exclusive_active();
    }

    bool is_running() const {
        return backend && backend->is_running();
    }
    double sample_rate() const {
        return backend ? backend->get_sample_rate() : 44100.0;
    }
    int buffer_size() const {
        return backend ? backend->get_buffer_size() : 0;
    }

    // Human-readable status, e.g. "JACK ● 48000 Hz ● stereo out".
    string backend_status() const {
        return backend ? backend->status_string() : string{};
    }
    bool supports_port_routing() const {
        return backend && backend->supports_port_routing();
    }

    // Device enumeration (creates temp backend, so works before start())
    vector<IAudioBackend::DeviceInfo> enumerate_input_devices() const;
    vector<IAudioBackend::DeviceInfo> enumerate_output_devices() const;

    // Model / IR (thin wrappers for MainWindow convenience)
    // Loads publish through RtSlots; when audio is stopped nothing counts
    // blocks, so the wrappers reclaim retired objects eagerly in that case.
    bool load_model(const string& path);
    void unload_model();
    bool is_model_loaded() const {
        return nam.is_model_loaded();
    }
    string model_path() const {
        return nam.get_model_path();
    }
    string model_load_error() const {
        return nam.get_last_error();
    }

    bool load_ir(const string& path);
    void unload_ir();
    bool is_ir_loaded() const {
        return ir.is_loaded();
    }
    string ir_path() const {
        return ir.file_path();
    }
    string ir_load_error() const {
        return ir.last_error();
    }

    // Second cab slot (Phase 17 dual IR).
    bool load_irb(const string& path);
    void unload_irb();
    bool is_irb_loaded() const {
        return ir.is_loaded_b();
    }
    string ir_path_b() const {
        return ir.file_path_b();
    }
    string ir_b_load_error() const {
        return ir.last_error_b();
    }

    // Gain
    void set_input_gain(float db) {
        input_gain_db.store(db);
    }
    void set_output_gain(float db) {
        output_gain_db.store(db);
    }
    float input_gain() const {
        return input_gain_db.load();
    }
    float output_gain() const {
        return output_gain_db.load();
    }

    // Effect accessors (MainWindow calls setters directly on the effects)
    TunerProcessor& get_tuner() {
        return tuner;
    }
    NoiseGateProcessor& get_gate() {
        return gate;
    }
    CompressorProcessor& get_comp() {
        return comp;
    }
    DynamicWahProcessor& get_wah() {
        return wah;
    }
    FlangerProcessor& get_flanger() {
        return flanger;
    }
    PhaserProcessor& get_phaser() {
        return phaser;
    }
    NamProcessor& get_nam() {
        return nam;
    }
    EQProcessor& get_eq() {
        return eq;
    }
    ChorusProcessor& get_chorus() {
        return chorus;
    }
    TremoloProcessor& get_tremolo() {
        return tremolo;
    }
    DelayProcessor& get_delay() {
        return delay;
    }
    ReverbProcessor& get_reverb() {
        return reverb;
    }
    IRProcessor& get_ir() {
        return ir;
    }
    DriveProcessor& get_drive() {
        return drive;
    }
    VolumeProcessor& get_volume() {
        return volume;
    }
    OutputLimiter& get_limiter() {
        return limiter;
    }
    TransposeProcessor& get_transpose() {
        return transpose;
    }

    // Mixed in after the output gain — outside the chain, not metered.
    Metronome& get_metronome() {
        return metronome;
    }

    // Signal chain (processing order = UI order)
    // GUI-thread snapshot of the active chain. Reordering goes through
    // setChainOrder().
    vector<EffectBase*> get_chain() const {
        return *chain_slot.current();
    }

    // Stable effect IDs in default (construction) order — the persistence
    // contract for session state and preset schema v2.
    vector<string> default_chain_ids() const;
    // Current chain order as IDs.
    vector<string> chain_ids() const;
    EffectBase* effect_by_id(const string& id) const;
    string id_for_effect(const EffectBase* fx) const;

    // Reorder the chain RT-safely (RtSlot publish, same pattern as model
    // swaps — never blocks the GUI). Input is sanitized: unknown/duplicate
    // IDs are dropped and missing effects re-enter at their default
    // position. Effects whose position changed get a pending reset,
    // consumed by the audio thread at the top of their next block.
    void set_chain_order(const vector<string>& ids);

    // Port-routing pass-through (no-ops on backends without it)
    using PortConnections = IAudioBackend::PortConnections;
    PortConnections current_connections() const {
        return backend ? backend->current_connections() : PortConnections{};
    }
    void apply_connections(const PortConnections& c) {
        if (backend) {
            backend->apply_connections(c);
        }
    }

    // VU meters
    // The audio thread accumulates the peak (max) until the GUI takes it;
    // take-and-reset so no transient between polls is ever missed.
    float take_input_peak() {
        return input_peak.exchange(0.0f);
    }
    float take_output_peak() {
        return output_peak.exchange(0.0f);
    }

    // Diagnostics (status bar)
    // Highest audio-thread time per block since the last take, as a
    // fraction of the block's duration (1.0 = the whole budget).
    float take_dsp_load_peak() {
        return dsp_load_peak.exchange(0.0f);
    }
    int xrun_count() const {
        return backend ? backend->xrun_count() : -1;
    }
    // Blocks in which a non-finite sample was scrubbed since the last take
    // (process_block step 5).
    int take_non_finite_blocks() {
        return non_finite_blocks.exchange(0, std::memory_order_relaxed);
    }
    void reset_xruns() {
        if (backend) {
            backend->reset_xruns();
        }
    }
    // Processing latency the chain adds right now (effects + output limiter), each
    // nonzero source with a display name. GUI thread.
    struct LatencySource
    {
        string name;
        double ms;
    };
    vector<LatencySource> processing_latency() const;

    int io_latency_frames() const {
        return backend ? backend->io_latency_frames() : -1;
    }

    // IAudioBackend::Callback
    void on_audio(const float* in, float* out_l, float* out_r, size_t nframes) override;
    void on_buffer_size_changed(int new_size) override;

private:
    unique_ptr<IAudioBackend> backend;
    bool exclusive_wanted = false; // GUI thread; handed to each new backend

    // Signal chain — published through an RtSlot. The audio thread binds
    // one vector per callback; setChainOrder() publishes a new one and the
    // old vector is freed only after the audio thread has provably left it.
    using Chain = vector<EffectBase*>;
    RtSlot<Chain> chain_slot;

    // Free objects retired by loads/reorders while the audio thread is idle.
    void reclaim_retired();

    // id → effect, in default chain order
    vector<std::pair<string, EffectBase*>> effect_table;

    // Effect instances (owned here, pointers in the chain buffers)
    TunerProcessor tuner;
    NoiseGateProcessor gate;
    CompressorProcessor comp;
    DynamicWahProcessor wah;
    FlangerProcessor flanger;
    PhaserProcessor phaser;
    NamProcessor nam;
    EQProcessor eq;
    ChorusProcessor chorus;
    TremoloProcessor tremolo;
    DelayProcessor delay;
    ReverbProcessor reverb;
    IRProcessor ir;
    DriveProcessor drive;
    VolumeProcessor volume;
    OutputLimiter limiter;
    TransposeProcessor transpose;
    Metronome metronome;

    // Gain
    atomic<float> input_gain_db{0.0f};
    atomic<float> output_gain_db{0.0f};

    float input_gain_smooth = 1.0f;
    float output_gain_smooth = 1.0f;

    atomic<float> input_peak{0.0f};
    atomic<float> output_peak{0.0f};
    atomic<float> dsp_load_peak{0.0f};
    atomic<int> non_finite_blocks{0};

    // on_audio = timing around this.
    void process_block(const float* in, float* out_l, float* out_r, size_t nframes);

    vector<float> work_buf_l;
    vector<float> work_buf_r;

    // Set from on_buffer_size_changed (never concurrent with the audio thread).
    double chain_sample_rate{0.0};
    float smooth_coeff{0.999f}; // recomputed per sample rate for a ~20 ms time constant

    static inline float db_to_linear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }
};

} // namespace namx

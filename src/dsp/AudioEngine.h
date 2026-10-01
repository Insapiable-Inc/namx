#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ChorusProcessor.h"
#include "CompressorProcessor.h"
#include "DelayProcessor.h"
#include "DoublerProcessor.h"
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

/// Platform-agnostic audio engine. Owns the signal chain and all DSP
/// processors. Delegates I/O to an IAudioBackend (JACK on Linux, WASAPI on
/// Windows) created via IAudioBackend::create().
class AudioEngine : public IAudioBackend::Callback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool start(const std::string& client_name = "NAMPlayer");
    void stop();

    bool start_with_devices(const std::string& input_device_id, const std::string& output_device_id,
                            const std::string& client_name = "NAMPlayer");

    bool is_running() const {
        return m_backend && m_backend->is_running();
    }
    double sample_rate() const {
        return m_backend ? m_backend->sample_rate() : 44100.0;
    }
    int buffer_size() const {
        return m_backend ? m_backend->buffer_size() : 0;
    }

    /// Human-readable status, e.g. "JACK ● 48000 Hz ● stereo out".
    std::string backend_status() const {
        return m_backend ? m_backend->status_string() : std::string{};
    }
    bool supports_port_routing() const {
        return m_backend && m_backend->supports_port_routing();
    }

    // ---- Device enumeration (creates temp backend, so works before start()) ----
    std::vector<IAudioBackend::DeviceInfo> enumerate_input_devices() const;
    std::vector<IAudioBackend::DeviceInfo> enumerate_output_devices() const;

    // ---- Model / IR (thin wrappers for MainWindow convenience) ----
    // Loads publish through RtSlots; when audio is stopped nothing counts
    // blocks, so the wrappers reclaim retired objects eagerly in that case.
    bool load_model(const std::string& path);
    void unload_model();
    bool is_model_loaded() const {
        return m_nam.is_model_loaded();
    }
    std::string model_path() const {
        return m_nam.model_path();
    }
    std::string model_load_error() const {
        return m_nam.last_error();
    }

    bool load_ir(const std::string& path);
    void unload_ir();
    bool is_ir_loaded() const {
        return m_ir.is_loaded();
    }
    std::string ir_path() const {
        return m_ir.file_path();
    }
    std::string ir_load_error() const {
        return m_ir.last_error();
    }

    // Second cab slot (Phase 17 dual IR).
    bool load_irb(const std::string& path);
    void unload_irb();
    bool is_irb_loaded() const {
        return m_ir.is_loaded_b();
    }
    std::string ir_path_b() const {
        return m_ir.file_path_b();
    }
    std::string ir_b_load_error() const {
        return m_ir.last_error_b();
    }

    // ---- Gain ----
    void set_input_gain(float d_b) {
        m_input_gain_db.store(d_b);
    }
    void set_output_gain(float d_b) {
        m_output_gain_db.store(d_b);
    }
    float input_gain() const {
        return m_input_gain_db.load();
    }
    float output_gain() const {
        return m_output_gain_db.load();
    }

    // ---- Effect accessors (MainWindow calls setters directly on the effects) ----
    TunerProcessor& tuner() {
        return m_tuner;
    }
    NoiseGateProcessor& gate() {
        return m_gate;
    }
    CompressorProcessor& comp() {
        return m_comp;
    }
    DynamicWahProcessor& wah() {
        return m_wah;
    }
    FlangerProcessor& flanger() {
        return m_flanger;
    }
    PhaserProcessor& phaser() {
        return m_phaser;
    }
    NamProcessor& nam() {
        return m_nam;
    }
    EQProcessor& eq() {
        return m_eq;
    }
    ChorusProcessor& chorus() {
        return m_chorus;
    }
    TremoloProcessor& tremolo() {
        return m_tremolo;
    }
    DelayProcessor& delay() {
        return m_delay;
    }
    ReverbProcessor& reverb() {
        return m_reverb;
    }
    IRProcessor& ir() {
        return m_ir;
    }
    DriveProcessor& drive() {
        return m_drive;
    }
    DoublerProcessor& doubler() {
        return m_doubler;
    }
    VolumeProcessor& volume() {
        return m_volume;
    }
    OutputLimiter& limiter() {
        return m_limiter;
    }
    TransposeProcessor& transpose() {
        return m_transpose;
    }

    /// Mixed in after the output gain — outside the chain, not metered.
    Metronome& metronome() {
        return m_metronome;
    }

    // ---- Signal chain (processing order = UI order) ----
    // GUI-thread snapshot of the active chain. Reordering goes through
    // setChainOrder().
    std::vector<EffectBase*> chain() const {
        return *m_chain.current();
    }

    /// Stable effect IDs in default (construction) order — the persistence
    /// contract for session state and preset schema v2.
    std::vector<std::string> default_chain_ids() const;
    /// Current chain order as IDs.
    std::vector<std::string> chain_ids() const;
    EffectBase* effect_by_id(const std::string& id) const;
    std::string id_for_effect(const EffectBase* fx) const;

    /// Reorder the chain RT-safely (RtSlot publish, same pattern as model
    /// swaps — never blocks the GUI). Input is sanitized: unknown/duplicate
    /// IDs are dropped and missing effects re-enter at their default
    /// position. Effects whose position changed get a pending reset,
    /// consumed by the audio thread at the top of their next block.
    void set_chain_order(const std::vector<std::string>& ids);

    // ---- Port-routing pass-through (no-ops on backends without it) ----
    using PortConnections = IAudioBackend::PortConnections;
    PortConnections current_connections() const {
        return m_backend ? m_backend->current_connections() : PortConnections{};
    }
    void apply_connections(const PortConnections& c) {
        if (m_backend) {
            m_backend->apply_connections(c);
        }
    }

    // ---- VU meters ----
    // The audio thread accumulates the peak (max) until the GUI takes it;
    // take-and-reset so no transient between polls is ever missed.
    float take_input_peak() {
        return m_input_peak.exchange(0.0f);
    }
    float take_output_peak() {
        return m_output_peak.exchange(0.0f);
    }

    // ---- Diagnostics (status bar) ----
    /// Highest audio-thread time per block since the last take, as a
    /// fraction of the block's duration (1.0 = the whole budget).
    float take_dsp_load_peak() {
        return m_dsp_load_peak.exchange(0.0f);
    }
    int xrun_count() const {
        return m_backend ? m_backend->xrun_count() : -1;
    }
    void reset_xruns() {
        if (m_backend) {
            m_backend->reset_xruns();
        }
    }
    int io_latency_frames() const {
        return m_backend ? m_backend->io_latency_frames() : -1;
    }

    // ---- IAudioBackend::Callback ----
    void on_audio(const float* in, float* out_l, float* out_r, size_t nframes) override;
    void on_buffer_size_changed(int new_size) override;

private:
    std::unique_ptr<IAudioBackend> m_backend;

    // Signal chain — published through an RtSlot. The audio thread binds
    // one vector per callback; setChainOrder() publishes a new one and the
    // old vector is freed only after the audio thread has provably left it.
    using Chain = std::vector<EffectBase*>;
    RtSlot<Chain> m_chain;

    /// Free objects retired by loads/reorders while the audio thread is idle.
    void reclaim_retired();

    // id → effect, in default chain order
    std::vector<std::pair<std::string, EffectBase*>> m_effect_table;

    // Effect instances (owned here, pointers in the chain buffers)
    TunerProcessor m_tuner;
    NoiseGateProcessor m_gate;
    CompressorProcessor m_comp;
    DynamicWahProcessor m_wah;
    FlangerProcessor m_flanger;
    PhaserProcessor m_phaser;
    NamProcessor m_nam;
    EQProcessor m_eq;
    ChorusProcessor m_chorus;
    TremoloProcessor m_tremolo;
    DelayProcessor m_delay;
    ReverbProcessor m_reverb;
    IRProcessor m_ir;
    DriveProcessor m_drive;
    DoublerProcessor m_doubler;
    VolumeProcessor m_volume;
    OutputLimiter m_limiter;
    TransposeProcessor m_transpose;
    Metronome m_metronome;

    // Gain
    std::atomic<float> m_input_gain_db{0.0f};
    std::atomic<float> m_output_gain_db{0.0f};

    float m_input_gain_smooth = 1.0f;
    float m_output_gain_smooth = 1.0f;

    std::atomic<float> m_input_peak{0.0f};
    std::atomic<float> m_output_peak{0.0f};
    std::atomic<float> m_dsp_load_peak{0.0f};

    // on_audio = timing around this.
    void process_block(const float* in, float* out_l, float* out_r, size_t nframes);

    std::vector<float> m_work_buf_l;
    std::vector<float> m_work_buf_r;

    // Set from on_buffer_size_changed (never concurrent with the audio thread).
    double m_chain_sample_rate{0.0};
    float m_smooth_coeff{0.999f}; // recomputed per sample rate for a ~20 ms time constant

    static inline float db_to_linear(float d_b) {
        return std::pow(10.0f, d_b / 20.0f);
    }
};

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/// Practice metronome (Phase 15). Not an effect: AudioEngine mixes it into
/// the output after the output gain, on its own volume and pan, so it keeps
/// running across preset loads and never goes through the chain.
///
/// RT-safety: click samples for every sound set are synthesized in
/// setSampleRate() (non-RT — called from on_buffer_size_changed); process()
/// only reads them. All settings are atomics read once per block.
///
/// Timing: BPM counts quarter notes (the same tempo the sync knobs use);
/// the meter's unit sets what a beat is (x/8 → eighths at 2 × BPM). Each beat
/// can be split into 1–4 subdivisions. A sample-accurate double accumulator
/// places every click.
class Metronome
{
public:
    static constexpr int K_MAX_BEATS = 16;
    enum Accent : uint32_t
    {
        MUTE = 0,
        SOFT = 1,
        NORMAL = 2,
        STRONG = 3
    };
    enum Sound
    {
        CLICK = 0,
        WOOD,
        BEEP,
        COWBELL,
        K_SOUND_COUNT
    };

    Metronome();

    void set_sample_rate(double new_sr); // non-RT: rebuilds the click samples

    // ---- Settings (any thread) ----
    void set_running(bool on) {
        m_running.store(on);
    }
    bool is_running() const {
        return m_running.load();
    }
    void set_bpm(double new_bpm) {
        m_bpm.store(new_bpm);
    }
    void set_meter(int new_beats, int new_unit);
    int get_beats() const {
        return m_beats.load();
    }
    int get_unit() const {
        return m_unit.load();
    }
    /// 2 bits per beat (Accent), beat 0 in the low bits.
    void set_accents(uint32_t packed) {
        m_accents.store(packed);
    }
    uint32_t get_accents() const {
        return m_accents.load();
    }
    static Accent accent_of(uint32_t packed, int beat) {
        return Accent((packed >> (2 * beat)) & 3u);
    }
    static uint32_t with_accent(uint32_t packed, int beat, Accent a) {
        return (packed & ~(3u << (2 * beat))) | (uint32_t(a) << (2 * beat));
    }
    /// Default pattern: strong downbeat; compound meters (6/8, 9/8, 12/8 …)
    /// stress each group of three; everything else Normal.
    static uint32_t default_accents(int beats, int unit);

    void set_subdivision(int n) {
        m_subdiv.store(n < 1 ? 1 : n > 4 ? 4 : n);
    }
    void set_sound(int s) {
        m_sound.store(s < 0 || s >= K_SOUND_COUNT ? 0 : static_cast<size_t>(s));
    }
    void set_volume_db(float db) {
        m_volume_db.store(db);
    }
    void set_pan(float new_pan) {
        m_pan.store(new_pan);
    } // -1 … +1
    /// Bars counted in (all beats audible, downbeat strong) when starting.
    void set_count_in_bars(int bars) {
        m_count_in_bars.store(bars < 0 ? 0 : bars > 4 ? 4 : bars);
    }
    /// Silent after the count-in: the beat LEDs keep time, the click stops.
    void set_silent_after_count_in(bool s) {
        m_silent_after.store(s);
    }

    // ---- GUI polling ----
    /// Beat within the bar for the LEDs; -1 when stopped.
    int current_beat() const {
        return m_beat_now.load(std::memory_order_relaxed);
    }
    bool in_count_in() const {
        return m_count_in_now.load(std::memory_order_relaxed);
    }

    // ---- Audio thread ----
    /// Adds the click into L/R (after the output stage).
    void process(float* l, float* r, size_t n);

private:
    struct Voice
    {
        const float* data = nullptr;
        size_t len = 0;
        size_t pos = 0;
        float gain = 0.0f;
    };

    void trigger(const std::vector<float>& sample, float gain);
    void render(float* l, float* r, size_t from, size_t to, float g_l, float g_r);
    void build_sounds(double sr);

    // Click samples: [sound][0 = accent, 1 = beat, 2 = subdivision].
    std::array<std::array<std::vector<float>, 3>, K_SOUND_COUNT> m_samples;
    double m_sample_rate = 48000.0;

    std::atomic<bool> m_running{false};
    std::atomic<double> m_bpm{120.0};
    std::atomic<int> m_beats{4};
    std::atomic<int> m_unit{4};
    std::atomic<uint32_t> m_accents;
    std::atomic<int> m_subdiv{1};
    std::atomic<size_t> m_sound{CLICK};
    std::atomic<float> m_volume_db{-6.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<int> m_count_in_bars{0};
    std::atomic<bool> m_silent_after{false};

    std::atomic<int> m_beat_now{-1};
    std::atomic<bool> m_count_in_now{false};

    // Audio-thread state.
    bool m_was_running = false;
    double m_until_next = 0.0; // samples from block start to the next tick
    int m_tick = 0; // subdivision index within the beat
    int m_beat = 0; // beat within the bar
    int m_count_in_left = 0; // bars of count-in still to play
    std::array<Voice, 2> m_voices{};
};

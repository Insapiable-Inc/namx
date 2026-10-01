#include "TunerProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>

TunerProcessor::TunerProcessor() {
    ring_buf.fill(0.f);
    snap_buf.fill(0.f);
    worker = std::thread(&TunerProcessor::worker_main, this);
}

TunerProcessor::~TunerProcessor() {
    {
        std::lock_guard<std::mutex> lock(work_mutex);
        stop = true;
    }
    work_cv.notify_all();
    if (worker.joinable()) {
        worker.join();
    }
}

void TunerProcessor::worker_main() {
    std::unique_lock<std::mutex> lk(work_mutex);
    while (!stop) {
        work_cv.wait_for(lk, std::chrono::milliseconds(100));
        if (stop) {
            break;
        }
        if (!is_enabled()) {
            last_pitch.store(0.0f);
            continue;
        }
        lk.unlock();
        last_pitch.store(detect_pitch());
        lk.lock();
    }
}

void TunerProcessor::reset() {
    // May run on the audio thread (chain reorder) — never block. If the
    // worker is mid-snapshot, the ring keeps its stale contents; it is
    // overwritten within a few blocks anyway.
    if (buf_mutex.try_lock()) {
        ring_buf.fill(0.f);
        write_pos = 0;
        buffer_ready = false;
        buf_mutex.unlock();
    }
}

void TunerProcessor::process_block_mono(float* buf, size_t nframes) {
    // try_lock: if the GUI thread is currently snapshotting, skip the write for this
    // block. A ~5 ms gap in ring-buffer data is completely unnoticeable in a tuner display.
    if (buf_mutex.try_lock()) {
        for (size_t i = 0; i < nframes; ++i) {
            ring_buf[write_pos] = buf[i];
            write_pos = (write_pos + 1) % BUF_SIZE;
        }
        buffer_ready = true;
        buf_mutex.unlock();
    }

    if (mute.load()) {
        std::fill(buf, buf + nframes, 0.0f);
    }
}

void TunerProcessor::process_block_dual(float* l, float* r, size_t nframes) {
    if (buf_mutex.try_lock()) {
        for (size_t i = 0; i < nframes; ++i) {
            ring_buf[write_pos] = 0.5f * (l[i] + r[i]);
            write_pos = (write_pos + 1) % BUF_SIZE;
        }
        buffer_ready = true;
        buf_mutex.unlock();
    }

    if (mute.load()) {
        std::fill(l, l + nframes, 0.0f);
        std::fill(r, r + nframes, 0.0f);
    }
}

float TunerProcessor::detect_pitch() {
    {
        std::lock_guard<std::mutex> lock(buf_mutex);
        if (!buffer_ready) {
            return 0.0f;
        }

        for (size_t i = 0; i < BUF_SIZE; ++i) {
            snap_buf[i] = ring_buf[(write_pos + i) % BUF_SIZE];
        }
    }

    double rms_sum = 0.0;
    for (const float sample : snap_buf) {
        rms_sum += sample * sample;
    }
    if (std::sqrt(rms_sum / static_cast<double>(BUF_SIZE)) < 5e-4) {
        return 0.0f;
    }

    const float current_fs = fs.load();
    const size_t win_len = std::min<size_t>(BUF_SIZE, 1024);
    const size_t min_lag =
        std::max<size_t>(2, static_cast<size_t>(current_fs / 1400.0f)); // ~1400 Hz ceiling
    const size_t max_lag =
        std::min(static_cast<size_t>(current_fs / 50.0f), BUF_SIZE - win_len - 1); // ~50 Hz floor
    if (max_lag <= min_lag) {
        return 0.0f;
    }

    // NSDF (McLeod Pitch Method): n(τ) = 2·r(τ) / (m0 + m(τ)) over a fixed
    // 1024-sample window, evaluated for every lag from 1 so the first
    // positive lobe (around τ = 0) can be skipped explicitly below.
    double r0 = 0.0;
    for (size_t i = 0; i < win_len; ++i) {
        r0 += snap_buf[i] * snap_buf[i];
    }

    nsdf.assign(max_lag + 2, 0.0f);
    nsdf[0] = 1.0f;
    for (size_t lag = 1; lag <= max_lag + 1; ++lag) {
        double r = 0.0, mk = 0.0;
        for (size_t i = 0; i < win_len; ++i) {
            r += snap_buf[i] * snap_buf[i + lag];
            mk += snap_buf[i + lag] * snap_buf[i + lag];
        }
        const double denom = r0 + mk;
        nsdf[lag] = (denom > 1e-12) ? static_cast<float>(2.0 * r / denom) : 0.0f;
    }

    // Key-maxima picking. The global NSDF maximum is unreliable: for a
    // periodic signal every multiple of the period scores ~1.0, so the
    // global max regularly lands on 2T, 3T… (an octave or more too low).
    // MPM instead takes the highest point of each positive lobe after the
    // first negative crossing, then picks the FIRST such peak that is within
    // a fraction k of the tallest — that is the fundamental period.
    constexpr float KEY_MAX_RATIO = 0.8f;

    size_t lag = 1;
    while (lag <= max_lag && nsdf[lag] > 0.0f) {
        ++lag; // skip the τ≈0 lobe
    }

    float best_val = -1.0f;
    size_t pick_lag = 0;
    float pick_val = -1.0f;
    struct Key
    {
        size_t lag;
        float val;
    };
    Key keys[64];
    size_t n_keys = 0;

    while (lag <= max_lag && n_keys < 64) {
        while (lag <= max_lag && nsdf[lag] <= 0.0f) {
            ++lag;
        }
        if (lag > max_lag) {
            break;
        }
        size_t peak_lag = lag;
        float peak_val = nsdf[lag];
        while (lag <= max_lag && nsdf[lag] > 0.0f) {
            if (nsdf[lag] > peak_val) {
                peak_val = nsdf[lag];
                peak_lag = lag;
            }
            ++lag;
        }
        if (peak_lag < min_lag) {
            continue;
        }
        keys[n_keys++] = {peak_lag, peak_val};
        best_val = std::max(best_val, peak_val);
    }

    if (n_keys == 0 || best_val < 0.40f) {
        return 0.0f;
    }

    const float threshold = KEY_MAX_RATIO * best_val;
    for (size_t i = 0; i < n_keys; ++i) {
        if (keys[i].val >= threshold) {
            pick_lag = keys[i].lag;
            pick_val = keys[i].val;
            break;
        }
    }
    if (pick_lag == 0) {
        return 0.0f;
    }

    // Parabolic interpolation through (lag-1, lag, lag+1). With
    // y = a·x² + b·x + c fitted at x = -1, 0, +1 the vertex sits at
    // x = (y2 - y0) / (2·(2·y1 - y0 - y2)).
    if (pick_lag >= 1 && pick_lag + 1 <= max_lag + 1) {
        const float y0 = nsdf[pick_lag - 1];
        const float y1 = pick_val;
        const float y2 = nsdf[pick_lag + 1];
        const float denom = 2.0f * (2.0f * y1 - y0 - y2);
        if (std::abs(denom) > 1e-6f) {
            const float delta = std::max(-1.0f, std::min(1.0f, (y2 - y0) / denom));
            return current_fs / (static_cast<float>(pick_lag) + delta);
        }
    }

    return current_fs / static_cast<float>(pick_lag);
}

int TunerProcessor::freq_to_midi(float freq, float ref_hz) {
    if (freq <= 0.0f) {
        return -1;
    }
    return static_cast<int>(std::round(69.0f + 12.0f * std::log2(freq / ref_hz)));
}

std::string TunerProcessor::midi_to_name(int midi_note) {
    if (midi_note < 0 || midi_note > 127) {
        return "--";
    }
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = midi_note / 12 - 1;
    return std::string(names[midi_note % 12]) + std::to_string(octave);
}

float TunerProcessor::cents_offset(float freq, float ref_hz) {
    if (freq <= 0.0f) {
        return 0.0f;
    }
    int midi = freq_to_midi(freq, ref_hz);
    float note_freq = ref_hz * std::pow(2.0f, static_cast<float>(midi - 69) / 12.0f);
    return std::max(-50.0f, std::min(50.0f, 1200.0f * std::log2(freq / note_freq)));
}

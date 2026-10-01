#pragma once

#include <QString>
#include <QWidget>

class AudioEngine;
class QLabel;

/// VS Code-style status items for the bottom-right of the window: backend
/// connection state, sample rate, buffer size, latency (round trip when the
/// backend reports it), DSP load with max-hold, xruns, and the input/output
/// device pickers (click → device menu). Owns device enumeration and the
/// QSettings persistence of the chosen device IDs (audio/inputDeviceId,
/// audio/outputDeviceId) — the successor of the old dropdown row.
class AudioStatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit AudioStatusBar(AudioEngine& engine, QWidget* parent = nullptr);

    /// Re-read backend state + device lists. Call after audio (re)starts.
    void refresh();
    /// Show the not-connected state; the backend item becomes click-to-retry.
    void set_error(const QString& text);
    /// Keep rate/buffer/latency current (cheap; called from the meter timer —
    /// PipeWire can change the quantum while running).
    void update_metrics();

    QString input_device_id() const {
        return m_input_id;
    }
    QString output_device_id() const {
        return m_output_id;
    }

signals:
    /// A device was picked from a menu and persisted — owner restarts audio.
    void device_selection_changed();
    /// The backend item was clicked while disconnected.
    void reconnect_requested();

private:
    void pick_device(bool input);
    void update_dsp_load();
    void update_xruns();
    void reset_counters();
    void set_device_labels();

    AudioEngine& m_engine;
    QLabel* m_lbl_backend = nullptr;
    QLabel* m_lbl_rate = nullptr;
    QLabel* m_lbl_buffer = nullptr;
    QLabel* m_lbl_latency = nullptr;
    QLabel* m_lbl_load = nullptr;
    QLabel* m_lbl_xruns = nullptr;
    QLabel* m_lbl_input = nullptr;
    QLabel* m_lbl_output = nullptr;
    QString m_input_id, m_output_id;
    bool m_connected = false;

    // DSP load: peaks taken every metrics tick, shown as the max over a
    // ~0.5 s window (readable, still catches spikes); the all-time max since
    // the last reset goes in the tooltip.
    float m_load_window = 0.0f;
    int m_load_ticks = 0;
    float m_load_max = 0.0f;
    int m_shown_xruns = -2;
};

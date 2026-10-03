#pragma once

#include <QString>
#include <QWidget>

#include "Common.h"

class QLabel;

namespace namx
{

class AudioEngine;

// VS Code-style status items for the bottom-right of the window: backend
// connection state, sample rate, buffer size, latency (round trip when the
// backend reports it), DSP load with max-hold, xruns, and the input/output
// device pickers (click → device menu). Owns device enumeration and the
// QSettings persistence of the chosen device IDs (audio/inputDeviceId,
// audio/outputDeviceId) — the successor of the old dropdown row.
class AudioStatusBar : public QWidget
{
    Q_OBJECT
public:
    explicit AudioStatusBar(AudioEngine& new_engine, QWidget* parent = nullptr);

    // Re-read backend state + device lists. Call after audio (re)starts.
    void refresh();
    // Show the not-connected state; the backend item becomes click-to-retry.
    void set_error(const QString& text);
    // Keep rate/buffer/latency current (cheap; called from the meter timer —
    // PipeWire can change the quantum while running).
    void update_metrics();

    QString input_device_id() const {
        return input_id;
    }
    QString output_device_id() const {
        return output_id;
    }

    // Selects and persists a device (the status bar menus and the settings dialog).
    void set_device(bool input, const QString& id);
    void reset_counters();

signals:
    // A device was picked and persisted — owner restarts audio.
    void device_selection_changed();
    // The backend item was clicked while disconnected.
    void reconnect_requested();

private:
    void pick_device(bool input);
    void update_dsp_load();
    void update_xruns();
    void set_device_labels();

    AudioEngine& engine;
    QLabel* lbl_backend = nullptr;
    QLabel* lbl_rate = nullptr;
    QLabel* lbl_buffer = nullptr;
    QLabel* lbl_latency = nullptr;
    QLabel* lbl_load = nullptr;
    QLabel* lbl_xruns = nullptr;
    QLabel* lbl_input = nullptr;
    QLabel* lbl_output = nullptr;
    QString input_id, output_id;
    bool connected = false;

    // DSP load: peaks taken every metrics tick, shown as the max over a
    // ~0.5 s window (readable, still catches spikes); the all-time max since
    // the last reset goes in the tooltip.
    float load_window = 0.0f;
    int load_ticks = 0;
    float load_max = 0.0f;
    int shown_xruns = -2;
};

} // namespace namx

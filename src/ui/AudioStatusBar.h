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
    void setError(const QString& text);
    /// Keep rate/buffer/latency current (cheap; called from the meter timer —
    /// PipeWire can change the quantum while running).
    void updateMetrics();

    QString inputDeviceId()  const { return mInputId; }
    QString outputDeviceId() const { return mOutputId; }

signals:
    /// A device was picked from a menu and persisted — owner restarts audio.
    void deviceSelectionChanged();
    /// The backend item was clicked while disconnected.
    void reconnectRequested();

private:
    void pickDevice(bool input);
    void updateDspLoad();
    void updateXruns();
    void resetCounters();
    void setDeviceLabels();

    AudioEngine& mEngine;
    QLabel* mLblBackend = nullptr;
    QLabel* mLblRate    = nullptr;
    QLabel* mLblBuffer  = nullptr;
    QLabel* mLblLatency = nullptr;
    QLabel* mLblLoad    = nullptr;
    QLabel* mLblXruns   = nullptr;
    QLabel* mLblInput   = nullptr;
    QLabel* mLblOutput  = nullptr;
    QString mInputId, mOutputId;
    bool mConnected = false;

    // DSP load: peaks taken every metrics tick, shown as the max over a
    // ~0.5 s window (readable, still catches spikes); the all-time max since
    // the last reset goes in the tooltip.
    float mLoadWindow = 0.0f;
    int   mLoadTicks  = 0;
    float mLoadMax    = 0.0f;
    int   mShownXruns = -2;
};

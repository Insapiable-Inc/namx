#pragma once

#include <QString>
#include <QWidget>

class QComboBox;
class AudioEngine;

/// The input/output device selector row. Owns device enumeration, the
/// dropdowns, and QSettings persistence of the chosen device IDs
/// (audio/inputDeviceId, audio/outputDeviceId). On JACK-style backends with
/// a single "Default" entry the dropdowns are disabled.
class AudioDevicePanel : public QWidget
{
    Q_OBJECT
public:
    explicit AudioDevicePanel(AudioEngine& engine, QWidget* parent = nullptr);

    /// (Re)fill the dropdowns from the backend and restore the saved selection.
    void refresh();

    QString inputDeviceId() const;
    QString outputDeviceId() const;

signals:
    /// A user selection was made and persisted — the owner restarts audio.
    void deviceSelectionChanged();

private:
    void onSelectionChanged();

    AudioEngine& mEngine;
    QComboBox* mCbxInput  = nullptr;
    QComboBox* mCbxOutput = nullptr;
    bool       mChanging  = false;
};

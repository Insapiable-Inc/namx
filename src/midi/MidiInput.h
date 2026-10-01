#pragma once

#include <memory>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "MidiMessage.h"

/// Platform MIDI input (Phase 16): ALSA sequencer on Linux (USB, DIN through
/// an interface, and whatever PipeWire/JACK bridge into it), WinMM on
/// Windows. A backend thread receives events and posts them to the GUI
/// thread as queued `messageReceived` signals — MIDI never touches the
/// audio thread; the mapper applies it through the parameter registry.
///
/// Devices are identified by name (client numbers change on replug).
/// Every device is enabled unless the user disabled it (QSettings
/// midi/disabledDevices); hot-plugged devices connect automatically.
class MidiInput : public QObject
{
    Q_OBJECT
public:
    static std::unique_ptr<MidiInput> create(QObject* parent = nullptr);
    ~MidiInput() override = default;

    /// Open the backend and connect the enabled devices. False when MIDI is
    /// unavailable (no sequencer) — the app runs without it.
    virtual bool start() = 0;
    virtual QString backend_name() const = 0;

    /// Currently present source devices, sorted.
    virtual QStringList devices() const = 0;

    bool is_enabled(const QString& device) const {
        return !disabled.contains(device);
    }
    void set_enabled(const QString& device, bool on);

signals:
    void message_received(const MidiMessage& msg);
    /// A device appeared or went away (list changed).
    void devices_changed();

protected:
    explicit MidiInput(QObject* parent);
    /// Re-apply connections after the enabled set or the device list changed.
    virtual void sync_connections() = 0;

    QSet<QString> disabled;
};

#include "MidiInput.h"

#include <QSettings>

#include "Common.h"

namespace namx
{

MidiInput::MidiInput(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<MidiMessage>();
    const QStringList off = QSettings().value("midi/disabledDevices").toStringList();
    disabled = QSet<QString>(off.begin(), off.end());
}

void MidiInput::set_enabled(const QString& device, bool on) {
    if (on == is_enabled(device)) {
        return;
    }
    if (on) {
        disabled.remove(device);
    } else {
        disabled.insert(device);
    }
    QStringList off(disabled.begin(), disabled.end());
    off.sort();
    QSettings().setValue("midi/disabledDevices", off);
    sync_connections();
}

} // namespace namx

#include "MidiInput.h"

#include <QSettings>

MidiInput::MidiInput(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<MidiMessage>();
    const QStringList off = QSettings().value("midi/disabledDevices").toStringList();
    m_disabled = QSet<QString>(off.begin(), off.end());
}

void MidiInput::set_enabled(const QString& device, bool on) {
    if (on == is_enabled(device)) {
        return;
    }
    if (on) {
        m_disabled.remove(device);
    } else {
        m_disabled.insert(device);
    }
    QStringList off(m_disabled.begin(), m_disabled.end());
    off.sort();
    QSettings().setValue("midi/disabledDevices", off);
    sync_connections();
}

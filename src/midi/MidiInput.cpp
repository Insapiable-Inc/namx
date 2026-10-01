#include "MidiInput.h"

#include <QSettings>

MidiInput::MidiInput(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<MidiMessage>();
    const QStringList off = QSettings().value("midi/disabledDevices").toStringList();
    mDisabled = QSet<QString>(off.begin(), off.end());
}

void MidiInput::setEnabled(const QString& device, bool on) {
    if (on == isEnabled(device)) {
        return;
    }
    if (on) {
        mDisabled.remove(device);
    } else {
        mDisabled.insert(device);
    }
    QStringList off(mDisabled.begin(), mDisabled.end());
    off.sort();
    QSettings().setValue("midi/disabledDevices", off);
    syncConnections();
}

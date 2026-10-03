#include "EffectShortcuts.h"

#include <QKeySequence>

#include "Common.h"

namespace namx
{

// Digits, then the row below: keys that exist on every Latin keyboard layout.
static constexpr Qt::Key KEYS[] = {
    Qt::Key_1, Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5, Qt::Key_6, Qt::Key_7,
    Qt::Key_8, Qt::Key_9, Qt::Key_0, Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R,
    Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
};

// The effect a switch belongs to: the "displayName" of its card.
static QString card_name(const QWidget* w) {
    for (; w; w = w->parentWidget()) {
        const QVariant name = w->property("displayName");
        if (name.isValid()) {
            return name.toString();
        }
    }
    return "the effect";
}

void EffectShortcuts::add_effect(ToggleSwitch* enable) {
    if (!enable || next_key >= std::size(KEYS)) {
        return;
    }
    const QKeySequence key(KEYS[next_key++]);
    auto* shortcut = new QShortcut(key, window);
    shortcut->setWhatsThis(QString("Turn %1 on or off").arg(card_name(enable)));
    QObject::connect(shortcut, &QShortcut::activated, enable, &QAbstractButton::toggle);
    shortcuts.push_back(shortcut);
    enable->setToolTip(QString("Toggle effect  (%1)").arg(key.toString()));
}

void EffectShortcuts::clear() {
    for (QShortcut* s : shortcuts) {
        delete s;
    }
    shortcuts.clear();
    next_key = 0;
}

} // namespace namx

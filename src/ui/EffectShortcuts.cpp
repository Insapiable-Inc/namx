#include "EffectShortcuts.h"

#include <QKeySequence>
#include <QShortcut>

#include "Common.h"
#include "ToggleSwitch.h"

namespace npl
{

// Digit row of a Latin American (es-419) layout: after 0 come ' and ¿.
// Backspace closes the row for the last card (currently the IR toggle).
static constexpr Qt::Key ENABLE_KEYS[] = {
    Qt::Key_1,         Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,          Qt::Key_6,
    Qt::Key_7,         Qt::Key_8, Qt::Key_9, Qt::Key_0, Qt::Key_Apostrophe, Qt::Key_questiondown,
    Qt::Key_Backspace,
};

static constexpr Qt::Key STEREO_KEYS[] = {
    Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
    Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
};

constexpr int NUM_ENABLE_KEYS = sizeof(ENABLE_KEYS) / sizeof(ENABLE_KEYS[0]);
constexpr int NUM_STEREO_KEYS = sizeof(STEREO_KEYS) / sizeof(STEREO_KEYS[0]);

void EffectShortcuts::add_effect(ToggleSwitch* enable, ToggleSwitch* stereo) {
    if (enable && enable_index < NUM_ENABLE_KEYS) {
        bind(enable, ENABLE_KEYS[enable_index++], "Toggle effect", "Turn %1 on or off");
    }

    if (stereo && stereo_index < NUM_STEREO_KEYS) {
        bind(stereo, STEREO_KEYS[stereo_index++], "Toggle stereo", "%1: Stereo on or off");
    }
}

void EffectShortcuts::clear() {
    for (auto* s : shortcuts) {
        delete s;
    }
    shortcuts.clear();
    enable_index = 0;
    stereo_index = 0;
}

// The effect a toggle belongs to: the "displayName" of its card.
static QString card_name(const QWidget* w) {
    for (; w; w = w->parentWidget()) {
        const QVariant name = w->property("displayName");
        if (name.isValid()) {
            return name.toString();
        }
    }
    return "the effect";
}

void EffectShortcuts::bind(ToggleSwitch* toggle, int key, const char* action,
                           const char* description) {
    auto* shortcut = new QShortcut(QKeySequence(key), window);
    shortcut->setWhatsThis(QString(description).arg(card_name(toggle)));
    QObject::connect(shortcut, &QShortcut::activated, toggle, &QAbstractButton::toggle);
    shortcuts.push_back(shortcut);

    toggle->setToolTip(QString("%1  (%2)").arg(action, QKeySequence(key).toString()));
}

} // namespace npl

#include "EffectShortcuts.h"

#include <QKeySequence>
#include <QShortcut>

#include "ToggleSwitch.h"

namespace
{

// Digit row of a Latin American (es-419) layout: after 0 come ' and ¿.
// Backspace closes the row for the last card (currently the IR toggle).
constexpr Qt::Key K_ENABLE_KEYS[] = {
    Qt::Key_1,         Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,          Qt::Key_6,
    Qt::Key_7,         Qt::Key_8, Qt::Key_9, Qt::Key_0, Qt::Key_Apostrophe, Qt::Key_questiondown,
    Qt::Key_Backspace,
};

constexpr Qt::Key K_STEREO_KEYS[] = {
    Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
    Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
};

constexpr int K_NUM_ENABLE_KEYS = sizeof(K_ENABLE_KEYS) / sizeof(K_ENABLE_KEYS[0]);
constexpr int K_NUM_STEREO_KEYS = sizeof(K_STEREO_KEYS) / sizeof(K_STEREO_KEYS[0]);

} // namespace

void EffectShortcuts::add_effect(ToggleSwitch* enable, ToggleSwitch* stereo) {
    if (enable && m_enable_index < K_NUM_ENABLE_KEYS) {
        bind(enable, K_ENABLE_KEYS[m_enable_index++], "Toggle effect");
    }

    if (stereo && m_stereo_index < K_NUM_STEREO_KEYS) {
        bind(stereo, K_STEREO_KEYS[m_stereo_index++], "Toggle stereo");
    }
}

void EffectShortcuts::clear() {
    for (auto* s : m_shortcuts) {
        delete s;
    }
    m_shortcuts.clear();
    m_enable_index = 0;
    m_stereo_index = 0;
}

void EffectShortcuts::bind(ToggleSwitch* toggle, int key, const char* action) {
    auto* shortcut = new QShortcut(QKeySequence(key), m_window);
    QObject::connect(shortcut, &QShortcut::activated, toggle, &QAbstractButton::toggle);
    m_shortcuts.push_back(shortcut);

    toggle->setToolTip(QString("%1  (%2)").arg(action, QKeySequence(key).toString()));
}

#include "EffectShortcuts.h"

#include <QKeySequence>
#include <QShortcut>

#include "ToggleSwitch.h"

namespace
{

// Digit row of a Latin American (es-419) layout: after 0 come ' and ¿.
// Backspace closes the row for the last card (currently the IR toggle).
constexpr Qt::Key kEnableKeys[] = {
    Qt::Key_1,         Qt::Key_2, Qt::Key_3, Qt::Key_4, Qt::Key_5,          Qt::Key_6,
    Qt::Key_7,         Qt::Key_8, Qt::Key_9, Qt::Key_0, Qt::Key_Apostrophe, Qt::Key_questiondown,
    Qt::Key_Backspace,
};

constexpr Qt::Key kStereoKeys[] = {
    Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
    Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P,
};

constexpr int kNumEnableKeys = sizeof(kEnableKeys) / sizeof(kEnableKeys[0]);
constexpr int kNumStereoKeys = sizeof(kStereoKeys) / sizeof(kStereoKeys[0]);

} // namespace

void EffectShortcuts::addEffect(ToggleSwitch* enable, ToggleSwitch* stereo) {
    if (enable && mEnableIndex < kNumEnableKeys) {
        bind(enable, kEnableKeys[mEnableIndex++], "Toggle effect");
    }

    if (stereo && mStereoIndex < kNumStereoKeys) {
        bind(stereo, kStereoKeys[mStereoIndex++], "Toggle stereo");
    }
}

void EffectShortcuts::clear() {
    for (auto* s : mShortcuts) {
        delete s;
    }
    mShortcuts.clear();
    mEnableIndex = 0;
    mStereoIndex = 0;
}

void EffectShortcuts::bind(ToggleSwitch* toggle, int key, const char* action) {
    auto* shortcut = new QShortcut(QKeySequence(key), mWindow);
    QObject::connect(shortcut, &QShortcut::activated, toggle, &QAbstractButton::toggle);
    mShortcuts.push_back(shortcut);

    toggle->setToolTip(QString("%1  (%2)").arg(action, QKeySequence(key).toString()));
}

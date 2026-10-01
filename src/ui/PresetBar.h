#pragma once

#include <functional>

#include <QFrame>
#include <QHash>
#include <QString>
#include <QStringList>

#include "Common.h"

class QLabel;
class QMenu;
class QToolButton;

namespace npl
{

class LibraryIndex;

// The preset strip above the rack: ‹ [name ▾] › with a `*` while the state
// differs from the file, Save / Save As, quick slots 1–8, A/B compare and
// the full Preset menu behind ⋯.
//
// Display + intent only: the owner (MainWindow) loads, saves and compares
// state, then reports back through setPreset / setAbState. The quick slots
// are self-contained (QSettings `presets/slots`, Ctrl+1…8) and ask the
// owner to load through presetChosen like the name menu does.
class PresetBar : public QFrame
{
    Q_OBJECT
public:
    static constexpr int SLOT_COUNT = 8;

    PresetBar(const LibraryIndex& new_index, QMenu* preset_menu, QWidget* parent = nullptr);

    // Current preset file (empty = untitled) and whether the live state
    // differs from it.
    void set_preset(const QString& new_path, bool new_modified);
    // Active A/B side (0 = A, 1 = B).
    void set_ab_state(int active);

    // Load slot i's preset if it has one (MIDI; never stores).
    void recall_slot(int i);
    // Widget for an action id ("action:slot3", "action:presetNext", …), for
    // the MIDI Learn highlight.
    QWidget* action_widget(const QString& id) const {
        return action_widgets.value(id);
    }
    // Adds MIDI Learn entries to the context menus of the bar's buttons.
    void set_learn_menu_hook(std::function<void(const QString& action_id, QMenu* menu)> hook) {
        learn_hook = std::move(hook);
    }

signals:
    void preset_chosen(const QString& path); // name menu or a quick slot
    void step_requested(int step); // ‹ ›
    void save_requested();
    void save_as_requested();
    void ab_switch_requested(int side);
    void ab_copy_requested(); // active side → the other one

private:
    void populate_preset_menu(QMenu* menu);
    void activate_slot(int i);
    void show_slot_menu(int i, const QPoint& global_pos);
    void update_slots();

    const LibraryIndex& library_index;
    QString path;
    QToolButton* name = nullptr;
    QLabel* modified = nullptr;
    QToolButton* quick_slots[SLOT_COUNT]{};
    QStringList slot_paths; // SLOT_COUNT entries, "" = empty slot
    QToolButton* ab[2]{};
    QToolButton* ab_copy = nullptr;
    int ab_active = 0;
    QHash<QString, QWidget*> action_widgets;
    std::function<void(const QString&, QMenu*)> learn_hook;
    void learn_menu_for(QWidget* w, const QString& action_id);
    void show_learn_menu(const QString& action_id, const QPoint& global_pos);
    QToolButton* make_tool_button(const QString& text, const QString& tip, const char* object_name);
    void on_ab_clicked(int side);
};

} // namespace npl

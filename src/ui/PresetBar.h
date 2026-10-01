#pragma once

#include <functional>

#include <QFrame>
#include <QHash>
#include <QString>
#include <QStringList>

class LibraryIndex;
class QLabel;
class QMenu;
class QToolButton;

/// The preset strip above the rack: ‹ [name ▾] › with a `*` while the state
/// differs from the file, Save / Save As, quick slots 1–8, A/B compare and
/// the full Preset menu behind ⋯.
///
/// Display + intent only: the owner (MainWindow) loads, saves and compares
/// state, then reports back through setPreset / setAbState. The quick slots
/// are self-contained (QSettings `presets/slots`, Ctrl+1…8) and ask the
/// owner to load through presetChosen like the name menu does.
class PresetBar : public QFrame
{
    Q_OBJECT
public:
    static constexpr int K_SLOT_COUNT = 8;

    PresetBar(const LibraryIndex& new_index, QMenu* preset_menu, QWidget* parent = nullptr);

    /// Current preset file (empty = untitled) and whether the live state
    /// differs from it.
    void set_preset(const QString& new_path, bool new_modified);
    /// Active A/B side (0 = A, 1 = B).
    void set_ab_state(int active);

    /// Load slot i's preset if it has one (MIDI; never stores).
    void recall_slot(int i);
    /// Widget for an action id ("action:slot3", "action:presetNext", …), for
    /// the MIDI Learn highlight.
    QWidget* action_widget(const QString& id) const {
        return m_action_widgets.value(id);
    }
    /// Adds MIDI Learn entries to the context menus of the bar's buttons.
    void set_learn_menu_hook(std::function<void(const QString& action_id, QMenu* menu)> hook) {
        m_learn_hook = std::move(hook);
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

    const LibraryIndex& m_library_index;
    QString m_path;
    QToolButton* m_name = nullptr;
    QLabel* m_modified = nullptr;
    QToolButton* m_quick_slots[K_SLOT_COUNT]{};
    QStringList m_slot_paths; // kSlotCount entries, "" = empty slot
    QToolButton* m_ab[2]{};
    QToolButton* m_ab_copy = nullptr;
    int m_ab_active = 0;
    QHash<QString, QWidget*> m_action_widgets;
    std::function<void(const QString&, QMenu*)> m_learn_hook;
    void learn_menu_for(QWidget* w, const QString& action_id);
};

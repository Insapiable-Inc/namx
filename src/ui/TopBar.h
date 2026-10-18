#pragma once

#include <QFrame>
#include <QList>
#include <QString>

#include "Common.h"

class QCompleter;
class QLabel;
class QLineEdit;
class QMenu;
class QStandardItemModel;
class QToolButton;

namespace namx
{

class Metronome;
class NumberField;
class TempoBar;

// The bar across the top of the window (ui/node-graph): logo, the preset's name
// (click for the preset menu, ✎ to rename), the tuner, the metronome with its tempo, a search
// over everything that can be added or loaded, the favourite star, the main menu and
// Settings.
class TopBar : public QFrame
{
    Q_OBJECT

public:
    // One search result: what it is (shown dim) and what picking it means.
    struct SearchEntry
    {
        enum class Kind
        {
            NODE, // payload = type ID; stereo variant flag
            PRESET, // payload = path
            MODEL,
            IR
        };
        Kind kind;
        QString text;
        QString payload;
        bool stereo = false;
    };

    explicit TopBar(Metronome& metronome, QWidget* parent = nullptr);

    void set_preset(const QString& name, bool modified, bool untitled);
    void set_favorite(bool on);
    void set_tuner(bool on);
    void set_search_entries(const QList<SearchEntry>& entries);
    // The metronome popup's tempo bar (MIDI clock follow, beat LEDs).
    TempoBar* tempo() const {
        return tempo_bar;
    }
    // At display rate: beat LEDs and the play button's state.
    void poll();
    void focus_search();

    QMenu* preset_menu() const {
        return presets;
    }
    QMenu* main_menu() const {
        return main;
    }

signals:
    void preset_menu_about_to_show();
    void rename_requested();
    void favorite_toggled(bool on);
    void tuner_toggled(bool on);
    void settings_requested();
    void search_picked(const TopBar::SearchEntry& entry);

private:
    QToolButton* tool(const QString& text, const QString& tip);

    Metronome& metro;
    QToolButton* preset_button = nullptr;
    QMenu* presets = nullptr;
    QMenu* main = nullptr;
    QToolButton* metro_button = nullptr;
    NumberField* bpm = nullptr;
    QFrame* tempo_popup = nullptr;
    TempoBar* tempo_bar = nullptr;
    QLineEdit* search = nullptr;
    QCompleter* completer = nullptr;
    QStandardItemModel* search_model = nullptr;
    QList<SearchEntry> search_entries;
    QToolButton* star = nullptr;
    QToolButton* tuner_button = nullptr;
};

} // namespace namx

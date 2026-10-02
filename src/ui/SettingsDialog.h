#pragma once

#include <functional>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include "AudioEngine.h"
#include "AudioStatusBar.h"
#include "Common.h"
#include "LibraryIndex.h"
#include "MidiInput.h"
#include "MidiMapper.h"
#include "NumberField.h"

namespace npl
{

// What the settings dialog edits. Each page drives the same objects the rest of the
// window uses, so a change shows up everywhere at once.
struct SettingsTargets
{
    AudioEngine& engine;
    AudioStatusBar& status_bar; // owns the device selection
    MidiInput* midi = nullptr; // null when MIDI input is unavailable
    MidiMapper& midi_mapper;
    LibraryIndex& library_index;
    NumberField& tuner_reference; // the tuner card's own controls
    QToolButton& tuner_show_hz;
    QMainWindow& window; // its menus and shortcuts fill the Shortcuts page
    std::function<void()> open_midi_mappings;
};

// Settings window: one page per area, every change applied immediately (no OK/Cancel).
// Non-modal; MainWindow keeps one and shows it on the page asked for.
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Page
    {
        AUDIO,
        MIDI,
        LIBRARY,
        TUNER,
        SHORTCUTS,
    };

    SettingsDialog(SettingsTargets new_targets, QWidget* parent);

    void show_page(Page page);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QWidget* build_audio_page();
    QWidget* build_midi_page();
    QWidget* build_library_page();
    QWidget* build_tuner_page();
    QWidget* build_shortcuts_page();

    void refresh_current_page();
    void refresh_audio_devices();
    void refresh_audio_info();
    void refresh_midi_devices();
    void refresh_library();
    void refresh_shortcuts();

    void choose_device(bool input, int index);
    void add_folder(size_t section);
    void remove_folder(size_t section);
    void apply_folders();
    void choose_tone3000_dir();

    SettingsTargets targets;
    QListWidget* page_list = nullptr;
    QStackedWidget* pages = nullptr;
    QTimer* audio_timer = nullptr; // live rate/latency/xruns while the Audio page shows

    QComboBox* input_combo = nullptr;
    QComboBox* output_combo = nullptr;
    QLabel* server_label = nullptr;
    QLabel* rate_label = nullptr;
    QLabel* buffer_label = nullptr;
    QLabel* latency_label = nullptr;
    QLabel* xruns_label = nullptr;

    QListWidget* midi_devices = nullptr;
    QCheckBox* follow_clock = nullptr;

    array<QListWidget*, 3> folder_lists{}; // models, IRs, presets
    QLabel* tone3000_dir = nullptr;

    QSpinBox* reference_box = nullptr;
    QCheckBox* show_hz_box = nullptr;

    QTableWidget* shortcuts = nullptr;
};

} // namespace npl

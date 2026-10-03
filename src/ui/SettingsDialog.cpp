#include "SettingsDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

#include "Common.h"

namespace namx
{

static const char* const FOLDER_SECTIONS[3] = {"Models", "Impulse responses", "Presets"};

// A page: optional explanation on top, then the content, pinned to the top unless it
// `fills` the page (a list or table that should grow).
static QWidget* make_page(const QString& hint, QLayout* content, bool fills = false) {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 12, 16, 12);
    if (!hint.isEmpty()) {
        auto* label = new QLabel(hint);
        label->setWordWrap(true);
        label->setObjectName("settingsHint");
        lay->addWidget(label);
    }
    lay->addLayout(content, fills ? 1 : 0);
    if (!fills) {
        lay->addStretch(1);
    }
    return page;
}

// A form whose fields keep their natural width instead of spanning the page.
static QFormLayout* make_form() {
    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignLeft);
    return form;
}

static QString format_ms(double frames, double rate) {
    return QString("%1 ms").arg(frames / rate * 1000.0, 0, 'f', 1);
}

// Menu text without the accelerator marker ("&Save" → "Save").
static QString plain_text(const QString& text) {
    QString t = text;
    return t.remove('&');
}

// Every action with a shortcut in `menu` and its submenus, labelled "Menu → Item".
static void collect_menu_shortcuts(QMenu* menu, const QString& path,
                                   vector<std::pair<QKeySequence, QString>>& out) {
    for (QAction* action : menu->actions()) {
        if (QMenu* sub = action->menu()) {
            collect_menu_shortcuts(sub, path + " → " + plain_text(sub->title()), out);
        } else if (!action->shortcut().isEmpty()) {
            out.emplace_back(action->shortcut(), path + " → " + plain_text(action->text()));
        }
    }
}

SettingsDialog::SettingsDialog(SettingsTargets new_targets, QWidget* parent)
    : QDialog(parent)
    , targets(std::move(new_targets)) {
    setWindowTitle("Settings");
    resize(720, 480);

    page_list = new QListWidget;
    page_list->setFixedWidth(150);
    pages = new QStackedWidget;
    const std::pair<const char*, QWidget*> all[] = {
        {"Audio", build_audio_page()},         {"MIDI", build_midi_page()},
        {"Library", build_library_page()},     {"Tuner", build_tuner_page()},
        {"Shortcuts", build_shortcuts_page()},
    };
    for (const auto& [name, page] : all) {
        page_list->addItem(name);
        pages->addWidget(page);
    }
    connect(page_list, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    connect(page_list, &QListWidget::currentRowChanged, this,
            &SettingsDialog::refresh_current_page);

    auto* close = new QPushButton("Close");
    connect(close, &QPushButton::clicked, this, &QDialog::close);

    auto* body = new QHBoxLayout;
    body->addWidget(page_list);
    body->addWidget(pages, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);
    auto* lay = new QVBoxLayout(this);
    lay->addLayout(body, 1);
    lay->addLayout(buttons);

    audio_timer = new QTimer(this);
    audio_timer->setInterval(500);
    connect(audio_timer, &QTimer::timeout, this, &SettingsDialog::refresh_audio_info);
    connect(&targets.status_bar, &AudioStatusBar::device_selection_changed, this,
            &SettingsDialog::refresh_audio_devices);

    page_list->setCurrentRow(0);
}

void SettingsDialog::show_page(Page page) {
    page_list->setCurrentRow(static_cast<int>(page));
    show();
    raise();
    activateWindow();
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    refresh_current_page();
    audio_timer->start();
}

void SettingsDialog::hideEvent(QHideEvent* event) {
    audio_timer->stop();
    QDialog::hideEvent(event);
}

// Pages reflect state that changes elsewhere (devices plugged in, folders edited from
// the library, shortcuts reassigned by a chain reorder), so they reload when shown.
void SettingsDialog::refresh_current_page() {
    switch (static_cast<Page>(pages->currentIndex())) {
        case Page::AUDIO:
            refresh_audio_devices();
            refresh_audio_info();
            if (exclusive_box) {
                const QSignalBlocker block(exclusive_box);
                exclusive_box->setChecked(targets.engine.is_exclusive_wanted());
            }
            break;
        case Page::MIDI:
            refresh_midi_devices();
            break;
        case Page::LIBRARY:
            refresh_library();
            break;
        case Page::TUNER:
            reference_box->setValue(qRound(targets.tuner_reference.value()));
            show_hz_box->setChecked(targets.tuner_display.get_show_hz());
            break;
        case Page::SHORTCUTS:
            refresh_shortcuts();
            break;
    }
}

// Audio

QWidget* SettingsDialog::build_audio_page() {
    input_combo = new QComboBox;
    output_combo = new QComboBox;
    input_combo->setMinimumWidth(320);
    output_combo->setMinimumWidth(320);
    connect(input_combo, &QComboBox::activated, this, [this](int index) {
        choose_device(true, index);
    });
    connect(output_combo, &QComboBox::activated, this, [this](int index) {
        choose_device(false, index);
    });

    server_label = new QLabel;
    rate_label = new QLabel;
    buffer_label = new QLabel;
    latency_label = new QLabel;
    xruns_label = new QLabel;
    auto* reset = new QPushButton("Reset");
    connect(reset, &QPushButton::clicked, &targets.status_bar, &AudioStatusBar::reset_counters);
    auto* xruns = new QHBoxLayout;
    xruns->addWidget(xruns_label);
    xruns->addSpacing(12);
    xruns->addWidget(reset);
    xruns->addStretch();

    QFormLayout* form = make_form();
    form->addRow("Input", input_combo);
    form->addRow("Output", output_combo);
    form->addRow("Server", server_label);
    form->addRow("Sample rate", rate_label);
    form->addRow("Buffer", buffer_label);
    form->addRow("Round-trip latency", latency_label);
    form->addRow("Xruns", xruns);
    if (targets.engine.supports_exclusive_mode()) {
        exclusive_box = new QCheckBox("Exclusive mode");
        exclusive_box->setToolTip(
            "Take the devices over: the lowest latency WASAPI offers, but no other app can "
            "play or record through them while NAM-X runs. Falls back to shared mode "
            "when a device refuses.");
        connect(exclusive_box, &QCheckBox::toggled, this, [this](bool on) {
            QSettings().setValue("audio/exclusive", on);
            targets.engine.set_exclusive_mode(on);
            if (targets.restart_audio) {
                targets.restart_audio();
            }
            refresh_audio_info();
        });
        form->addRow("", exclusive_box);
    }
    return make_page("The same choice as the In: / Out: items in the status bar. Sample rate "
                     "and buffer size are set by the audio server (PipeWire or JACK) and "
                     "shared by every application using it.",
                     form);
}

void SettingsDialog::refresh_audio_devices() {
    const std::pair<QComboBox*, bool> combos[] = {{input_combo, true}, {output_combo, false}};
    for (const auto& [combo, input] : combos) {
        const vector<IAudioBackend::DeviceInfo> devices =
            input ? targets.engine.enumerate_input_devices()
                  : targets.engine.enumerate_output_devices();
        const QString current =
            input ? targets.status_bar.input_device_id() : targets.status_bar.output_device_id();
        const QSignalBlocker block(combo);
        combo->clear();
        for (const IAudioBackend::DeviceInfo& d : devices) {
            combo->addItem(QString::fromStdString(d.name), QString::fromStdString(d.id));
        }
        combo->setEnabled(!devices.empty());
        const int index = combo->findData(current);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

void SettingsDialog::refresh_audio_info() {
    // The backend's one-line status reads "JACK  ●  48000 Hz  ●  stereo out".
    const QString status = QString::fromStdString(targets.engine.backend_status());
    const bool running = targets.engine.is_running();
    server_label->setText(running ? status.section("●", 0, 0).trimmed()
                                  : QString("Not connected %1").arg(status));
    const double rate = targets.engine.sample_rate();
    const int buffer = targets.engine.buffer_size();
    const int round_trip = targets.engine.io_latency_frames();
    rate_label->setText(running ? QString("%1 Hz").arg(qRound(rate)) : "—");
    buffer_label->setText(running && rate > 0.0
                              ? QString("%1 frames (%2)").arg(buffer).arg(format_ms(buffer, rate))
                              : "—");
    latency_label->setText(running && round_trip > 0 ? format_ms(round_trip, rate)
                                                     : "not reported by the server");
    xruns_label->setText(QString::number(targets.engine.xrun_count()));
}

void SettingsDialog::choose_device(bool input, int index) {
    QComboBox* combo = input ? input_combo : output_combo;
    targets.status_bar.set_device(input, combo->itemData(index).toString());
}

// MIDI

QWidget* SettingsDialog::build_midi_page() {
    midi_devices = new QListWidget;
    connect(midi_devices, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        targets.midi->set_enabled(item->text(), item->checkState() == Qt::Checked);
    });

    follow_clock = new QCheckBox("Follow MIDI clock (tempo from an external device)");
    follow_clock->setChecked(targets.midi_mapper.get_follow_clock());
    connect(follow_clock, &QCheckBox::toggled, &targets.midi_mapper, &MidiMapper::set_follow_clock);

    auto* mappings = new QPushButton("Mappings…");
    connect(mappings, &QPushButton::clicked, this, [this] {
        targets.open_midi_mappings();
    });

    auto* lay = new QVBoxLayout;
    lay->addWidget(new QLabel("Input devices"));
    lay->addWidget(midi_devices, 1);
    lay->addWidget(follow_clock);
    auto* row = new QHBoxLayout;
    row->addWidget(mappings);
    row->addStretch();
    lay->addLayout(row);
    return make_page("Unchecked devices are ignored. Devices plugged in later appear here "
                     "and connect automatically.",
                     lay, true);
}

void SettingsDialog::refresh_midi_devices() {
    const QSignalBlocker block(midi_devices);
    midi_devices->clear();
    follow_clock->setChecked(targets.midi_mapper.get_follow_clock());
    if (!targets.midi) {
        midi_devices->addItem("MIDI input is not available");
        midi_devices->setEnabled(false);
        return;
    }
    const QStringList devices = targets.midi->devices();
    if (devices.isEmpty()) {
        midi_devices->addItem("No MIDI devices found");
        midi_devices->setEnabled(false);
        return;
    }
    midi_devices->setEnabled(true);
    for (const QString& d : devices) {
        auto* item = new QListWidgetItem(d, midi_devices);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(targets.midi->is_enabled(d) ? Qt::Checked : Qt::Unchecked);
    }
}

// Library

QWidget* SettingsDialog::build_library_page() {
    auto* lay = new QVBoxLayout;
    for (size_t section = 0; section < folder_lists.size(); ++section) {
        folder_lists[section] = new QListWidget;
        folder_lists[section]->setMaximumHeight(90);
        auto* add = new QPushButton("Add…");
        auto* remove = new QPushButton("Remove");
        connect(add, &QPushButton::clicked, this, [this, section] {
            add_folder(section);
        });
        connect(remove, &QPushButton::clicked, this, [this, section] {
            remove_folder(section);
        });
        auto* buttons = new QVBoxLayout;
        buttons->addWidget(add);
        buttons->addWidget(remove);
        buttons->addStretch();
        auto* row = new QHBoxLayout;
        row->addWidget(folder_lists[section], 1);
        row->addLayout(buttons);
        lay->addWidget(new QLabel(FOLDER_SECTIONS[section]));
        lay->addLayout(row);
    }

    auto* rescan = new QPushButton("Rescan library");
    connect(rescan, &QPushButton::clicked, &targets.library_index, &LibraryIndex::rescan);

    auto* last = new QHBoxLayout;
    last->addStretch();
    last->addWidget(rescan);
    lay->addLayout(last);
    return make_page("Each folder is scanned recursively and followed for changes. A folder "
                     "can serve several kinds.",
                     lay);
}

void SettingsDialog::refresh_library() {
    const LibraryRoots& roots = targets.library_index.get_roots();
    const QStringList* lists[3] = {&roots.models, &roots.irs, &roots.presets};
    for (size_t section = 0; section < folder_lists.size(); ++section) {
        folder_lists[section]->clear();
        folder_lists[section]->addItems(*lists[section]);
    }
}

void SettingsDialog::add_folder(size_t section) {
    QListWidget* list = folder_lists[section];
    const QString start = list->count() ? list->item(list->count() - 1)->text() : QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QString("Add a folder: %1").arg(FOLDER_SECTIONS[section]), start);
    if (dir.isEmpty()) {
        return;
    }
    const QString clean = QDir::cleanPath(dir);
    if (list->findItems(clean, Qt::MatchExactly).isEmpty()) {
        list->addItem(clean);
        apply_folders();
    }
}

void SettingsDialog::remove_folder(size_t section) {
    delete folder_lists[section]->currentItem();
    apply_folders();
}

// Persists the lists and rescans (LibraryIndex::set_roots).
void SettingsDialog::apply_folders() {
    QStringList lists[3];
    for (size_t section = 0; section < folder_lists.size(); ++section) {
        for (int row = 0; row < folder_lists[section]->count(); ++row) {
            lists[section] << folder_lists[section]->item(row)->text();
        }
    }
    LibraryRoots roots;
    roots.models = lists[0];
    roots.irs = lists[1];
    roots.presets = lists[2];
    targets.library_index.set_roots(roots);
}

// Tuner: drives the tuner card's own controls, which persist the values.

QWidget* SettingsDialog::build_tuner_page() {
    reference_box = new QSpinBox;
    reference_box->setRange(qRound(targets.tuner_reference.minimum()),
                            qRound(targets.tuner_reference.maximum()));
    reference_box->setPrefix("A4 = ");
    reference_box->setSuffix(" Hz");
    connect(reference_box, &QSpinBox::valueChanged, &targets.tuner_reference,
            &NumberField::set_value);

    show_hz_box = new QCheckBox("Show the detected frequency");
    connect(show_hz_box, &QCheckBox::toggled, this, targets.set_tuner_show_hz);

    QFormLayout* form = make_form();
    form->addRow("Reference pitch", reference_box);
    form->addRow("", show_hz_box);
    return make_page("The reference is also on the tuner card. Concert pitch is 440 Hz.", form);
}

// Shortcuts: listed from the window itself, so the page can't go stale.

QWidget* SettingsDialog::build_shortcuts_page() {
    shortcuts = new QTableWidget(0, 2);
    shortcuts->setHorizontalHeaderLabels({"Key", "Action"});
    shortcuts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    shortcuts->verticalHeader()->hide();
    shortcuts->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcuts->setSelectionMode(QAbstractItemView::NoSelection);
    auto* lay = new QVBoxLayout;
    lay->addWidget(shortcuts);
    return make_page("Effect keys follow the order of the effect rows: 1 … 0, then Q … P "
                     "turn the effects on and off. For live switching, map a MIDI "
                     "footswitch (MIDI → Mappings…).",
                     lay, true);
}

void SettingsDialog::refresh_shortcuts() {
    vector<std::pair<QKeySequence, QString>> rows;
    for (QAction* action : targets.window.menuBar()->actions()) {
        if (QMenu* menu = action->menu()) {
            collect_menu_shortcuts(menu, plain_text(menu->title()), rows);
        }
    }
    for (const QShortcut* shortcut : targets.window.findChildren<QShortcut*>()) {
        if (shortcut->isEnabled() && !shortcut->whatsThis().isEmpty()) {
            rows.emplace_back(shortcut->key(), shortcut->whatsThis());
        }
    }
    shortcuts->setRowCount(static_cast<int>(rows.size()));
    for (size_t i = 0; i < rows.size(); ++i) {
        const int row = static_cast<int>(i);
        shortcuts->setItem(row, 0,
                           new QTableWidgetItem(rows[i].first.toString(QKeySequence::NativeText)));
        shortcuts->setItem(row, 1, new QTableWidgetItem(rows[i].second));
    }
    shortcuts->resizeColumnToContents(0);
}

} // namespace namx

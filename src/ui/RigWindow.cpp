#include "RigWindow.h"

#include <algorithm>

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "AppImageIntegration.h"
#include "AudioEngine.h"
#include "AudioStatusBar.h"
#include "GraphModel.h"
#include "KnobWidget.h"
#include "LibraryIndex.h"
#include "MidiInput.h"
#include "MidiMapper.h"
#include "MidiMappingsDialog.h"
#include "NamProcessor.h"
#include "NodeCanvas.h"
#include "NodeCatalog.h"
#include "NumberField.h"
#include "ParamPanel.h"
#include "SettingsDialog.h"
#include "TempoBar.h"
#include "Theme.h"
#include "ToggleSwitch.h"
#include "TunerDisplay.h"
#include "TunerProcessor.h"

namespace namx
{

namespace
{

QString format_rate(double hz) {
    return QString("%1 kHz").arg(hz / 1000.0, 0, 'g', 4);
}

vector<string> to_std_strings(const QStringList& l) {
    vector<string> v;
    v.reserve(static_cast<size_t>(l.size()));
    for (const auto& s : l) {
        v.push_back(s.toStdString());
    }
    return v;
}

QStringList to_string_list(const vector<string>& v) {
    QStringList l;
    for (const auto& x : v) {
        l << QString::fromStdString(x);
    }
    return l;
}

// Save-dialog path with the .nampreset suffix; empty when cancelled.
QString ask_preset_save_path(QWidget* parent, const QString& title, const QString& start) {
    QString path = QFileDialog::getSaveFileName(parent, title, start,
                                                "NAM Preset (*.nampreset);;All Files (*)");
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty()) {
        path += ".nampreset"; // portal dialogs don't append the filter's suffix
    }
    return path;
}

} // namespace

RigWindow::RigWindow(QWidget* parent)
    : QMainWindow(parent)
    , engine(make_unique<AudioEngine>()) {
    setWindowTitle("NAM-X");
    setMinimumSize(1100, 680);
    resize(1600, 960);
    setAcceptDrops(true);

    seed_starter_presets(); // before the first scan, so they are in it
    library = new LibraryIndex(this);
    model = new GraphModel(*engine, this);
    model->set_normalize_models(QSettings().value("audio/normalizeModels", true).toBool());
    midi_mapper = new MidiMapper(this);
    midi = MidiInput::create(this);

    setup_ui();
    setup_menus();
    setup_midi();

    session_timer = new QTimer(this);
    session_timer->setSingleShot(true);
    session_timer->setInterval(1000);
    connect(session_timer, &QTimer::timeout, this, &RigWindow::save_session);
    connect(model, &GraphModel::modified, this, [this] {
        if (!restoring) {
            session_timer->start();
            update_preset_state();
        }
    });
    connect(library, &LibraryIndex::entries_changed, this, &RigWindow::rebuild_search);

    {
        const QSettings s;
        const QByteArray geo = s.value("graph/window/geometry").toByteArray();
        if (!geo.isEmpty()) {
            restoreGeometry(geo);
        }
        const QByteArray sp = s.value("graph/window/split").toByteArray();
        if (!sp.isEmpty()) {
            split->restoreState(sp);
        }
    }
    setup_audio();
    restore_session();
    register_midi_targets();
    connect(model, &GraphModel::structure_changed, this, &RigWindow::register_midi_targets);
    rebuild_search();
    QTimer::singleShot(0, canvas, &NodeCanvas::fit_all);

    meter_timer = new QTimer(this);
    meter_timer->setTimerType(Qt::PreciseTimer);
    meter_timer->setInterval(16);
    connect(meter_timer, &QTimer::timeout, this, [this] {
        if (isMinimized() || !isVisible()) {
            return;
        }
        panel->poll_meters(*engine);
        poll_tuner();
        top->poll();
    });
    meter_timer->start();

    status_timer = new QTimer(this);
    connect(status_timer, &QTimer::timeout, this, &RigWindow::update_status);
    status_timer->start(50);
}

RigWindow::~RigWindow() {
    meter_timer->stop();
    status_timer->stop();
    session_timer->stop();
}

void RigWindow::setup_ui() {
    auto* central = new QWidget;
    central->setObjectName("rigCentral");
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    top = new TopBar(engine->get_metronome());
    lay->addWidget(top);
    tuner_strip = make_tuner_strip();
    lay->addWidget(tuner_strip);
    connect(top, &TopBar::preset_menu_about_to_show, this, &RigWindow::populate_preset_menu);
    connect(top, &TopBar::rename_requested, this, &RigWindow::rename_preset);
    connect(top, &TopBar::favorite_toggled, this, &RigWindow::toggle_favorite);
    connect(top, &TopBar::settings_requested, this, &RigWindow::open_settings);
    connect(top, &TopBar::search_picked, this, &RigWindow::on_search_picked);

    split = new QSplitter(Qt::Horizontal);
    split->setObjectName("rigSplit");
    split->setChildrenCollapsible(false);
    canvas = new NodeCanvas(*model);
    panel = new ParamPanel(*model, *library);
    panel->on_duplicate = [this](int id) {
        QTimer::singleShot(0, canvas, [this, id] {
            canvas->select_node(id);
        });
    };
    split->addWidget(canvas);
    split->addWidget(panel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    split->setSizes({900, 600});
    lay->addWidget(split, 1);
    setCentralWidget(central);
    connect(canvas, &NodeCanvas::node_selected, panel, &ParamPanel::show_node);

    audio_bar = new AudioStatusBar(*engine);
    statusBar()->setSizeGripEnabled(false);
    lbl_notice = new QLabel;
    lbl_notice->setObjectName("statusNotice");
    lbl_notice->setAlignment(Qt::AlignCenter);
    statusBar()->addWidget(lbl_notice, 1);
    statusBar()->addPermanentWidget(audio_bar);
    connect(audio_bar, &AudioStatusBar::device_selection_changed, this, &RigWindow::restart_audio);
    connect(audio_bar, &AudioStatusBar::reconnect_requested, this, &RigWindow::restart_audio);

    connect(top, &TopBar::tuner_toggled, this, &RigWindow::set_tuner_on);
}

// The tuner strip under the top bar: the global tuner (not a node), shown while it runs.
QFrame* RigWindow::make_tuner_strip() {
    const QSettings s;
    auto* strip = new QFrame;
    strip->setObjectName("tunerStrip");
    auto* lay = new QHBoxLayout(strip);
    lay->setContentsMargins(18, 8, 18, 8);
    lay->setSpacing(14);
    tuner_display = new TunerDisplay;
    tuner_display->setMinimumHeight(96);
    tuner_display->set_show_hz(s.value("tuner/showHz", false).toBool());
    lay->addWidget(tuner_display, 1);

    auto* side = new QVBoxLayout;
    side->setSpacing(8);
    auto* mute_row = new QHBoxLayout;
    tuner_mute = new ToggleSwitch;
    tuner_mute->setCheckable(true);
    tuner_mute->setToolTip("Silence the rig while the tuner is on");
    tuner_mute->setChecked(s.value("tuner/mute", true).toBool());
    engine->get_tuner().set_mute(tuner_mute->isChecked());
    connect(tuner_mute, &ToggleSwitch::toggled, this, [this](bool on) {
        engine->get_tuner().set_mute(on);
        QSettings().setValue("tuner/mute", on);
    });
    auto* mute_cap = new QLabel("Mute");
    mute_cap->setObjectName("toggleCaption");
    mute_row->addWidget(tuner_mute);
    mute_row->addWidget(mute_cap);
    mute_row->addStretch();
    side->addLayout(mute_row);

    auto* ref_row = new QHBoxLayout;
    auto* ref_cap = new QLabel("A4");
    ref_cap->setObjectName("toggleCaption");
    tuner_reference = new NumberField({.minimum = 420,
                                       .maximum = 460,
                                       .decimals = 0,
                                       .step = 1,
                                       .unit = "Hz",
                                       .reset_value = 440});
    tuner_reference->setToolTip("Reference pitch: 440 Hz is concert pitch");
    tuner_reference->set_value(s.value("tuner/reference", 440).toInt());
    tuner_display->set_reference(static_cast<int>(tuner_reference->value()));
    connect(tuner_reference, &NumberField::value_changed, this, [this](double v) {
        tuner_display->set_reference(static_cast<int>(v));
        QSettings().setValue("tuner/reference", static_cast<int>(v));
    });
    ref_row->addWidget(ref_cap);
    ref_row->addWidget(tuner_reference);
    ref_row->addStretch();
    side->addLayout(ref_row);
    side->addStretch();
    lay->addLayout(side);

    Theme::tag_card(strip, "tuner");
    strip->hide();
    return strip;
}

void RigWindow::set_tuner_on(bool on) {
    engine->get_tuner().set_enabled(on);
    tuner_strip->setVisible(on);
    top->set_tuner(on);
}

void RigWindow::poll_tuner() {
    if (!tuner_strip->isVisible()) {
        return;
    }
    const TunerProcessor& t = engine->get_tuner();
    const float freq = t.get_last_pitch();
    if (freq <= 0.0f) {
        tuner_display->set_reading("-", 0.0f, false);
        return;
    }
    const float ref = static_cast<float>(tuner_reference->value());
    const int midi_note = TunerProcessor::freq_to_midi(freq, ref);
    tuner_display->set_reading(QString::fromStdString(TunerProcessor::midi_to_name(midi_note)),
                               TunerProcessor::cents_offset(freq, ref), true, freq);
}

void RigWindow::setup_menus() {
    QMenu* menu = top->main_menu();
    auto add = [this, menu](const QString& text, const QKeySequence& key, auto fn) {
        QAction* a = menu->addAction(text, this, fn);
        a->setShortcut(key);
        addAction(a); // shortcuts work with the menu closed
        return a;
    };
    add("New Rig", QKeySequence::New, [this] {
        new_rig();
    });
    add("Open Preset…", QKeySequence::Open, [this] {
        open_preset_dialog();
    });
    add("Save Preset", QKeySequence::Save, [this] {
        save_preset();
    });
    add("Save Preset As…", QKeySequence("Ctrl+Shift+S"), [this] {
        save_preset_as();
    });
    menu->addSeparator();
    QMenu* add_menu = menu->addMenu("Add Node");
    NodeCanvas::populate_add_menu(add_menu, [this](const QString& type, bool stereo) {
        if (const int id = model->add_node(type, stereo)) {
            QTimer::singleShot(0, canvas, [this, id] {
                canvas->select_node(id);
            });
        }
    });
    add("Fit Rig in View", QKeySequence("Ctrl+0"), [this] {
        canvas->fit_all();
    });
    add("Auto-Organize Rig", QKeySequence("Ctrl+L"), [this] {
        canvas->auto_organize();
    });
    add("Find…", QKeySequence::Find, [this] {
        top->focus_search();
    });
    menu->addSeparator();
    midi_menu = menu->addMenu("MIDI");
    add("Settings…", QKeySequence("Ctrl+,"), [this] {
        open_settings();
    });
    if (appimage::is_running_as_appimage()) {
        menu->addAction("Add to Applications Menu", this, [this] {
            if (auto done = appimage::add_to_menu(); !done) {
                QMessageBox::warning(this, "Applications Menu", done.error());
            }
        });
    }
    menu->addSeparator();
    add("Quit", QKeySequence::Quit, [this] {
        close();
    });

    // The Settings window's Shortcuts page lists the menu bar's actions: keep one,
    // hidden, holding the same menu.
    auto* bar = new QMenuBar(this);
    QMenu* named = bar->addMenu("NAM-X");
    named->addActions(menu->actions());
    setMenuBar(bar);
    bar->hide();

    QAction* tuner = menu->addAction("Tuner", this, [this] {
        set_tuner_on(!engine->get_tuner().is_enabled());
    });
    tuner->setShortcut(QKeySequence("Ctrl+T"));
    addAction(tuner);
    named->addAction(tuner);

    // Metronome on Space, like the old tempo bar (not while typing in a field).
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    space->setWhatsThis("Metronome on / off");
    connect(space, &QShortcut::activated, this, [this] {
        top->tempo()->toggle_play();
    });
}

void RigWindow::setup_audio() {
    const QSettings s;
    engine->set_exclusive_mode(s.value("audio/exclusive", false).toBool());
    const QString saved_in = s.value("audio/inputDeviceId").toString();
    const QString saved_out = s.value("audio/outputDeviceId").toString();
    bool started = false;
    if (!saved_in.isEmpty() || !saved_out.isEmpty()) {
        started = engine->start_with_devices(saved_in.toStdString(), saved_out.toStdString());
    }
    if (!started) {
        started = engine->start("NAM-X");
    }
    audio_ok = started;
    if (started) {
        audio_bar->refresh();
        if (engine->supports_port_routing()) {
            restore_connections();
        }
    } else {
        audio_bar->set_error("Not connected");
    }
}

void RigWindow::restart_audio() {
    audio_ok = engine->start_with_devices(audio_bar->input_device_id().toStdString(),
                                          audio_bar->output_device_id().toStdString());
    if (audio_ok) {
        audio_bar->refresh();
        if (engine->supports_port_routing()) {
            restore_connections();
        }
    } else {
        audio_bar->set_error("Not connected");
    }
}

void RigWindow::restore_connections() {
    const QSettings s;
    AudioEngine::PortConnections c;
    if (!s.contains("connections/saved")) {
        // First run: the default system ports, so a new user hears sound.
        c.input_sources = {"system:capture_1"};
        c.output_l_dests = {"system:playback_1"};
        c.output_r_dests = {"system:playback_2"};
    } else {
        c.input_sources = to_std_strings(s.value("connections/inputSources").toStringList());
        c.output_l_dests = to_std_strings(s.value("connections/outputLDests").toStringList());
        c.output_r_dests = to_std_strings(s.value("connections/outputRDests").toStringList());
    }
    engine->apply_connections(c);
}

void RigWindow::save_connections() {
    if (!engine->is_running() || !engine->supports_port_routing()) {
        return;
    }
    const IAudioBackend::PortConnections c = engine->current_connections();
    QSettings s;
    s.setValue("connections/inputSources", to_string_list(c.input_sources));
    s.setValue("connections/outputLDests", to_string_list(c.output_l_dests));
    s.setValue("connections/outputRDests", to_string_list(c.output_r_dests));
    s.setValue("connections/saved", true);
}

void RigWindow::update_status() {
    audio_bar->update_metrics();
    engine->reclaim_retired();
    if (audio_ok && !engine->is_running()) {
        audio_ok = false;
        audio_bar->set_error("Disconnected");
    }
    if (const int blocks = engine->take_non_finite_blocks(); blocks > 0) {
        non_finite_total += blocks;
        set_notice("nonFinite",
                   QString("Invalid audio (NaN/inf) scrubbed %1 time(s): nodes were reset")
                       .arg(non_finite_total),
                   "A sample that wasn't a number reached the output; it was replaced with "
                   "silence and every node's state cleared.");
    }
    double model_rate = 0.0;
    for (const auto& n : model->get_nodes()) {
        if (n.type->id == "nam") {
            model_rate = std::max(
                model_rate,
                static_cast<const NamProcessor&>(n.dsp->processor()).get_resampled_model_rate());
        }
    }
    const double device_rate = engine->sample_rate();
    if (model_rate > 0.0 && device_rate > 0.0) {
        set_notice("sampleRate",
                   QString("Model sample rate (%1) doesn't match the device (%2): resampling "
                           "uses more CPU")
                       .arg(format_rate(model_rate), format_rate(device_rate)));
    } else {
        set_notice("sampleRate", {});
    }
}

void RigWindow::set_notice(const QString& key, const QString& text, const QString& tip) {
    const auto it = std::find_if(notices.begin(), notices.end(), [&](const Notice& n) {
        return n.key == key;
    });
    const bool present = it != notices.end();
    if ((present && it->text == text && it->tip == tip) || (!present && text.isEmpty())) {
        return;
    }
    if (present) {
        notices.erase(it);
    }
    if (!text.isEmpty()) {
        notices.push_back({key, text, tip});
    }
    QStringList texts, tips;
    for (const Notice& n : notices) {
        texts << n.text;
        if (!n.tip.isEmpty()) {
            tips << n.tip;
        }
    }
    lbl_notice->setText(texts.join("  ·  "));
    lbl_notice->setToolTip(tips.join("\n"));
}

void RigWindow::open_settings() {
    if (!settings_dialog) {
        SettingsTargets targets{
            .engine = *engine,
            .status_bar = *audio_bar,
            .midi = midi,
            .midi_mapper = *midi_mapper,
            .library_index = *library,
            .tuner_reference = *tuner_reference,
            .tuner_display = *tuner_display,
            .set_tuner_show_hz =
                [this](bool on) {
                    tuner_display->set_show_hz(on);
                    QSettings().setValue("tuner/showHz", on);
                },
            .window = *this,
            .open_midi_mappings =
                [this] {
                    open_midi_mappings();
                },
            .restart_audio =
                [this] {
                    restart_audio();
                },
            .set_normalize_models =
                [this](bool on) {
                    QSettings().setValue("audio/normalizeModels", on);
                    model->set_normalize_models(on);
                },
        };
        settings_dialog = new SettingsDialog(std::move(targets), this);
    }
    settings_dialog->show_page(SettingsDialog::Page::AUDIO);
}

// MIDI

void RigWindow::setup_midi() {
    connect(midi, &MidiInput::message_received, midi_mapper, &MidiMapper::handle);
    midi_ok = midi->start();
    connect(midi_mapper, &MidiMapper::tempo_from_clock, top->tempo(), &TempoBar::set_bpm);
    // Synced rates and times follow the tempo.
    model->set_bpm(top->tempo()->get_bpm());
    connect(top->tempo(), &TempoBar::bpm_changed, model, &GraphModel::set_bpm);

    // Actions.
    midi_mapper->add_action("action:presetNext", "Presets", "Next preset", [this] {
        step_preset(+1);
    });
    midi_mapper->add_action("action:presetPrev", "Presets", "Previous preset", [this] {
        step_preset(-1);
    });
    midi_mapper->add_action("action:metronome", "Metronome", "Start / stop", [this] {
        top->tempo()->toggle_play();
    });
    midi_mapper->add_action("action:tap", "Tempo", "Tap", [this] {
        top->tempo()->tap_tempo();
    });
    midi_mapper->add_action("action:tuner", "Tuner", "On / off", [this] {
        set_tuner_on(!engine->get_tuner().is_enabled());
    });
    // Learn entries: knobs through their context-menu hook, other panel controls through
    // the panel, each by its "midiTarget" property.
    KnobWidget::set_menu_hook([this](KnobWidget* k, QMenu* menu) {
        QString id = k->property("midiTarget").toString();
        if (id.isEmpty()) {
            if (const MidiTarget* t = midi_mapper->target_for(k)) {
                id = t->id;
            }
        }
        if (!id.isEmpty() && midi_mapper->target(id)) {
            menu->addSeparator();
            add_learn_entries(id, menu);
        }
    });
    panel->learn_menu = [this](const QString& id, QMenu* menu) {
        add_learn_entries(id, menu);
    };
    top->tempo()->set_learn_menu_hook([this](const QString& id, QMenu* menu) {
        add_learn_entries(id, menu);
    });

    learn_esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    learn_esc->setEnabled(false);
    learn_esc->setWhatsThis("Cancel MIDI learn");
    connect(learn_esc, &QShortcut::activated, midi_mapper, &MidiMapper::cancel_learn);
    connect(midi_mapper, &MidiMapper::learn_changed, this, &RigWindow::on_learn_changed);

    // Status bar: a light that blinks on incoming MIDI.
    lbl_midi = new QLabel;
    lbl_midi->setObjectName("statusItem");
    lbl_midi->setTextFormat(Qt::RichText);
    statusBar()->insertWidget(0, lbl_midi);
    midi_blink = new QTimer(this);
    midi_blink->setSingleShot(true);
    midi_blink->setInterval(120);
    connect(midi_blink, &QTimer::timeout, this, [this] {
        show_midi_light(false);
    });
    connect(midi_mapper, &MidiMapper::activity, this, [this](const QString& what) {
        show_midi_light(true);
        midi_blink->start();
        lbl_midi->setToolTip("Last MIDI message: " + what);
    });
    show_midi_light(false);
    lbl_midi->setToolTip(midi_ok ? QString("MIDI input (%1)").arg(midi->backend_name())
                                 : QString("MIDI is not available on this system"));

    // Menu.
    connect(midi_menu->addAction("Mappings…"), &QAction::triggered, this,
            &RigWindow::open_midi_mappings);
    QMenu* devices = midi_menu->addMenu("Devices");
    connect(devices, &QMenu::aboutToShow, this, [this, devices] {
        devices->clear();
        const QStringList list = midi_ok ? midi->devices() : QStringList();
        if (list.isEmpty()) {
            devices->addAction(midi_ok ? "No MIDI devices found" : "MIDI not available")
                ->setEnabled(false);
        }
        for (const QString& d : list) {
            QAction* a = devices->addAction(d);
            a->setCheckable(true);
            a->setChecked(midi->is_enabled(d));
            connect(a, &QAction::toggled, this, [this, d](bool on) {
                midi->set_enabled(d, on);
            });
        }
    });
    QAction* clock = midi_menu->addAction("Follow MIDI Clock");
    clock->setCheckable(true);
    connect(midi_menu, &QMenu::aboutToShow, clock, [this, clock] {
        clock->setChecked(midi_mapper->get_follow_clock());
    });
    connect(clock, &QAction::toggled, midi_mapper, &MidiMapper::set_follow_clock);
}

void RigWindow::register_midi_targets() {
    midi_mapper->remove_targets("param:");
    // The tuner's own targets live outside the graph: keep them.
    midi_mapper->add_switch(
        "param:tuner/mute", "Tuner", "Mute",
        [this] {
            return tuner_mute->isChecked();
        },
        [this](bool on) {
            tuner_mute->setChecked(on);
        });
    midi_mapper->add_alias("param:tuner/enabled", "action:tuner");

    vector<string> aliased; // types whose first node took the chain-era IDs
    for (const auto& n : model->get_nodes()) {
        const int id = n.id();
        const QString group = n.name;
        const bool io = n.kind() == NodeKind::INPUT || n.kind() == NodeKind::OUTPUT;
        // Mappings from the effect-row era ("param:delay/mix", "param:namEnabled") reach
        // the first node of the type.
        const bool legacy = std::find(aliased.begin(), aliased.end(), n.type->id) == aliased.end();
        if (legacy) {
            aliased.push_back(n.type->id);
        }
        const QString old_group = QString::fromStdString(NodeCatalog::legacy_group(n.type->id));
        auto old_id = [&n, &old_group](const QString& key) {
            if (n.type->id == "input") {
                return QString("param:inputGain");
            }
            if (n.type->id == "output") {
                return key == "gain" ? QString("param:outputGain") : QString("param:limiter");
            }
            if (key == "enabled" && (n.type->id == "nam" || n.type->id == "ir")) {
                return QString(n.type->id == "nam" ? "param:namEnabled" : "param:irEnabled");
            }
            return "param:" + old_group + "/" + key;
        };

        if (!io) {
            const QString target = GraphModel::midi_target_id(group, "enabled");
            midi_mapper->add_switch(
                target, group, "On / Off",
                [this, id] {
                    const GraphModel::Node* x = model->node(id);
                    return x && x->enabled;
                },
                [this, id](bool on) {
                    model->set_enabled(id, on);
                });
            if (legacy) {
                midi_mapper->add_alias(old_id("enabled"), target);
            }
        }
        for (const auto& p : n.type->params) {
            if (p.stereo_only && !n.stereo()) {
                continue;
            }
            const QString key = QString::fromStdString(p.key);
            const QString target = GraphModel::midi_target_id(group, key);
            auto get = [this, id, key] {
                const GraphModel::Node* x = model->node(id);
                return x ? x->values.value(key) : 0.0;
            };
            if (p.kind == ParamSpec::Kind::SWITCH) {
                midi_mapper->add_switch(
                    target, group, QString::fromStdString(p.label),
                    [get] {
                        return get() > 0.5;
                    },
                    [this, id, key](bool on) {
                        model->set_value(id, key, on ? 1.0 : 0.0);
                    });
            } else {
                const bool stepped =
                    p.kind == ParamSpec::Kind::CHOICE || p.kind == ParamSpec::Kind::NOTE;
                std::function<bool()> reversed;
                if (!p.reverse_switch.empty()) {
                    const QString rkey = QString::fromStdString(p.reverse_switch);
                    reversed = [this, id, rkey] {
                        const GraphModel::Node* x = model->node(id);
                        return x && x->values.value(rkey) > 0.5;
                    };
                }
                QString label = QString::fromStdString(p.label);
                if (p.kind == ParamSpec::Kind::NOTE) {
                    label += " (note)";
                }
                midi_mapper->add_value(
                    target, group, label, p.min, p.max, stepped ? 1.0 : 0.0,
                    QString::fromStdString(p.unit), get,
                    [this, id, key, stepped](double v) {
                        model->set_value(id, key, stepped ? std::round(v) : v);
                    },
                    reversed);
            }
            if (legacy) {
                midi_mapper->add_alias(old_id(key), target);
            }
        }
    }
}

void RigWindow::add_learn_entries(const QString& target_id, QMenu* menu) {
    if (!midi_mapper->target(target_id)) {
        return;
    }
    connect(menu->addAction("MIDI Learn"), &QAction::triggered, this, [this, target_id] {
        if (!midi_ok) {
            QMessageBox::information(this, "MIDI Learn",
                                     "MIDI input is not available on this system.");
            return;
        }
        midi_mapper->learn(target_id);
    });
    if (midi_mapper->has_mapping(target_id)) {
        connect(menu->addAction("Forget MIDI (" + midi_mapper->mapping_summary(target_id) + ")"),
                &QAction::triggered, this, [this, target_id] {
                    midi_mapper->forget(target_id);
                });
    }
}

void RigWindow::on_learn_changed() {
    if (learn_widget) {
        learn_widget->setGraphicsEffect(nullptr);
        learn_widget = nullptr;
    }
    const QString target = midi_mapper->learning();
    learn_esc->setEnabled(!target.isEmpty());
    if (target.isEmpty()) {
        if (!learn_target.isEmpty()) {
            const MidiTarget* t = midi_mapper->target(learn_target);
            const QString summary = midi_mapper->mapping_summary(learn_target);
            statusBar()->showMessage(
                summary.isEmpty() || !t
                    ? QString("MIDI Learn cancelled")
                    : QString("MIDI: %1 → %2 · %3").arg(summary, t->group, t->label),
                4000);
        }
        learn_target.clear();
        return;
    }
    learn_target = target;
    const MidiTarget* t = midi_mapper->target(target);
    statusBar()->showMessage(
        QString("MIDI Learn: move a control on your MIDI device for %1 · %2 — Esc cancels")
            .arg(t ? t->group : QString(), t ? t->label : target));
    // Highlight the control on screen, if it is (panel controls carry their target).
    QWidget* w = nullptr;
    for (QWidget* c : findChildren<QWidget*>()) {
        if (c->isVisible() && c->property("midiTarget").toString() == target) {
            w = c;
        }
    }
    if (!w) {
        w = top->tempo()->action_widget(target);
    }
    if (w) {
        auto* glow = new QGraphicsDropShadowEffect(w);
        glow->setColor(Theme::status_warn());
        glow->setBlurRadius(22);
        glow->setOffset(0, 0);
        w->setGraphicsEffect(glow);
        learn_widget = w;
    }
}

void RigWindow::show_midi_light(bool lit) {
    const QColor c = !midi_ok ? Theme::track() : lit ? Theme::status_warn() : Theme::status_ok();
    lbl_midi->setText(QString("<span style=\"color:%1\">●</span> MIDI").arg(c.name()));
}

void RigWindow::open_midi_mappings() {
    MidiMappingsDialog dlg(*midi_mapper, midi_ok ? midi : nullptr, this);
    dlg.exec();
}

// Files into nodes

void RigWindow::load_file_into_node(const QString& path, bool is_model) {
    const string type = is_model ? "nam" : "ir";
    int target = 0;
    if (const GraphModel::Node* sel = model->node(canvas->selected_node());
        sel && sel->type->id == type) {
        target = sel->id();
    }
    for (const auto& n : model->get_nodes()) {
        if (!target && n.type->id == type) {
            target = n.id();
        }
    }
    if (!target) {
        target = model->add_node(QString::fromStdString(type), false);
    }
    if (auto ok = model->load_file(target, path); !ok) {
        QMessageBox::warning(this, "Couldn't Load the File", ok.error());
        return;
    }
    const int id = target;
    QTimer::singleShot(0, canvas, [this, id] {
        canvas->select_node(id);
    });
}

void RigWindow::open_paths(const QStringList& paths) {
    for (const QString& p : paths) {
        bool ok = false;
        const LibraryEntry::Kind kind = LibraryIndex::kind_for_path(p, &ok);
        if (!ok) {
            continue;
        }
        if (kind == LibraryEntry::Kind::PRESET) {
            load_preset_file(p);
        } else {
            load_file_into_node(p, kind == LibraryEntry::Kind::MODEL);
        }
    }
}

void RigWindow::dragEnterEvent(QDragEnterEvent* ev) {
    if (ev->mimeData()->hasUrls()) {
        ev->acceptProposedAction();
    }
}

void RigWindow::dropEvent(QDropEvent* ev) {
    QStringList paths;
    for (const QUrl& u : ev->mimeData()->urls()) {
        if (u.isLocalFile()) {
            paths << u.toLocalFile();
        }
    }
    open_paths(paths);
    ev->acceptProposedAction();
}

// Presets

QStringList RigWindow::preset_paths() const {
    QStringList paths;
    for (const auto& e : library->get_entries()) {
        if (e.kind == LibraryEntry::Kind::PRESET) {
            paths << e.path;
        }
    }
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).completeBaseName().compare(QFileInfo(b).completeBaseName(),
                                                       Qt::CaseInsensitive) < 0;
    });
    return paths;
}

void RigWindow::populate_preset_menu() {
    QMenu* m = top->preset_menu();
    m->clear();
    m->addAction("‹  Previous", this, [this] {
        step_preset(-1);
    });
    m->addAction("›  Next", this, [this] {
        step_preset(1);
    });
    m->addSeparator();
    m->addAction("Save", this, &RigWindow::save_preset);
    m->addAction("Save As…", this, &RigWindow::save_preset_as);
    m->addAction("Open…", this, &RigWindow::open_preset_dialog);
    m->addAction("New Rig", this, &RigWindow::new_rig);
    const QStringList paths = preset_paths();
    if (!paths.isEmpty()) {
        m->addSection("Presets");
        const QStringList favs = QSettings().value("library/favorites").toStringList();
        for (const QString& p : paths) {
            const QString name =
                (favs.contains(p) ? QString("★ ") : QString()) + QFileInfo(p).completeBaseName();
            QAction* a = m->addAction(name, this, [this, p] {
                load_preset_file(p);
            });
            a->setCheckable(true);
            a->setChecked(p == preset_path);
        }
    }
}

void RigWindow::step_preset(int step) {
    const QStringList paths = preset_paths();
    if (paths.isEmpty()) {
        return;
    }
    const qsizetype at = paths.indexOf(preset_path);
    const qsizetype size = paths.size();
    const qsizetype next = at < 0 ? (step > 0 ? 0 : size - 1) : ((at + step) % size + size) % size;
    load_preset_file(paths[next]);
}

bool RigWindow::load_preset_file(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 4 * 1024 * 1024) {
        QMessageBox::warning(this, "Preset", "Cannot read the preset:\n" + path);
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, "Preset",
                             "The preset isn't valid JSON:\n" + path + "\n\n" + err.errorString());
        return false;
    }
    const QJsonObject obj = doc.object();
    if (obj["version"].toInt(0) > 3) {
        QMessageBox::warning(this, "Preset",
                             "The preset was saved by a newer NAM-X. Update NAM-X to load it.");
        return false;
    }
    QStringList warnings;
    restoring = true;
    const bool ok = model->from_json(obj, &warnings);
    restoring = false;
    if (!ok) {
        QMessageBox::warning(this, "Preset", "This file isn't a NAM-X preset:\n" + path);
        return false;
    }
    set_current_preset(path);
    session_timer->start();
    QTimer::singleShot(0, canvas, &NodeCanvas::fit_all);
    if (!warnings.isEmpty()) {
        QMessageBox::warning(this, "Preset", "Loaded with problems:\n\n" + warnings.join("\n"));
    }
    return true;
}

bool RigWindow::write_preset_file(const QString& path) {
    const QByteArray json = QJsonDocument(model->to_json()).toJson();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(json) != json.size() || !f.commit()) {
        QMessageBox::critical(this, "Preset",
                              "Cannot write the preset:\n" + path + "\n\n" + f.errorString());
        return false;
    }
    library->rescan();
    return true;
}

void RigWindow::save_preset() {
    if (preset_path.isEmpty() || !QFile::exists(preset_path)) {
        save_preset_as();
        return;
    }
    if (write_preset_file(preset_path)) {
        set_current_preset(preset_path);
    }
}

void RigWindow::save_preset_as() {
    const QString start = preset_path.isEmpty()
                              ? LibraryRoots::default_preset_dir() + "/Untitled.nampreset"
                              : preset_path;
    const QString path = ask_preset_save_path(this, "Save Preset As", start);
    if (!path.isEmpty() && write_preset_file(path)) {
        set_current_preset(path);
    }
}

void RigWindow::rename_preset() {
    if (preset_path.isEmpty() || !QFile::exists(preset_path)) {
        save_preset_as();
        return;
    }
    const QFileInfo fi(preset_path);
    bool ok = false;
    const QString name = QInputDialog::getText(this, "Rename Preset", "Name:", QLineEdit::Normal,
                                               fi.completeBaseName(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == fi.completeBaseName()) {
        return;
    }
    if (name.contains('/') || name.contains('\\')) {
        QMessageBox::warning(this, "Rename Preset", "A name can't contain / or \\.");
        return;
    }
    const QString target = fi.absolutePath() + "/" + name + "." + fi.suffix();
    if (QFile::exists(target)) {
        QMessageBox::warning(this, "Rename Preset", "A preset with that name already exists.");
        return;
    }
    if (!QFile::rename(preset_path, target)) {
        QMessageBox::warning(this, "Rename Preset", "The file couldn't be renamed.");
        return;
    }
    // Keep it a favourite under its new name.
    QSettings s;
    QStringList favs = s.value("library/favorites").toStringList();
    if (favs.removeAll(preset_path) > 0) {
        favs << target;
        s.setValue("library/favorites", favs);
    }
    preset_path = target;
    library->rescan();
    update_preset_state();
    session_timer->start();
}

void RigWindow::new_rig() {
    restoring = true;
    model->reset_to_default();
    restoring = false;
    set_current_preset({});
    session_timer->start();
    QTimer::singleShot(0, canvas, &NodeCanvas::fit_all);
}

void RigWindow::open_preset_dialog() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Preset",
        preset_path.isEmpty() ? LibraryRoots::default_preset_dir() : QFileInfo(preset_path).path(),
        "NAM Preset (*.nampreset *.json);;All Files (*)");
    if (!path.isEmpty()) {
        load_preset_file(path);
    }
}

void RigWindow::set_current_preset(const QString& path) {
    preset_path = path;
    preset_baseline = model->to_json();
    update_preset_state();
}

void RigWindow::update_preset_state() {
    const bool untitled = preset_path.isEmpty();
    const QString name = untitled ? QString("Untitled") : QFileInfo(preset_path).completeBaseName();
    top->set_preset(name, model->to_json() != preset_baseline, untitled);
    top->set_favorite(!untitled &&
                      QSettings().value("library/favorites").toStringList().contains(preset_path));
    setWindowTitle(QString("%1 — NAM-X").arg(name));
}

void RigWindow::toggle_favorite(bool on) {
    if (preset_path.isEmpty()) {
        top->set_favorite(false);
        return;
    }
    QSettings s;
    QStringList favs = s.value("library/favorites").toStringList();
    favs.removeAll(preset_path);
    if (on) {
        favs << preset_path;
    }
    s.setValue("library/favorites", favs);
}

void RigWindow::seed_starter_presets() {
    QSettings s;
    if (s.value("presets/starterSeeded").toBool()) {
        return;
    }
    const QString dir = LibraryRoots::default_preset_dir() + "/Starter";
    QDir().mkpath(dir);
    for (const QFileInfo& fi : QDir(":/presets").entryInfoList({"*.nampreset"}, QDir::Files)) {
        const QString dest = dir + "/" + fi.fileName();
        if (QFile::exists(dest) || !QFile::copy(fi.filePath(), dest)) {
            continue;
        }
        QFile::setPermissions(dest, QFile::permissions(dest) | QFile::WriteOwner);
    }
    if (!QDir(dir).entryList({"*.nampreset"}, QDir::Files).isEmpty()) {
        s.setValue("presets/starterSeeded", true);
    }
}

// Session

void RigWindow::save_session() {
    QSettings s;
    s.setValue("graph/session",
               QString::fromUtf8(QJsonDocument(model->to_json()).toJson(QJsonDocument::Compact)));
    s.setValue("graph/presetPath", preset_path);
    s.setValue(
        "graph/presetBaseline",
        preset_path.isEmpty()
            ? QString()
            : QString::fromUtf8(QJsonDocument(preset_baseline).toJson(QJsonDocument::Compact)));
}

QJsonObject RigWindow::legacy_session() {
    QSettings s;
    if (!s.childGroups().contains("session")) {
        return {};
    }
    QJsonObject obj;
    s.beginGroup("session");
    auto value_of = [](const QVariant& v) -> QJsonValue {
        const QString text = v.toString();
        if (text == "true" || text == "false") {
            return text == "true";
        }
        bool ok = false;
        const double d = text.toDouble(&ok);
        return ok ? QJsonValue(d) : QJsonValue(text);
    };
    for (const QString& key : s.allKeys()) {
        if (key == "effectOrder" || key.startsWith("preset")) {
            continue;
        }
        const qsizetype slash = key.indexOf('/');
        if (slash < 0) {
            obj[key] = value_of(s.value(key));
        } else {
            QJsonObject g = obj[key.left(slash)].toObject();
            g[key.mid(slash + 1)] = value_of(s.value(key));
            obj[key.left(slash)] = g;
        }
    }
    obj["effectOrder"] = QJsonArray::fromStringList(s.value("effectOrder").toStringList());
    s.endGroup();
    obj["namPath"] = s.value("files/lastNAM").toString();
    obj["irPath"] = s.value("files/lastIR").toString();
    return obj;
}

void RigWindow::restore_session() {
    QSettings s;
    restoring = true;
    bool restored = false;
    const QByteArray saved = s.value("graph/session").toString().toUtf8();
    if (!saved.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(saved);
        restored = doc.isObject() && model->from_json(doc.object());
    }
    if (!restored) {
        // First launch of the graph UI: carry the chain-era rig over.
        const QJsonObject legacy = legacy_session();
        restored = !legacy.isEmpty() && model->from_json(legacy);
    }
    if (!restored) {
        model->reset_to_default();
    }
    restoring = false;

    preset_path = s.value("graph/presetPath").toString();
    const QJsonDocument base =
        QJsonDocument::fromJson(s.value("graph/presetBaseline").toString().toUtf8());
    preset_baseline = preset_path.isEmpty() ? model->to_json() : base.object();
    update_preset_state();
}

// Search

void RigWindow::rebuild_search() {
    QList<TopBar::SearchEntry> entries;
    for (const auto& t : NodeCatalog::types()) {
        const QString id = QString::fromStdString(t.id);
        const QString name = QString::fromStdString(t.name);
        const QString cat = QString::fromStdString(t.category);
        if (t.has_mono) {
            entries.push_back(
                {TopBar::SearchEntry::Kind::NODE, name + " (" + cat + ")", id, false});
        }
        if (t.has_stereo) {
            entries.push_back({TopBar::SearchEntry::Kind::NODE,
                               (t.has_mono ? name + " Stereo (" : name + " (") + cat + ")", id,
                               true});
        }
    }
    for (const auto& e : library->get_entries()) {
        const TopBar::SearchEntry::Kind kind =
            e.kind == LibraryEntry::Kind::PRESET  ? TopBar::SearchEntry::Kind::PRESET
            : e.kind == LibraryEntry::Kind::MODEL ? TopBar::SearchEntry::Kind::MODEL
                                                  : TopBar::SearchEntry::Kind::IR;
        entries.push_back({kind, e.name, e.path, false});
    }
    top->set_search_entries(entries);
}

void RigWindow::on_search_picked(const TopBar::SearchEntry& entry) {
    switch (entry.kind) {
        case TopBar::SearchEntry::Kind::NODE:
            if (const int id = model->add_node(entry.payload, entry.stereo)) {
                QTimer::singleShot(0, canvas, [this, id] {
                    canvas->select_node(id);
                });
            }
            break;
        case TopBar::SearchEntry::Kind::PRESET:
            load_preset_file(entry.payload);
            break;
        case TopBar::SearchEntry::Kind::MODEL:
            load_file_into_node(entry.payload, true);
            break;
        case TopBar::SearchEntry::Kind::IR:
            load_file_into_node(entry.payload, false);
            break;
    }
}

void RigWindow::closeEvent(QCloseEvent* ev) {
    save_session();
    save_connections();
    QSettings s;
    s.setValue("graph/window/geometry", saveGeometry());
    s.setValue("graph/window/split", split->saveState());
    QMainWindow::closeEvent(ev);
}

void RigWindow::offer_desktop_integration() {
    if (!appimage::is_running_as_appimage()) {
        return;
    }
    appimage::refresh_launcher_path();
    QSettings settings;
    if (settings.value("appimage/offerAnswered").toBool() || appimage::is_integrated_by_anyone()) {
        return;
    }
    QMessageBox box(QMessageBox::Question, "Applications Menu",
                    "Add NAM-X to your applications menu?", QMessageBox::NoButton, this);
    box.setInformativeText("It will start from the menu and open .nam and .nampreset files.");
    QPushButton* add = box.addButton("Add to Menu", QMessageBox::AcceptRole);
    box.addButton("Not Now", QMessageBox::RejectRole);
    box.exec();
    settings.setValue("appimage/offerAnswered", true);
    if (box.clickedButton() == add) {
        if (expected<void, QString> done = appimage::add_to_menu(); !done) {
            QMessageBox::warning(this, "Applications Menu", done.error());
        }
    }
}

} // namespace namx

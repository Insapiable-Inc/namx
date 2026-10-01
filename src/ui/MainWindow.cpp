#include "MainWindow.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "AudioStatusBar.h"
#include "Constants.h"
#include "EffectShortcuts.h"
#include "GainReductionMeter.h"
#include "LibraryDock.h"
#include "LibraryPanel.h"
#include "MidiInput.h"
#include "MidiMapper.h"
#include "MidiMappingsDialog.h"
#include "PresetBar.h"
#include "PresetSerializer.h"
#include "TempoBar.h"
#include "TempoSync.h"
#include "Theme.h"
#include "Tone3000Client.h"
#include "Tone3000Panel.h"
#include "TunerDisplay.h"
#include "TunerProcessor.h"
#include "VUMeter.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Mime type carrying the effect ID of a dragged strip card.
static const char K_EFFECT_MIME[] = "application/x-nam-effect-id";

// QStringList → the engine's std::string list (effect IDs, JACK port names).
static std::vector<std::string> to_std_strings(const QStringList& list) {
    std::vector<std::string> out;
    for (const QString& s : list) {
        out.push_back(s.toStdString());
    }
    return out;
}

// Fixed columns at the left edge of every row card, so the enable toggles,
// stereo slots, and first knobs line up vertically across the strip.
static constexpr int K_GRIP_COLUMN_WIDTH = 22; // "≡" drag handle
static constexpr int K_NAME_COLUMN_WIDTH = 120; // fits "Dynamic Wah"
static constexpr int K_ENABLE_SLOT_WIDTH = 74; // toggle + "On"/"Off" caption
static constexpr int K_STEREO_SLOT_WIDTH = 96; // toggle + "Stereo" caption
// Every row is exactly as tall as a knob row (40 px dial + knob margins +
// card margins + border), so knob-less rows and the tuner match the rest.
static constexpr int K_ROW_CARD_HEIGHT = 62;
static constexpr int K_DETAIL_ROW_HEIGHT = 54; // the ▾ row of advanced knobs
// Loaded file name on the Amplifier/Cabinet rows elides beyond this.
static constexpr int K_HERO_NAME_MAX_WIDTH = 420;

// Enable toggle with a live "On"/"Off" caption beside it — one line, so the
// switch stays on the row's vertical centerline.
static QWidget* make_enable_slot(ToggleSwitch* sw) {
    auto* w = new QWidget;
    w->setFixedWidth(K_ENABLE_SLOT_WIDTH);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* lbl = new QLabel(sw->isChecked() ? "On" : "Off");
    lbl->setObjectName("toggleCaption");
    QObject::connect(sw, &ToggleSwitch::toggled, lbl, [lbl](bool on) {
        lbl->setText(on ? "On" : "Off");
    });

    lay->addWidget(sw, 0, Qt::AlignVCenter);
    lay->addWidget(lbl, 0, Qt::AlignVCenter);
    lay->addStretch();
    return w;
}

// The stereo slot is reserved on every effect row (blank when the effect has
// no stereo mode) so the knob column starts at the same X on all rows.
// Toggle and caption sit on one line for the same centering reason.
static QWidget* make_stereo_slot(ToggleSwitch** out_toggle) {
    auto* w = new QWidget;
    w->setFixedWidth(K_STEREO_SLOT_WIDTH);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    if (out_toggle) {
        auto* sw = new ToggleSwitch;
        sw->setCheckable(true);
        sw->setChecked(false);
        auto* lbl = new QLabel("Stereo");
        lbl->setObjectName("toggleCaption");
        lay->addWidget(sw, 0, Qt::AlignVCenter);
        lay->addWidget(lbl, 0, Qt::AlignVCenter);
        lay->addStretch();
        *out_toggle = sw;
    }

    return w;
}

// A card's main row (grip, name, enable slot, stereo slot, controls…).
static QHBoxLayout* row_layout(QWidget* card) {
    return static_cast<QHBoxLayout*>(
        card->findChild<QWidget*>("cardRow", Qt::FindDirectChildrenOnly)->layout());
}

// Rows without a stereo mode (Gate, Compressor) show their gain-reduction
// meter in the reserved stereo slot, so the knob column stays aligned.
static void place_in_stereo_slot(QWidget* card, QWidget* w) {
    // Main row: grip, name, enable slot, stereo slot, …
    QWidget* slot = row_layout(card)->itemAt(3)->widget();
    slot->layout()->addWidget(w);
}

// Frequency readout for EQ/cut knobs: "250 Hz", "3.2 kHz", or "Off" at the
// knob's open end (offAt > 0: at or below it; offAt < 0: at or above −offAt).
static std::function<QString(double)> hz_label(double off_at = 0.0) {
    return [off_at](double hz) {
        if ((off_at > 0.0 && hz <= off_at) || (off_at < 0.0 && hz >= -off_at)) {
            return QString("Off");
        }
        return hz < 1000.0 ? QString("%1 Hz").arg(qRound(hz))
                           : QString("%1 kHz").arg(hz / 1000.0, 0, 'f', hz < 10000.0 ? 1 : 0);
    };
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_engine(std::make_unique<AudioEngine>()) {
    setWindowTitle("NAM Player");
    // Laptop-friendly minimum; the effect strip scrolls when it doesn't fit.
    // The default size still shows the full strip — window managers clamp it
    // on smaller screens, and the session restores the user's geometry.
    setMinimumSize(1280, 720);
    resize(1800, 1000);
    setAcceptDrops(true);

    seed_starter_presets(); // before the first scan, so they are in it

    // The index starts its first scan immediately (worker thread); the panel
    // created in setupUI picks the entries up when they land.
    m_library_index = new LibraryIndex(this);
    m_tone3000 = new Tone3000Client(this);
    m_midi_mapper = new MidiMapper(this); // targets fill in as parameters register

    setup_ui();
    register_parameters();
    setup_midi(m_midi_menu);

    // Debounced session save — every registry widget change restarts it, so
    // the session survives a crash without a write per knob tick. Created
    // before restoreSessionState so the restore's widget signals find it.
    m_session_save_timer = new QTimer(this);
    m_session_save_timer->setSingleShot(true);
    m_session_save_timer->setInterval(1000);
    connect(m_session_save_timer, &QTimer::timeout, this, &MainWindow::save_session_state);
    // The preset `*` follows the same changes, coalesced to one compare per
    // event-loop pass (a preset load moves dozens of widgets).
    m_preset_state_timer = new QTimer(this);
    m_preset_state_timer->setSingleShot(true);
    m_preset_state_timer->setInterval(0);
    connect(m_preset_state_timer, &QTimer::timeout, this, &MainWindow::update_preset_state);

    // Knob tapers (Phase 9): times, rates and frequencies on log dials.
    for (KnobWidget* k :
         {m_knob_gate_attack, m_knob_gate_release, m_knob_comp_attack, m_knob_comp_release,
          m_knob_fl_rate, m_knob_ph_rate, m_knob_ch_rate, m_knob_tr_rate, m_knob_del_time}) {
        k->set_log_taper(true);
    }

    restore_session_state();
    {
        QSettings s;
        const QByteArray geo = s.value("window/geometry").toByteArray();
        if (!geo.isEmpty()) {
            restoreGeometry(geo);
        }
        const QByteArray state = s.value("window/state").toByteArray();
        if (!state.isEmpty()) {
            restoreState(state); // dock position + visibility
        }
    }

    setup_audio();
    restore_last_files();
    restore_preset_state();

    m_meter_timer = new QTimer(this);
    m_meter_timer->setTimerType(Qt::PreciseTimer);
    m_meter_timer->setInterval(16);
    connect(m_meter_timer, &QTimer::timeout, this, &MainWindow::update_meters);
    update_meter_timer();

    m_status_timer = new QTimer(this);
    connect(m_status_timer, &QTimer::timeout, this, &MainWindow::update_status);
    m_status_timer->start(50);

    m_tuner_timer = new QTimer(this);
    connect(m_tuner_timer, &QTimer::timeout, this, &MainWindow::update_tuner);
    m_tuner_timer->start(100);
}

MainWindow::~MainWindow() {
    m_meter_timer->stop();
    m_status_timer->stop();
    m_tuner_timer->stop();
    m_session_save_timer->stop();
    m_preset_state_timer->stop();
    QSettings().setValue("window/geometry", saveGeometry());
    QSettings().setValue("window/state", saveState());
    save_session_state();
    save_connections();
    m_engine->stop();
}

// ---------------------------------------------------------------------------

QFrame* MainWindow::make_row_card(const QString& title) {
    auto* card = new QFrame;
    card->setProperty("fxCard", true); // card chrome lives in the Theme QSS
    card->setProperty("displayName", title); // context menu + drag bookkeeping
    card->setFixedHeight(K_ROW_CARD_HEIGHT);
    // Card = main row (+ an optional detail row, see addDetailRow).
    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* row = new QWidget;
    row->setObjectName("cardRow");
    row->setFixedHeight(K_ROW_CARD_HEIGHT - 2); // inside the 1 px border
    outer->addWidget(row);
    outer->addStretch();
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(10);

    // Grip column: the ≡ handle is the only place a reorder drag can start,
    // so toggle/knob clicks can never move a card.
    auto* grip = new QLabel("≡");
    grip->setObjectName("fxGrip");
    grip->setFixedWidth(K_GRIP_COLUMN_WIDTH);
    grip->setAlignment(Qt::AlignCenter);
    grip->setCursor(Qt::OpenHandCursor);
    grip->setToolTip("Drag to reorder");
    lay->addWidget(grip, 0, Qt::AlignVCenter);
    m_card_grips[card] = grip;

    auto* name = new QLabel(title);
    name->setObjectName("fxName");
    name->setFixedWidth(K_NAME_COLUMN_WIDTH);
    lay->addWidget(name, 0, Qt::AlignVCenter);

    return card;
}

KnobWidget* MainWindow::late_knob(const QString& group, const QString& key, KnobWidget* k,
                                  std::function<void(double)> apply) {
    m_late_params.push_back([this, group, key, k, apply] {
        knob_param(group, key, k, apply);
    });
    return k;
}

void MainWindow::add_detail_row(QWidget* card, const QList<QWidget*>& widgets) {
    // All detail rows of a card live in one container behind one ▾ toggle;
    // a second call adds a second row.
    auto* container = card->findChild<QWidget*>("cardDetail", Qt::FindDirectChildrenOnly);
    const bool first = !container;
    if (first) {
        container = new QWidget;
        container->setObjectName("cardDetail");
        auto* v = new QVBoxLayout(container);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        container->hide();
        static_cast<QVBoxLayout*>(card->layout())->insertWidget(1, container);
    }
    auto* row = new QWidget;
    row->setFixedHeight(K_DETAIL_ROW_HEIGHT);
    auto* lay = new QHBoxLayout(row);
    // Indent to the knob column: margin + grip + name + enable + stereo slots.
    const int indent = 10 + K_GRIP_COLUMN_WIDTH + K_NAME_COLUMN_WIDTH + K_ENABLE_SLOT_WIDTH +
                       K_STEREO_SLOT_WIDTH + 4 * 10;
    lay->setContentsMargins(indent, 0, 10, 8);
    lay->setSpacing(10);
    for (QWidget* w : widgets) {
        lay->addWidget(w, 0, Qt::AlignVCenter);
    }
    lay->addStretch();
    container->layout()->addWidget(row);
    const int rows = container->layout()->count();

    QToolButton* more = card->findChild<QToolButton*>("cardMore");
    if (!first) {
        // Re-apply the current state with the new height.
        if (more && more->isChecked()) {
            card->setFixedHeight(K_ROW_CARD_HEIGHT + rows * K_DETAIL_ROW_HEIGHT);
        }
        return;
    }

    const QString name = card->property("displayName").toString();
    more = new QToolButton;
    more->setObjectName("cardMore");
    more->setCheckable(true);
    more->setFocusPolicy(Qt::NoFocus);
    more->setAutoRaise(true);
    row_layout(card)->addWidget(more, 0, Qt::AlignVCenter); // after the stretch: right edge
    auto apply = [card, container, more](bool open) {
        container->setVisible(open);
        card->setFixedHeight(K_ROW_CARD_HEIGHT +
                             (open ? container->layout()->count() * K_DETAIL_ROW_HEIGHT : 0));
        more->setText(open ? "▴" : "▾");
        more->setToolTip(open ? "Fewer controls" : "More controls");
    };
    connect(more, &QToolButton::toggled, this, [apply, name](bool open) {
        apply(open);
        QSettings s;
        QStringList expanded = s.value("ui/expandedCards").toStringList();
        expanded.removeAll(name);
        if (open) {
            expanded << name;
        }
        s.setValue("ui/expandedCards", expanded);
    });
    const bool open = QSettings().value("ui/expandedCards").toStringList().contains(name);
    more->setChecked(open);
    apply(open);
}

QWidget* MainWindow::make_effect_row(const QString& title, ToggleSwitch*& toggle_out,
                                     const std::initializer_list<KnobWidget*>& knobs,
                                     ToggleSwitch** stereo_toggle_out) {
    QFrame* card = make_row_card(title);
    auto* lay = row_layout(card);

    toggle_out = new ToggleSwitch;
    toggle_out->setCheckable(true);
    toggle_out->setChecked(false);
    lay->addWidget(make_enable_slot(toggle_out), 0, Qt::AlignVCenter);

    lay->addWidget(make_stereo_slot(stereo_toggle_out), 0, Qt::AlignVCenter);

    for (auto* k : knobs) {
        lay->addWidget(k, 0, Qt::AlignVCenter);
    }

    lay->addStretch();
    return card;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void MainWindow::setup_ui() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(8, 4, 8, 6);
    root_layout->setSpacing(4);

    // ---- Menu bar ----
    QMenu* library_menu = nullptr;
    QMenu* preset_menu = nullptr;
    QMenu* midi_menu = nullptr;
    {
        auto* mb = menuBar();
        preset_menu = mb->addMenu("Preset"); // also behind ⋯ on the preset bar
        auto* act_save = preset_menu->addAction("Save");
        act_save->setShortcut(QKeySequence::Save); // Ctrl+S
        connect(act_save, &QAction::triggered, this, &MainWindow::on_save_preset);
        auto* act_save_as = preset_menu->addAction("Save As…");
        act_save_as->setShortcut(QKeySequence("Ctrl+Shift+S"));
        connect(act_save_as, &QAction::triggered, this, &MainWindow::on_save_preset_as);
        auto* act_load = preset_menu->addAction("Open…");
        act_load->setShortcut(QKeySequence::Open); // Ctrl+O
        connect(act_load, &QAction::triggered, this, &MainWindow::on_load_preset);
        preset_menu->addSeparator();
        connect(preset_menu->addAction("Import…"), &QAction::triggered, this,
                &MainWindow::on_import_presets);
        connect(preset_menu->addAction("Export…"), &QAction::triggered, this,
                &MainWindow::on_export_preset);
        preset_menu->addSeparator();
        connect(preset_menu->addAction("Show in Folder"), &QAction::triggered, this,
                &MainWindow::on_locate_preset);
        connect(preset_menu->addAction("Delete…"), &QAction::triggered, this,
                &MainWindow::on_delete_preset);
        preset_menu->addSeparator();
        connect(preset_menu->addAction("Reset All to Defaults…"), &QAction::triggered, this,
                &MainWindow::on_reset_all);

        library_menu = mb->addMenu("Library"); // filled once the dock exists (below)
        midi_menu = mb->addMenu("MIDI"); // filled by setupMidi

        auto* help_menu = mb->addMenu("Help");
        connect(help_menu->addAction("About NAM Player"), &QAction::triggered, this, [this] {
            QMessageBox::about(
                this, "About NAM Player",
                QString("<b>NAM Player</b> v%1<br><br>"
                        "Standalone player for Neural Amp Modeler profiles.<br>"
                        "Built on <a href=\"https://github.com/sdatkinson/NeuralAmpModelerCore\">"
                        "NeuralAmpModelerCore</a> by Steven Atkinson.<br><br>"
                        "Not affiliated with the official Neural Amp Modeler plugin.")
                    .arg(QApplication::applicationVersion()));
        });
    }

    // ---- Preset bar (top): name, ‹ ›, Save, quick slots, A/B, Preset menu.
    m_preset_bar = new PresetBar(*m_library_index, preset_menu);
    root_layout->addWidget(m_preset_bar);
    connect(m_preset_bar, &PresetBar::preset_chosen, this, [this](const QString& p) {
        load_preset_file(p);
    });
    connect(m_preset_bar, &PresetBar::step_requested, this, &MainWindow::step_preset);
    connect(m_preset_bar, &PresetBar::save_requested, this, &MainWindow::on_save_preset);
    connect(m_preset_bar, &PresetBar::save_as_requested, this, &MainWindow::on_save_preset_as);
    connect(m_preset_bar, &PresetBar::ab_switch_requested, this, &MainWindow::switch_ab);
    connect(m_preset_bar, &PresetBar::ab_copy_requested, this, &MainWindow::copy_ab);

    // ---- Tempo bar: global tempo (session setting) + metronome.
    m_tempo_bar = new TempoBar(m_engine->metronome());
    m_bpm = m_tempo_bar->bpm();
    root_layout->addWidget(m_tempo_bar);
    connect(m_tempo_bar, &TempoBar::bpm_changed, this, &MainWindow::set_tempo);

    // ---- Status bar (bottom right): backend, rate, buffer, latency, devices.
    m_audio_bar = new AudioStatusBar(*m_engine);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(m_audio_bar);
    m_lbl_rig_status = new QLabel;
    m_lbl_rig_status->setObjectName("statusItem");
    m_lbl_rig_status->setTextFormat(Qt::RichText);
    statusBar()->addWidget(m_lbl_rig_status);
    connect(m_tone3000, &Tone3000Client::auth_changed, this, &MainWindow::update_rig_status);
    connect(m_tone3000, &Tone3000Client::user_changed, this, &MainWindow::update_rig_status);
    connect(m_audio_bar, &AudioStatusBar::device_selection_changed, this,
            &MainWindow::restart_audio_with_current_devices);
    connect(m_audio_bar, &AudioStatusBar::reconnect_requested, this,
            &MainWindow::restart_audio_with_current_devices);

    // ---- Scrollable effect strip ----
    // (NAM model / IR file pickers live inside the Amp and IR rows below.)
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    m_strip = new QWidget;
    m_strip_lay = new QVBoxLayout(m_strip);
    m_strip_lay->setContentsMargins(2, 2, 2, 2);
    m_strip_lay->setSpacing(4);
    m_strip_lay->setAlignment(Qt::AlignTop);

    // Effect columns are created once here, keyed by EffectBase*. They are
    // NOT added to the layout — rebuildEffectStrip lays them out in engine
    // chain order, so audio order and UI order stay in sync automatically.

    // ---- Tuner ----
    {
        QFrame* col = make_row_card("Tuner");
        auto* lay = row_layout(col);

        m_sw_tuner_enable = new ToggleSwitch;
        m_sw_tuner_enable->setCheckable(true);
        lay->addWidget(make_enable_slot(m_sw_tuner_enable), 0, Qt::AlignVCenter);
        lay->addWidget(make_stereo_slot(nullptr), 0, Qt::AlignVCenter);

        m_tuner_display = new TunerDisplay;
        m_tuner_display->setMaximumWidth(560); // one compact instrument, not a strip
        lay->addWidget(m_tuner_display, 1, Qt::AlignVCenter);

        // Reference pitch + Hz readout: machine settings (QSettings tuner/*),
        // not part of presets or the registry.
        QSettings s;
        m_tuner_ref_hz = std::clamp(s.value("tuner/reference", 440).toInt(), 420, 460);
        auto* ref = new QSpinBox;
        ref->setObjectName("tunerRef");
        ref->setRange(420, 460);
        ref->setPrefix("A4 = ");
        ref->setSuffix(" Hz");
        ref->setValue(m_tuner_ref_hz);
        ref->setFocusPolicy(Qt::ClickFocus); // keep digit keys for effect shortcuts
        ref->setToolTip("Reference pitch (concert pitch is 440 Hz)");
        connect(ref, &QSpinBox::valueChanged, this, [this](int hz) {
            m_tuner_ref_hz = hz;
            m_tuner_display->set_reference(hz);
            QSettings().setValue("tuner/reference", hz);
        });
        m_tuner_display->set_reference(m_tuner_ref_hz);

        auto* hz = new QToolButton;
        hz->setObjectName("tunerOption");
        hz->setText("Hz");
        hz->setCheckable(true);
        hz->setFocusPolicy(Qt::NoFocus);
        hz->setToolTip("Show the detected frequency");
        hz->setChecked(s.value("tuner/showHz", false).toBool());
        m_tuner_display->set_show_hz(hz->isChecked());
        connect(hz, &QToolButton::toggled, this, [this](bool on) {
            m_tuner_display->set_show_hz(on);
            QSettings().setValue("tuner/showHz", on);
        });

        ref->setFixedWidth(112);
        lay->addSpacing(8);
        lay->addWidget(ref, 0, Qt::AlignVCenter);
        lay->addWidget(hz, 0, Qt::AlignVCenter);
        lay->addStretch();

        Theme::tag_card(col, "tuner"); // after children — pushes accents to them
        m_fx_cols[&m_engine->tuner()] = col;
    }

    // ---- Transpose ----
    {
        auto* semis = late_knob("transpose", "semitones",
                                new KnobWidget("Semitones", -12.0, 12.0, 0.0), [this](double v) {
                                    m_engine->transpose().set_semitones(static_cast<float>(v));
                                });
        semis->set_snap(1.0);
        semis->set_value_formatter([](double v) {
            const int s = static_cast<int>(std::lround(v));
            return s == 0 ? QString("0 st") : QString("%1%2 st").arg(s > 0 ? "+" : "").arg(s);
        });
        semis->setToolTip("Pitch shift in semitones: −1 / −2 for drop tunings, ±12 for octaves "
                          "(adds ~25 ms latency when not 0)");
        auto* mix = late_knob("transpose", "mix", new KnobWidget("Mix", 0.0, 100.0, 100.0, "%"),
                              [this](double v) {
                                  m_engine->transpose().set_mix(static_cast<float>(v));
                              });
        mix->set_scroll_step(1.0);
        auto* col = make_effect_row("Transpose", m_sw_transpose_enable, {semis, mix});
        Theme::tag_card(col, "transpose");
        m_fx_cols[&m_engine->transpose()] = col;
    }

    // ---- Noise Gate ----
    {
        using namespace NAMConstants;
        m_knob_gate_thresh = new KnobWidget("Threshold", K_GATE_THRESH_MIN, K_GATE_THRESH_MAX,
                                            K_GATE_THRESH_DEFAULT, "dB");
        m_knob_gate_attack = new KnobWidget("Attack", K_GATE_ATTACK_MIN, K_GATE_ATTACK_MAX,
                                            K_GATE_ATTACK_DEFAULT, "ms");
        m_knob_gate_release = new KnobWidget("Release", K_GATE_RELEASE_MIN, K_GATE_RELEASE_MAX,
                                             K_GATE_RELEASE_DEFAULT, "ms");
        m_knob_gate_thresh->set_scroll_step(1.0);
        m_knob_gate_attack->set_scroll_step(1.0);
        m_knob_gate_release->set_scroll_step(10.0);

        auto* col = make_effect_row("Noise Gate", m_sw_gate_enable,
                                    {m_knob_gate_thresh, m_knob_gate_attack, m_knob_gate_release});
        m_gr_gate = new GainReductionMeter(40.0f, Theme::accent("gate"));
        place_in_stereo_slot(col, m_gr_gate);
        Theme::tag_card(col, "gate");

        m_fx_cols[&m_engine->gate()] = col;
    }

    // ---- Compressor ----
    {
        using namespace NAMConstants;
        m_knob_comp_thresh = new KnobWidget("Threshold", K_COMP_THRESH_MIN, K_COMP_THRESH_MAX,
                                            K_COMP_THRESH_DEFAULT, "dB");
        m_knob_comp_ratio =
            new KnobWidget("Ratio", K_COMP_RATIO_MIN, K_COMP_RATIO_MAX, K_COMP_RATIO_DEFAULT, ":1");
        m_knob_comp_attack = new KnobWidget("Attack", K_COMP_ATTACK_MIN, K_COMP_ATTACK_MAX,
                                            K_COMP_ATTACK_DEFAULT, "ms");
        m_knob_comp_release = new KnobWidget("Release", K_COMP_RELEASE_MIN, K_COMP_RELEASE_MAX,
                                             K_COMP_RELEASE_DEFAULT, "ms");
        m_knob_comp_makeup = new KnobWidget("Makeup Gain", K_COMP_MAKEUP_MIN, K_COMP_MAKEUP_MAX,
                                            K_COMP_MAKEUP_DEFAULT, "dB");
        m_knob_comp_thresh->set_scroll_step(1.0);
        m_knob_comp_ratio->set_scroll_step(0.5);
        m_knob_comp_attack->set_scroll_step(1.0);
        m_knob_comp_release->set_scroll_step(10.0);
        m_knob_comp_makeup->set_scroll_step(0.5);

        auto* col = make_effect_row("Compressor", m_sw_comp_enable,
                                    {m_knob_comp_thresh, m_knob_comp_ratio, m_knob_comp_attack,
                                     m_knob_comp_release, m_knob_comp_makeup});
        m_gr_comp = new GainReductionMeter(20.0f, Theme::accent("comp"));
        place_in_stereo_slot(col, m_gr_comp);
        Theme::tag_card(col, "comp");

        m_fx_cols[&m_engine->comp()] = col;
    }

    // ---- Dynamic Wah ----
    {
        using namespace NAMConstants;
        m_knob_wah_sens =
            new KnobWidget("Sensitivity", K_WAH_SENS_MIN, K_WAH_SENS_MAX, K_WAH_SENS_DEFAULT, "%");
        m_knob_wah_res =
            new KnobWidget("Resonance", K_WAH_RES_MIN, K_WAH_RES_MAX, K_WAH_RES_DEFAULT, "%");
        m_knob_wah_mix =
            new KnobWidget("Mix", K_WAH_MIX_MIN, K_WAH_MIX_MAX, K_WAH_MIX_DEFAULT, "%");
        m_knob_wah_sens->set_scroll_step(1.0);
        m_knob_wah_res->set_scroll_step(1.0);
        m_knob_wah_mix->set_scroll_step(1.0);

        auto* mode =
            late_knob("dynamicWah", "mode", new KnobWidget("Mode", 0, 1, 0), [this](double v) {
                m_engine->wah().set_pedal_mode(v > 0.5);
            });
        mode->set_snap(1.0);
        mode->set_value_formatter([](double v) {
            return v > 0.5 ? QString("Pedal") : QString("Auto");
        });
        mode->setToolTip("Auto: the envelope sweeps the wah · Pedal: Position sweeps it (map an "
                         "expression pedal)");
        auto* pos = late_knob("dynamicWah", "position",
                              new KnobWidget("Position", K_WAH_POSITION_MIN, K_WAH_POSITION_MAX,
                                             K_WAH_POSITION_DEFAULT, "%"),
                              [this](double v) {
                                  m_engine->wah().set_position(static_cast<float>(v));
                              });
        pos->set_scroll_step(1.0);
        auto* col = make_effect_row("Dynamic Wah", m_sw_wah_enable,
                                    {m_knob_wah_mix, m_knob_wah_sens, m_knob_wah_res, mode, pos});
        Theme::tag_card(col, "wah");

        m_fx_cols[&m_engine->wah()] = col;
    }

    // ---- Drive (TS-style boost in front of the amp) ----
    {
        using namespace NAMConstants;
        m_knob_drive = new KnobWidget("Drive", K_DRIVE_MIN, K_DRIVE_MAX, K_DRIVE_DEFAULT, "%");
        m_knob_drive_tone =
            new KnobWidget("Tone", K_DRIVE_TONE_MIN, K_DRIVE_TONE_MAX, K_DRIVE_TONE_DEFAULT, "%");
        m_knob_drive_level = new KnobWidget("Level", K_DRIVE_LEVEL_MIN, K_DRIVE_LEVEL_MAX,
                                            K_DRIVE_LEVEL_DEFAULT, "dB");
        m_knob_drive->set_scroll_step(1.0);
        m_knob_drive_tone->set_scroll_step(1.0);
        m_knob_drive_level->set_scroll_step(0.5);
        auto* col = make_effect_row("Drive", m_sw_drive_enable,
                                    {m_knob_drive, m_knob_drive_tone, m_knob_drive_level});
        Theme::tag_card(col, "drive");
        m_fx_cols[&m_engine->drive()] = col;
    }

    // ---- Amp (NAM) ----
    {
        auto* col = make_effect_row("Amplifier", m_sw_nam_enable, {}, &m_sw_nam_stereo);
        m_sw_nam_stereo->setToolTip("Stereo input: run the amp per channel (twice the CPU).\n"
                                    "Off: a stereo effect before the amp is summed to mono first.");
        m_sw_nam_enable->setChecked(true);
        make_hero_controls(col, LibraryEntry::Kind::MODEL, m_lbl_nam_status, m_lbl_nam_meta,
                           m_lbl_nam_origin);

        Theme::tag_card(col, "nam");
        m_fx_cols[&m_engine->nam()] = col;
    }

    // ---- EQ ----
    {
        using namespace NAMConstants;
        m_knob_low = new KnobWidget("Bass", K_EQ_BAND_MIN, K_EQ_BAND_MAX, K_EQ_BAND_DEFAULT, "dB");
        m_knob_mid =
            new KnobWidget("Middle", K_EQ_BAND_MIN, K_EQ_BAND_MAX, K_EQ_BAND_DEFAULT, "dB");
        m_knob_treble =
            new KnobWidget("Treble", K_EQ_BAND_MIN, K_EQ_BAND_MAX, K_EQ_BAND_DEFAULT, "dB");
        m_knob_low->set_scroll_step(0.1);
        m_knob_mid->set_scroll_step(0.1);
        m_knob_treble->set_scroll_step(0.1);

        auto eq_set = [this](EQProcessor::Band b, bool gain) {
            return [this, b, gain](double v) {
                gain ? m_engine->eq().set_gain(b, v) : m_engine->eq().set_freq(b, v);
            };
        };
        auto* hi_mid = late_knob(
            "eq", "highMid",
            new KnobWidget("High-Mid", K_EQ_BAND_MIN, K_EQ_BAND_MAX, K_EQ_BAND_DEFAULT, "dB"),
            eq_set(EQProcessor::HIGH_MID, true));
        hi_mid->set_scroll_step(0.1);
        auto freq_knob = [&](const QString& key, const QString& label, double lo, double hi,
                             double def, EQProcessor::Band b, double off_at) {
            auto* k =
                late_knob("eq", key, new KnobWidget(label, lo, hi, def, "Hz"), eq_set(b, false));
            k->set_log_taper(true);
            k->set_value_formatter(hz_label(off_at));
            return static_cast<QWidget*>(k);
        };
        auto* col = make_effect_row("Equalizer", m_sw_eq_enable,
                                    {m_knob_low, m_knob_mid, hi_mid, m_knob_treble});
        add_detail_row(
            col,
            {
                freq_knob("lowFreq", "Bass Freq", K_EQ_BASS_FREQ_MIN, K_EQ_BASS_FREQ_MAX,
                          K_EQ_BASS_FREQ_DEFAULT, EQProcessor::BASS, 0),
                freq_knob("midFreq", "Mid Freq", K_EQ_MID_FREQ_MIN, K_EQ_MID_FREQ_MAX,
                          K_EQ_MID_FREQ_DEFAULT, EQProcessor::MIDDLE, 0),
                freq_knob("highMidFreq", "High-Mid Freq", K_EQ_HI_MID_FREQ_MIN,
                          K_EQ_HI_MID_FREQ_MAX, K_EQ_HI_MID_FREQ_DEFAULT, EQProcessor::HIGH_MID, 0),
                freq_knob("trebleFreq", "Treble Freq", K_EQ_TREBLE_FREQ_MIN, K_EQ_TREBLE_FREQ_MAX,
                          K_EQ_TREBLE_FREQ_DEFAULT, EQProcessor::TREBLE, 0),
                freq_knob("lowCut", "Low Cut", K_LOW_CUT_MIN, K_LOW_CUT_MAX, K_LOW_CUT_DEFAULT,
                          EQProcessor::LOW_CUT, K_LOW_CUT_MIN + 0.5),
                freq_knob("highCut", "High Cut", K_HIGH_CUT_MIN, K_HIGH_CUT_MAX, K_HIGH_CUT_DEFAULT,
                          EQProcessor::HIGH_CUT, -(K_HIGH_CUT_MAX - 1.0)),
            });
        m_sw_eq_enable->setChecked(true);
        Theme::tag_card(col, "eq");
        m_fx_cols[&m_engine->eq()] = col;
    }

    // ---- Doubler ----
    {
        using namespace NAMConstants;
        m_knob_dbl_spread = new KnobWidget("Spread", K_DOUBLER_SPREAD_MIN, K_DOUBLER_SPREAD_MAX,
                                           K_DOUBLER_SPREAD_DEFAULT, "ms");
        m_knob_dbl_mix =
            new KnobWidget("Mix", K_DOUBLER_MIX_MIN, K_DOUBLER_MIX_MAX, K_DOUBLER_MIX_DEFAULT, "%");
        m_knob_dbl_spread->set_scroll_step(0.5);
        m_knob_dbl_mix->set_scroll_step(1.0);
        auto* col =
            make_effect_row("Doubler", m_sw_doubler_enable, {m_knob_dbl_mix, m_knob_dbl_spread});
        col->setToolTip(
            "Double-tracking from a mono signal: left dry, right a drifting delayed copy.\n"
            "Passes a stereo input through untouched.");
        Theme::tag_card(col, "doubler");
        m_fx_cols[&m_engine->doubler()] = col;
    }

    // ---- Volume pedal ----
    {
        using namespace NAMConstants;
        m_knob_vol_pos =
            new KnobWidget("Volume", K_VOLUME_POS_MIN, K_VOLUME_POS_MAX, K_VOLUME_POS_DEFAULT, "%");
        m_knob_vol_mid =
            new KnobWidget("Mid", K_VOLUME_MID_MIN, K_VOLUME_MID_MAX, K_VOLUME_MID_DEFAULT, "dB");
        m_knob_vol_pos->set_scroll_step(1.0);
        m_knob_vol_mid->set_scroll_step(1.0);
        m_knob_vol_mid->setToolTip("Level at half travel: lower = swells stay quiet longer");
        auto* col =
            make_effect_row("Volume Pedal", m_sw_volume_enable, {m_knob_vol_pos, m_knob_vol_mid});
        Theme::tag_card(col, "volume");
        m_fx_cols[&m_engine->volume()] = col;
    }

    // ---- Flanger ----
    {
        using namespace NAMConstants;
        m_knob_fl_rate = new KnobWidget("Rate", K_FLANGER_RATE_MIN, K_FLANGER_RATE_MAX,
                                        K_FLANGER_RATE_DEFAULT, "Hz");
        m_knob_fl_depth = new KnobWidget("Depth", K_FLANGER_DEPTH_MIN, K_FLANGER_DEPTH_MAX,
                                         K_FLANGER_DEPTH_DEFAULT, "%");
        m_knob_fl_feedback =
            new KnobWidget("Feedback", K_FLANGER_FEEDBACK_MIN, K_FLANGER_FEEDBACK_MAX,
                           K_FLANGER_FEEDBACK_DEFAULT, "%");
        m_knob_fl_mix =
            new KnobWidget("Mix", K_FLANGER_MIX_MIN, K_FLANGER_MIX_MAX, K_FLANGER_MIX_DEFAULT, "%");
        m_knob_fl_rate->set_scroll_step(0.1);
        m_knob_fl_depth->set_scroll_step(1.0);
        m_knob_fl_feedback->set_scroll_step(1.0);
        m_knob_fl_mix->set_scroll_step(1.0);

        auto* col =
            make_effect_row("Flanger", m_sw_flanger_enable,
                            {m_knob_fl_mix, m_knob_fl_rate, m_knob_fl_depth, m_knob_fl_feedback},
                            &m_sw_flanger_stereo);
        add_tempo_sync(col, m_knob_fl_rate, "Rate", false, 1 /* 2 bars */, K_FLANGER_RATE_MIN,
                       K_FLANGER_RATE_MAX, [this](double v) {
                           m_engine->flanger().set_rate(static_cast<float>(v));
                       });
        auto* range = late_knob("flanger", "range",
                                new KnobWidget("Range", K_FLANGER_RANGE_MIN, K_FLANGER_RANGE_MAX,
                                               K_FLANGER_RANGE_DEFAULT, "%"),
                                [this](double v) {
                                    m_engine->flanger().set_range(static_cast<float>(v));
                                });
        range->set_scroll_step(1.0);
        range->setToolTip("Sweep length: low = short, metallic · high = long jet sweep");
        add_detail_row(col, {range});
        Theme::tag_card(col, "flanger");

        m_fx_cols[&m_engine->flanger()] = col;
    }

    // ---- Phaser ----
    {
        using namespace NAMConstants;
        m_knob_ph_rate = new KnobWidget("Rate", K_PHASER_RATE_MIN, K_PHASER_RATE_MAX,
                                        K_PHASER_RATE_DEFAULT, "Hz");
        m_knob_ph_depth = new KnobWidget("Depth", K_PHASER_DEPTH_MIN, K_PHASER_DEPTH_MAX,
                                         K_PHASER_DEPTH_DEFAULT, "%");
        m_knob_ph_feedback = new KnobWidget("Feedback", K_PHASER_FEEDBACK_MIN,
                                            K_PHASER_FEEDBACK_MAX, K_PHASER_FEEDBACK_DEFAULT, "%");
        m_knob_ph_mix =
            new KnobWidget("Mix", K_PHASER_MIX_MIN, K_PHASER_MIX_MAX, K_PHASER_MIX_DEFAULT, "%");
        m_knob_ph_rate->set_scroll_step(0.1);
        m_knob_ph_depth->set_scroll_step(1.0);
        m_knob_ph_feedback->set_scroll_step(1.0);
        m_knob_ph_mix->set_scroll_step(1.0);

        auto* col =
            make_effect_row("Phaser", m_sw_phaser_enable,
                            {m_knob_ph_mix, m_knob_ph_rate, m_knob_ph_depth, m_knob_ph_feedback},
                            &m_sw_phaser_stereo);
        add_tempo_sync(col, m_knob_ph_rate, "Rate", false, 4 /* 1/2 */, K_PHASER_RATE_MIN,
                       K_PHASER_RATE_MAX, [this](double v) {
                           m_engine->phaser().set_rate(static_cast<float>(v));
                       });
        auto* mode = late_knob("phaser", "mode", new KnobWidget("Mode", 0, 1, 0), [this](double v) {
            m_engine->phaser().set_vibe(v > 0.5);
        });
        mode->set_snap(1.0);
        mode->set_value_formatter([](double v) {
            return v > 0.5 ? QString("Vibe") : QString("Classic");
        });
        mode->setToolTip(
            "Classic phaser, or Vibe: staggered stages, lamp-shaped sweep, no feedback");
        add_detail_row(col, {mode});
        Theme::tag_card(col, "phaser");

        m_fx_cols[&m_engine->phaser()] = col;
    }

    // ---- Chorus ----
    {
        using namespace NAMConstants;
        m_knob_ch_rate = new KnobWidget("Rate", K_CHORUS_RATE_MIN, K_CHORUS_RATE_MAX,
                                        K_CHORUS_RATE_DEFAULT, "Hz");
        m_knob_ch_depth = new KnobWidget("Depth", K_CHORUS_DEPTH_MIN, K_CHORUS_DEPTH_MAX,
                                         K_CHORUS_DEPTH_DEFAULT, "%");
        m_knob_ch_mix =
            new KnobWidget("Mix", K_CHORUS_MIX_MIN, K_CHORUS_MIX_MAX, K_CHORUS_MIX_DEFAULT, "%");
        m_knob_ch_rate->set_scroll_step(0.1);
        m_knob_ch_depth->set_scroll_step(1.0);
        m_knob_ch_mix->set_scroll_step(1.0);

        auto* col =
            make_effect_row("Chorus", m_sw_chorus_enable,
                            {m_knob_ch_mix, m_knob_ch_rate, m_knob_ch_depth}, &m_sw_chorus_stereo);
        add_tempo_sync(col, m_knob_ch_rate, "Rate", false, 2 /* 1/1 */, K_CHORUS_RATE_MIN,
                       K_CHORUS_RATE_MAX, [this](double v) {
                           m_engine->chorus().set_rate(static_cast<float>(v));
                       });
        auto* mode = late_knob("chorus", "mode", new KnobWidget("Mode", 0, 3, 0), [this](double v) {
            m_engine->chorus().set_mode(static_cast<int>(std::lround(v)));
        });
        mode->set_snap(1.0);
        mode->set_value_formatter([](double v) {
            static const char* names[] = {"Normal", "Tri", "Vintage", "Spatial"};
            return QString(names[std::clamp(static_cast<int>(std::lround(v)), 0, 3)]);
        });
        mode->setToolTip("Normal · Tri (three voices) · Vintage (darker, triangle LFO) · Spatial "
                         "(wider stereo)");
        add_detail_row(col, {mode});
        Theme::tag_card(col, "chorus");

        m_fx_cols[&m_engine->chorus()] = col;
    }

    // ---- Tremolo ----
    {
        using namespace NAMConstants;
        m_knob_tr_rate = new KnobWidget("Rate", K_TREMOLO_RATE_MIN, K_TREMOLO_RATE_MAX,
                                        K_TREMOLO_RATE_DEFAULT, "Hz");
        m_knob_tr_depth = new KnobWidget("Depth", K_TREMOLO_DEPTH_MIN, K_TREMOLO_DEPTH_MAX,
                                         K_TREMOLO_DEPTH_DEFAULT, "%");
        m_knob_tr_rate->set_scroll_step(0.1);
        m_knob_tr_depth->set_scroll_step(1.0);

        auto* col = make_effect_row("Tremolo", m_sw_tremolo_enable,
                                    {m_knob_tr_rate, m_knob_tr_depth}, &m_sw_tremolo_stereo);
        add_tempo_sync(col, m_knob_tr_rate, "Rate", false, 10 /* 1/8 */, K_TREMOLO_RATE_MIN,
                       K_TREMOLO_RATE_MAX, [this](double v) {
                           m_engine->tremolo().set_rate(static_cast<float>(v));
                       });
        Theme::tag_card(col, "tremolo");

        m_fx_cols[&m_engine->tremolo()] = col;
    }

    // ---- Delay ----
    {
        using namespace NAMConstants;
        m_knob_del_time =
            new KnobWidget("Time", K_DELAY_TIME_MIN, K_DELAY_TIME_MAX, K_DELAY_TIME_DEFAULT, "ms");
        m_knob_del_feedback = new KnobWidget("Feedback", K_DELAY_FEEDBACK_MIN, K_DELAY_FEEDBACK_MAX,
                                             K_DELAY_FEEDBACK_DEFAULT, "%");
        m_knob_del_mix =
            new KnobWidget("Mix", K_DELAY_MIX_MIN, K_DELAY_MIX_MAX, K_DELAY_MIX_DEFAULT, "%");
        m_knob_del_time->set_scroll_step(1.0);
        m_knob_del_feedback->set_scroll_step(1.0);
        m_knob_del_mix->set_scroll_step(1.0);

        auto* col = make_effect_row("Delay", m_sw_delay_enable,
                                    {m_knob_del_mix, m_knob_del_time, m_knob_del_feedback},
                                    &m_sw_delay_stereo);
        add_tempo_sync(col, m_knob_del_time, "Time", true, 8 /* 1/8. */, K_DELAY_TIME_MIN,
                       K_DELAY_TIME_MAX, [this](double v) {
                           m_engine->delay().set_time(static_cast<float>(v));
                       });
        auto pct = [&](const QString& key, const QString& label, double lo, double hi, double def,
                       std::function<void(double)> f, const QString& tip) {
            auto* k =
                late_knob("delay", key, new KnobWidget(label, lo, hi, def, "%"), std::move(f));
            k->set_scroll_step(1.0);
            k->setToolTip(tip);
            return static_cast<QWidget*>(k);
        };
        auto* low_cut = late_knob(
            "delay", "lowCut",
            new KnobWidget("Low Cut", K_LOW_CUT_MIN, K_DELAY_LOW_CUT_MAX, K_LOW_CUT_DEFAULT, "Hz"),
            [this](double v) {
                m_engine->delay().set_low_cut(static_cast<float>(v));
            });
        low_cut->set_log_taper(true);
        low_cut->set_value_formatter(hz_label(K_LOW_CUT_MIN + 0.5));
        auto* high_cut = late_knob("delay", "highCut",
                                   new KnobWidget("High Cut", K_DELAY_HIGH_CUT_MIN, K_HIGH_CUT_MAX,
                                                  K_HIGH_CUT_DEFAULT, "Hz"),
                                   [this](double v) {
                                       m_engine->delay().set_high_cut(static_cast<float>(v));
                                   });
        high_cut->set_log_taper(true);
        high_cut->set_value_formatter(hz_label(-(K_HIGH_CUT_MAX - 1.0)));
        add_detail_row(
            col, {
                     pct(
                         "rightRatio", "R Time", K_DELAY_RIGHT_MIN, K_DELAY_RIGHT_MAX,
                         K_DELAY_RIGHT_DEFAULT,
                         [this](double v) {
                             m_engine->delay().set_right_ratio(static_cast<float>(v));
                         },
                         "Right repeat time as a share of Time (stereo): 75 % or 150 % for a wide, "
                         "uneven bounce"),
                     pct(
                         "crossFeed", "Cross-feed", K_DELAY_CROSS_MIN, K_DELAY_CROSS_MAX,
                         K_DELAY_CROSS_DEFAULT,
                         [this](double v) {
                             m_engine->delay().set_cross_feed(static_cast<float>(v));
                         },
                         "100 % = ping-pong between the sides · 0 % = two separate echoes"),
                     low_cut,
                     high_cut,
                     pct(
                         "saturation", "Saturation", K_DELAY_SAT_MIN, K_DELAY_SAT_MAX,
                         K_DELAY_SAT_DEFAULT,
                         [this](double v) {
                             m_engine->delay().set_saturation(static_cast<float>(v));
                         },
                         "Tape-style rounding of the repeats as they build up"),
                     pct(
                         "modulation", "Modulation", K_DELAY_MOD_MIN, K_DELAY_MOD_MAX,
                         K_DELAY_MOD_DEFAULT,
                         [this](double v) {
                             m_engine->delay().set_modulation(static_cast<float>(v));
                         },
                         "Wobble on the repeats (tape flutter / chorused echoes)"),
                     pct(
                         "crystals", "Crystals", 0.0, 100.0, 0.0,
                         [this](double v) {
                             m_engine->delay().set_crystals(static_cast<float>(v));
                         },
                         "Octave-up repeats: each echo climbs higher"),
                 });
        m_sw_delay_stereo->setChecked(true);
        Theme::tag_card(col, "delay");

        m_fx_cols[&m_engine->delay()] = col;
    }

    // ---- Reverb ----
    {
        using namespace NAMConstants;
        m_knob_rev_room = new KnobWidget("Decay", K_REVERB_ROOM_MIN, K_REVERB_ROOM_MAX,
                                         K_REVERB_ROOM_DEFAULT, "%");
        m_knob_rev_damping = new KnobWidget("Damping", K_REVERB_DAMPING_MIN, K_REVERB_DAMPING_MAX,
                                            K_REVERB_DAMPING_DEFAULT, "%");
        m_knob_rev_width = new KnobWidget("Width", K_REVERB_WIDTH_MIN, K_REVERB_WIDTH_MAX,
                                          K_REVERB_WIDTH_DEFAULT, "%");
        m_knob_rev_mix =
            new KnobWidget("Mix", K_REVERB_MIX_MIN, K_REVERB_MIX_MAX, K_REVERB_MIX_DEFAULT, "%");
        m_knob_rev_room->set_scroll_step(1.0);
        m_knob_rev_damping->set_scroll_step(1.0);
        m_knob_rev_width->set_scroll_step(1.0);
        m_knob_rev_mix->set_scroll_step(1.0);

        auto* col = make_effect_row(
            "Reverb", m_sw_reverb_enable,
            {m_knob_rev_mix, m_knob_rev_room, m_knob_rev_damping, m_knob_rev_width});
        auto* pre =
            late_knob("reverb", "preDelay",
                      new KnobWidget("Pre-delay", K_REVERB_PRE_DELAY_MIN, K_REVERB_PRE_DELAY_MAX,
                                     K_REVERB_PRE_DELAY_DEFAULT, "ms"),
                      [this](double v) {
                          m_engine->reverb().set_pre_delay(static_cast<float>(v));
                      });
        pre->set_scroll_step(5.0);
        pre->setToolTip("Gap before the tail starts: keeps the note clear of the room");
        auto* low_cut = late_knob(
            "reverb", "lowCut",
            new KnobWidget("Low Cut", K_LOW_CUT_MIN, K_REVERB_LOW_CUT_MAX, K_LOW_CUT_DEFAULT, "Hz"),
            [this](double v) {
                m_engine->reverb().set_low_cut(static_cast<float>(v));
            });
        low_cut->set_log_taper(true);
        low_cut->set_value_formatter(hz_label(K_LOW_CUT_MIN + 0.5));
        auto* high_cut = late_knob("reverb", "highCut",
                                   new KnobWidget("High Cut", K_REVERB_HIGH_CUT_MIN, K_HIGH_CUT_MAX,
                                                  K_HIGH_CUT_DEFAULT, "Hz"),
                                   [this](double v) {
                                       m_engine->reverb().set_high_cut(static_cast<float>(v));
                                   });
        high_cut->set_log_taper(true);
        high_cut->set_value_formatter(hz_label(-(K_HIGH_CUT_MAX - 1.0)));
        auto* shimmer = late_knob(
            "reverb", "shimmer", new KnobWidget("Shimmer", 0.0, 100.0, 0.0, "%"), [this](double v) {
                m_engine->reverb().set_shimmer(static_cast<float>(v));
            });
        shimmer->set_scroll_step(1.0);
        shimmer->setToolTip("Octave-up feedback into the tail — the shimmer reverb");
        add_detail_row(col, {pre, low_cut, high_cut, shimmer});
        Theme::tag_card(col, "reverb");

        m_fx_cols[&m_engine->reverb()] = col;
    }

    // ---- IR toggle ----
    {
        auto* col = make_effect_row("Cabinet", m_sw_ir_enable, {});
        m_sw_ir_enable->setChecked(true);
        make_hero_controls(col, LibraryEntry::Kind::IR, m_lbl_ir_status, m_lbl_ir_meta,
                           m_lbl_ir_origin);

        // Dual cab (Phase 17): A/B mixer + cuts, then slot B's picker.
        using namespace NAMConstants;
        auto level = [&](int slot) {
            auto* k = late_knob("ir", slot ? "levelB" : "levelA",
                                new KnobWidget(slot ? "Level B" : "Level A", -24.0, 6.0, 0.0, "dB"),
                                [this, slot](double v) {
                                    m_engine->ir().set_level(slot, static_cast<float>(v));
                                });
            k->set_scroll_step(0.5);
            return static_cast<QWidget*>(k);
        };
        auto pan = [&](int slot) {
            auto* k = late_knob("ir", slot ? "panB" : "panA",
                                new KnobWidget(slot ? "Pan B" : "Pan A", -100.0, 100.0, 0.0, "%"),
                                [this, slot](double v) {
                                    m_engine->ir().set_pan(slot, static_cast<float>(v));
                                });
            k->set_scroll_step(10.0);
            k->set_value_formatter([](double v) {
                const int p = static_cast<int>(std::lround(v));
                return p == 0 ? QString("Center")
                              : QString("%1 %2").arg(std::abs(p)).arg(p < 0 ? "L" : "R");
            });
            return static_cast<QWidget*>(k);
        };
        auto phase = [&](int slot) {
            auto* sw = new ToggleSwitch;
            sw->setCheckable(true);
            sw->setToolTip("Invert this slot's polarity (fix phase cancellation between two mics)");
            m_late_params.push_back([this, sw, slot] {
                toggle_param("ir", slot ? "invertB" : "invertA", sw, [this, slot](bool b) {
                    m_engine->ir().set_phase(slot, b);
                });
            });
            auto* w = new QWidget;
            auto* l = new QHBoxLayout(w);
            l->setContentsMargins(0, 0, 8, 0);
            l->setSpacing(6);
            auto* cap = new QLabel(slot ? "Ø B" : "Ø A");
            cap->setObjectName("toggleCaption");
            l->addWidget(sw, 0, Qt::AlignVCenter);
            l->addWidget(cap, 0, Qt::AlignVCenter);
            return w;
        };
        auto* blend = late_knob("ir", "blend", new KnobWidget("Blend", 0.0, 100.0, 50.0, "%"),
                                [this](double v) {
                                    m_engine->ir().set_blend(static_cast<float>(v));
                                });
        blend->set_scroll_step(1.0);
        blend->set_value_formatter([](double v) {
            const int b = static_cast<int>(std::lround(v));
            return b == 50  ? QString("A = B")
                   : b < 50 ? QString("A %1").arg(100 - b)
                            : QString("B %1").arg(b);
        });
        blend->setToolTip("Crossfade between the two IRs (only when both slots are loaded)");
        auto* low_cut = late_knob(
            "ir", "lowCut",
            new KnobWidget("Low Cut", K_LOW_CUT_MIN, K_LOW_CUT_MAX, K_LOW_CUT_DEFAULT, "Hz"),
            [this](double v) {
                m_engine->ir().set_low_cut(static_cast<float>(v));
            });
        low_cut->set_log_taper(true);
        low_cut->set_value_formatter(hz_label(K_LOW_CUT_MIN + 0.5));
        auto* high_cut = late_knob(
            "ir", "highCut",
            new KnobWidget("High Cut", K_HIGH_CUT_MIN, K_HIGH_CUT_MAX, K_HIGH_CUT_DEFAULT, "Hz"),
            [this](double v) {
                m_engine->ir().set_high_cut(static_cast<float>(v));
            });
        high_cut->set_log_taper(true);
        high_cut->set_value_formatter(hz_label(-(K_HIGH_CUT_MAX - 1.0)));
        add_detail_row(col, {level(0), pan(0), phase(0), blend, low_cut, high_cut});

        // Slot B: ‹ › through the library's IRs, Choose… (file dialog), ×.
        auto* b_title = new QLabel("IR B");
        b_title->setObjectName("fxName");
        auto arrow = [this](const QString& glyph, int step) {
            auto* b = new QToolButton;
            b->setObjectName("heroArrow");
            b->setText(glyph);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            b->setToolTip(step < 0 ? "Previous IR in the library (slot B)"
                                   : "Next IR in the library (slot B)");
            connect(b, &QToolButton::clicked, this, [this, step] {
                const QString next = m_library->neighbour(
                    LibraryEntry::Kind::IR, QString::fromStdString(m_engine->ir_path_b()), step);
                if (!next.isEmpty()) {
                    load_irb_file(next);
                }
            });
            return b;
        };
        auto* choose = new QPushButton("Choose…");
        choose->setFocusPolicy(Qt::NoFocus);
        connect(choose, &QPushButton::clicked, this, [this] {
            QString start = QString::fromStdString(m_engine->ir_path_b());
            if (start.isEmpty()) {
                start = QString::fromStdString(m_engine->ir_path());
            }
            const QString p = QFileDialog::getOpenFileName(
                nullptr, "Impulse Response for Slot B", start, "WAV Files (*.wav);;All Files (*)");
            if (!p.isEmpty()) {
                load_irb_file(p);
            }
        });
        auto* clear = new QToolButton;
        clear->setObjectName("heroArrow");
        clear->setText("×");
        clear->setAutoRaise(true);
        clear->setFocusPolicy(Qt::NoFocus);
        clear->setToolTip("Empty slot B");
        connect(clear, &QToolButton::clicked, this, [this] {
            m_engine->unload_irb();
            QSettings().remove("files/lastIRB");
            update_hero_cards();
        });
        m_lbl_irb_status = new QLabel;
        m_lbl_irb_status->setObjectName("fileStatus");
        add_detail_row(col, {b_title, arrow("‹", -1), arrow("›", +1), choose, clear,
                             m_lbl_irb_status, level(1), pan(1), phase(1)});

        Theme::tag_card(col, "ir");
        m_fx_cols[&m_engine->ir()] = col;
    }

    // Input/Output gain columns. Not draggable and not part of the chain, so
    // they flank the strip: input on the left, output on the right — signal
    // flows in → chain → out. The full-height meter reads bottom→top.
    auto make_gain_column = [&](const QString& title, KnobWidget*& knob,
                                VUMeter*& meter) -> QWidget* {
        using namespace NAMConstants;
        auto* col = new QFrame;
        col->setProperty("fxCard", true);
        col->setProperty("displayName", title);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(8, 8, 8, 8);
        lay->setSpacing(8);

        auto* name = new QLabel(title);
        name->setObjectName("fxName");
        name->setAlignment(Qt::AlignHCenter);
        knob = new KnobWidget("Gain", K_GAIN_MIN, K_GAIN_MAX, K_GAIN_DEFAULT, "dB",
                              KnobWidget::Form::STACKED);
        knob->set_scroll_step(0.1);
        meter = new VUMeter(Qt::Vertical);

        lay->addWidget(name);
        lay->addWidget(knob, 0, Qt::AlignHCenter);
        lay->addWidget(meter, 1, Qt::AlignHCenter);
        Theme::tag_card(col, "neutral"); // after children — pushes accents to them
        return col;
    };

    m_input_card = make_gain_column("Input", m_knob_input_gain, m_meter_in);
    m_output_card = make_gain_column("Output", m_knob_output_gain, m_meter_out);
    {
        // Output limiter switch under the gain knob.
        m_sw_limiter = new ToggleSwitch;
        m_sw_limiter->setCheckable(true);
        m_sw_limiter->setToolTip(
            "Look-ahead limiter: catches peaks at −0.3 dBFS (adds 1 ms latency)");
        m_sw_limiter->set_accent(Theme::generic_accent());
        auto* row = new QWidget;
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(2);
        auto* cap = new QLabel("Limit");
        cap->setObjectName("toggleCaption");
        cap->setAlignment(Qt::AlignHCenter);
        rl->addWidget(m_sw_limiter, 0, Qt::AlignHCenter);
        rl->addWidget(cap);
        static_cast<QVBoxLayout*>(m_output_card->layout())
            ->insertWidget(2, row, 0, Qt::AlignHCenter);
    }

    // Keyboard shortcut targets per card; keys are (re)assigned left to right
    // on every strip rebuild so they always match the visual order.
    m_fx_toggles = {
        {&m_engine->tuner(), {m_sw_tuner_enable, nullptr}},
        {&m_engine->gate(), {m_sw_gate_enable, nullptr}},
        {&m_engine->comp(), {m_sw_comp_enable, nullptr}},
        {&m_engine->wah(), {m_sw_wah_enable, nullptr}},
        {&m_engine->flanger(), {m_sw_flanger_enable, m_sw_flanger_stereo}},
        {&m_engine->phaser(), {m_sw_phaser_enable, m_sw_phaser_stereo}},
        {&m_engine->nam(), {m_sw_nam_enable, nullptr}},
        {&m_engine->eq(), {m_sw_eq_enable, nullptr}},
        {&m_engine->chorus(), {m_sw_chorus_enable, m_sw_chorus_stereo}},
        {&m_engine->tremolo(), {m_sw_tremolo_enable, m_sw_tremolo_stereo}},
        {&m_engine->delay(), {m_sw_delay_enable, m_sw_delay_stereo}},
        {&m_engine->reverb(), {m_sw_reverb_enable, nullptr}},
        {&m_engine->ir(), {m_sw_ir_enable, nullptr}},
        {&m_engine->drive(), {m_sw_drive_enable, nullptr}},
        {&m_engine->doubler(), {m_sw_doubler_enable, nullptr}},
        {&m_engine->volume(), {m_sw_volume_enable, nullptr}},
        {&m_engine->transpose(), {m_sw_transpose_enable, nullptr}},
    };

    for (const auto& [fx, toggles] : m_fx_toggles) {
        connect(toggles.enable, &ToggleSwitch::toggled, this, [this, fx](bool enabled) {
            update_card_bypass_state(fx, enabled);
        });
        update_card_bypass_state(fx, toggles.enable->isChecked());
    }

    // Drag sources + context menus: every card. Drop target: the strip itself.
    for (const auto& [fx, card] : m_fx_cols) {
        card->installEventFilter(this);
    }
    m_input_card->installEventFilter(this); // context menu only — not draggable
    m_output_card->installEventFilter(this);
    m_lbl_nam_origin->installEventFilter(this); // T3K mark click
    m_lbl_ir_origin->installEventFilter(this);
    m_strip->setAcceptDrops(true);
    m_strip->installEventFilter(this);

    rebuild_effect_strip();
    m_midi_menu = midi_menu;

    scroll->setWidget(m_strip);

    // Rack row: input column | effect strip | output column.
    auto* rack_row = new QHBoxLayout;
    rack_row->setSpacing(6);
    rack_row->addWidget(m_input_card);
    rack_row->addWidget(scroll, 1);
    rack_row->addWidget(m_output_card);
    root_layout->addLayout(rack_row, 1);

    // ---- Library dock (left of the rig; closable, movable, remembers state) ----
    m_library_tabs = new LibraryDock(*m_library_index, *m_tone3000);
    m_library = m_library_tabs->local();
    m_t3k_panel = m_library_tabs->tone3000();
    connect(m_library, &LibraryPanel::activated, this, &MainWindow::load_library_entry);
    connect(m_t3k_panel, &Tone3000Panel::load_requested, this, &MainWindow::load_library_entry);
    connect(m_tone3000, &Tone3000Client::auth_finished, this, [this](bool ok, const QString&, int) {
        if (ok) {
            m_library_dock->show();
            m_library_tabs->set_tab(LibraryDock::TONE3000);
        }
    });

    m_library_dock = new QDockWidget("Library", this);
    m_library_dock->setObjectName("libraryDock"); // saveState() needs a name
    m_library_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_library_dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                                QDockWidget::DockWidgetFloatable);
    m_library_dock->setWidget(m_library_tabs);
    addDockWidget(Qt::LeftDockWidgetArea, m_library_dock);
    resizeDocks({m_library_dock}, {320}, Qt::Horizontal);

    {
        QAction* show = m_library_dock->toggleViewAction();
        show->setText("Show Library");
        show->setShortcut(QKeySequence("Ctrl+L"));
        library_menu->addAction(show);

        auto* find = library_menu->addAction("Find in Library");
        find->setShortcut(QKeySequence::Find); // Ctrl+F
        connect(find, &QAction::triggered, this, [this] {
            m_library_dock->show();
            m_library_tabs->set_tab(LibraryDock::LOCAL);
            m_library->focus_search();
        });
        auto* t3k = library_menu->addAction("Browse TONE3000…");
        t3k->setIcon(
            QIcon(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(14, Qt::SmoothTransformation)));
        connect(t3k, &QAction::triggered, this, [this] {
            m_library_dock->show();
            m_library_tabs->set_tab(LibraryDock::TONE3000);
            if (m_tone3000->is_signed_in()) {
                m_t3k_panel->browse_catalog();
            }
        });
        library_menu->addSeparator();
        connect(library_menu->addAction("Folders…"), &QAction::triggered, m_library,
                &LibraryPanel::edit_folders);
        connect(library_menu->addAction("Rescan"), &QAction::triggered, m_library_index,
                &LibraryIndex::rescan);
        library_menu->addSeparator();
        connect(library_menu->addAction("Import Model…"), &QAction::triggered, this,
                &MainWindow::on_import_nam);
        connect(library_menu->addAction("Import Impulse Response…"), &QAction::triggered, this,
                &MainWindow::on_import_ir);
    }
}

void MainWindow::make_hero_controls(QWidget* card, LibraryEntry::Kind kind, QLabel*& name_out,
                                    QLabel*& meta_out, QLabel*& origin_out) {
    auto* lay = row_layout(card);
    const bool model = kind == LibraryEntry::Kind::MODEL;

    // ‹ › cycle the library section in its current visible order.
    auto arrow = [&](const QString& glyph, int step, const QString& tip) {
        auto* b = new QToolButton;
        b->setObjectName("heroArrow");
        b->setText(glyph);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [this, kind, step] {
            step_library(kind, step);
        });
        return b;
    };
    auto* prev =
        arrow("‹", -1, model ? "Previous model in the library" : "Previous IR in the library");
    auto* next = arrow("›", +1, model ? "Next model in the library" : "Next IR in the library");

    auto* browse = new QPushButton("Browse");
    browse->setToolTip(model ? "Show the model list in the Library"
                             : "Show the IR list in the Library");
    browse->setFocusPolicy(Qt::NoFocus);
    connect(browse, &QPushButton::clicked, this, [this, kind] {
        m_library_dock->show();
        m_library_tabs->set_tab(LibraryDock::LOCAL);
        m_library->show_section(kind);
    });

    // TONE3000 origin marker (the compact T3K mark), shown only when the
    // loaded file carries a download sidecar.
    origin_out = new QLabel;
    origin_out->setObjectName("heroOrigin");
    origin_out->setPixmap(
        QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(13, Qt::SmoothTransformation));
    origin_out->setCursor(Qt::PointingHandCursor);
    origin_out->hide();

    name_out = new QLabel(model ? "No model loaded" : "No impulse response loaded");
    name_out->setObjectName("fileStatus");
    meta_out = new QLabel;
    meta_out->setObjectName("fileMeta");

    // Before the trailing stretch: grip, name, toggle, stereo slot = 4 items.
    int i = 4;
    lay->insertWidget(i++, prev, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, next, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, browse, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, origin_out, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, name_out, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, meta_out, 1, Qt::AlignVCenter);
}

void MainWindow::layout_strip(const std::vector<EffectBase*>& order) {
    // Empty the layout: the stretch is recreated below, cards are reused
    // (they stay children of the strip while out of the layout).
    while (QLayoutItem* item = m_strip_lay->takeAt(0)) {
        delete item;
    }

    for (auto* fx : order) {
        auto it = m_fx_cols.find(fx);
        if (it != m_fx_cols.end()) {
            m_strip_lay->addWidget(it->second);
        }
    }
    m_strip_lay->addStretch();
    m_strip_lay->activate(); // fresh geometry now — the drag preview reads card centres
}

void MainWindow::rebuild_effect_strip() {
    layout_strip(m_engine->chain());

    // Digit/QWE shortcuts always match the visual top-to-bottom order.
    m_shortcuts.clear();
    for (auto* fx : m_engine->chain()) {
        auto tg = m_fx_toggles.find(fx);
        if (tg != m_fx_toggles.end()) {
            m_shortcuts.add_effect(tg->second.enable, tg->second.stereo);
        }
    }
}

void MainWindow::update_card_bypass_state(EffectBase* effect, bool enabled) {
    const auto it = m_fx_cols.find(effect);
    if (it == m_fx_cols.end()) {
        return;
    }

    QWidget* card = it->second;
    if (enabled) {
        card->setGraphicsEffect(nullptr);
        return;
    }
    auto* opacity = new QGraphicsOpacityEffect(card);
    opacity->setOpacity(0.48);
    card->setGraphicsEffect(opacity);
}

void MainWindow::apply_chain_order(const QStringList& ids) {
    m_engine->set_chain_order(to_std_strings(ids));
    rebuild_effect_strip();
}

// ---------------------------------------------------------------------------
// Card drag & drop
// ---------------------------------------------------------------------------

void MainWindow::start_card_drag(QWidget* card) {
    EffectBase* fx = nullptr;
    for (const auto& [e, w] : m_fx_cols) {
        if (w == card) {
            fx = e;
            break;
        }
    }
    if (!fx) {
        return;
    }
    const std::string id = m_engine->id_for_effect(fx);
    if (id.empty()) {
        return;
    }

    m_drag_id = id;
    m_preview_ids = m_engine->chain_ids();

    auto* mime = new QMimeData;
    mime->setData(K_EFFECT_MIME, QByteArray::fromStdString(id));
    auto* drag = new QDrag(card);
    drag->setMimeData(mime);
    drag->setPixmap(card->grab()); // grab before dimming
    drag->setHotSpot(m_drag_start_pos);

    // Dim the in-strip card: it marks its landing slot in the live preview
    // while the full-opacity pixmap follows the cursor.
    auto* dim = new QGraphicsOpacityEffect(card);
    dim->setOpacity(0.35);
    card->setGraphicsEffect(dim);

    drag->exec(Qt::MoveAction);

    // Drop committed inside the event filter; a cancelled drag just falls
    // back to the engine order here.
    auto toggles = m_fx_toggles.find(fx);
    update_card_bypass_state(fx,
                             toggles != m_fx_toggles.end() && toggles->second.enable->isChecked());
    m_drag_id.clear();
    rebuild_effect_strip();
}

void MainWindow::update_drag_preview(const QPoint& pos) {
    if (m_drag_id.empty()) {
        return;
    }

    std::vector<std::string> ids = m_preview_ids;
    ids.erase(std::remove(ids.begin(), ids.end(), m_drag_id), ids.end());
    if (ids.empty()) {
        return;
    }

    // Insert after every card whose centre lies above the cursor.
    int insert = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto it = m_fx_cols.find(m_engine->effect_by_id(ids[i]));
        if (it != m_fx_cols.end() && pos.y() > it->second->geometry().center().y()) {
            insert = static_cast<int>(i) + 1;
        }
    }
    ids.insert(ids.begin() + insert, m_drag_id);

    if (ids == m_preview_ids) {
        return;
    }
    m_preview_ids = ids;

    std::vector<EffectBase*> order;
    order.reserve(ids.size());
    for (const auto& s : ids) {
        if (EffectBase* e = m_engine->effect_by_id(s)) {
            order.push_back(e);
        }
    }
    layout_strip(order);
}

void MainWindow::commit_drag_preview() {
    if (m_drag_id.empty()) {
        return;
    }
    QStringList qids;
    for (const auto& s : m_preview_ids) {
        qids << QString::fromStdString(s);
    }
    apply_chain_order(qids);
    schedule_session_save();
}

void MainWindow::cancel_drag_preview() {
    if (m_drag_id.empty()) {
        return;
    }
    m_preview_ids = m_engine->chain_ids();
    rebuild_effect_strip();
}

void MainWindow::reset_card_to_defaults(QWidget* card) {
    // The enable toggle keeps its state — resetting restores the effect's
    // sound, it doesn't kick it in or out of the chain.
    ToggleSwitch* enable = nullptr;
    for (const auto& [fx, w] : m_fx_cols) {
        if (w == card) {
            auto tg = m_fx_toggles.find(fx);
            if (tg != m_fx_toggles.end()) {
                enable = tg->second.enable;
            }
            break;
        }
    }

    // A parameter belongs to this card iff its widget lives inside it — the
    // registry needs no extra per-effect bookkeeping. Widget signals update
    // the engine and schedule the session save.
    for (const auto& p : m_params) {
        QWidget* w = p.knob ? static_cast<QWidget*>(p.knob) : static_cast<QWidget*>(p.toggle);
        if (!card->isAncestorOf(w)) {
            continue;
        }
        if (p.toggle && p.toggle == enable) {
            continue;
        }
        if (p.knob) {
            p.knob->set_value(p.knob_default);
        } else {
            p.toggle->setChecked(p.toggle_default);
        }
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            // Reorder drags start on the ≡ grip only. The grip label ignores
            // mouse events, so the press propagates to the card — where this
            // filter sees it with the position already in card coordinates.
            auto* me = static_cast<QMouseEvent*>(ev);
            auto* card = qobject_cast<QWidget*>(obj);
            auto it = card ? m_card_grips.find(card) : m_card_grips.end();
            if (it != m_card_grips.end() && me->button() == Qt::LeftButton &&
                it->second->geometry().contains(me->pos())) {
                m_drag_card = card;
                m_drag_start_pos = me->pos();
            }
            break;
        }
        case QEvent::MouseMove: {
            if (!m_drag_card || obj != m_drag_card) {
                break;
            }
            auto* me = static_cast<QMouseEvent*>(ev);
            if (!(me->buttons() & Qt::LeftButton)) {
                m_drag_card = nullptr;
                break;
            }
            if ((me->pos() - m_drag_start_pos).manhattanLength() <
                QApplication::startDragDistance()) {
                break;
            }
            QWidget* card = m_drag_card;
            m_drag_card = nullptr;
            start_card_drag(card);
            return true;
        }
        case QEvent::MouseButtonRelease:
            if (obj == m_drag_card) {
                m_drag_card = nullptr;
            }
            if (obj == m_lbl_nam_origin || obj == m_lbl_ir_origin) {
                // T3K mark on a hero row: open that tone in the TONE3000 tab.
                const int tone_id = static_cast<QWidget*>(obj)->property("toneId").toInt();
                if (tone_id > 0) {
                    m_library_dock->show();
                    m_library_tabs->set_tab(LibraryDock::TONE3000);
                    m_t3k_panel->show_tone(tone_id);
                }
                return true;
            }
            break;
        case QEvent::ContextMenu: {
            // Right-click on a card (knobs keep their own menu): per-effect reset.
            auto* card = qobject_cast<QWidget*>(obj);
            if (card && obj != m_strip && card->property("fxCard").toBool()) {
                auto* ce = static_cast<QContextMenuEvent*>(ev);
                const QString name = card->property("displayName").toString();
                QMenu menu(card);

                // Hero rows: Import (file dialog) and Unload live here — the
                // library is the primary path, so they left the row itself.
                const bool is_amp = card == m_fx_cols[&m_engine->nam()];
                const bool is_cab = card == m_fx_cols[&m_engine->ir()];
                if (is_amp || is_cab) {
                    connect(menu.addAction(is_amp ? "Import Model…" : "Import Impulse Response…"),
                            &QAction::triggered, this,
                            is_amp ? &MainWindow::on_import_nam : &MainWindow::on_import_ir);
                    QAction* unload =
                        menu.addAction(is_amp ? "Unload Model" : "Unload Impulse Response");
                    unload->setEnabled(is_amp ? m_engine->is_model_loaded()
                                              : m_engine->is_ir_loaded());
                    connect(unload, &QAction::triggered, this,
                            is_amp ? &MainWindow::on_unload_nam : &MainWindow::on_unload_ir);
                    menu.addSeparator();
                }

                QAction* act = menu.addAction("Reset " + name + " to defaults");

                // A right-click on a switch: MIDI Learn for that switch.
                for (QWidget* w = card->childAt(card->mapFromGlobal(ce->globalPos()));
                     w && w != card; w = w->parentWidget()) {
                    if (const MidiTarget* t = m_midi_mapper->target_for(w); t && t->toggle) {
                        menu.addSeparator();
                        add_learn_entries(t->id, &menu);
                        break;
                    }
                }

                if (menu.exec(ce->globalPos()) == act) {
                    reset_card_to_defaults(card);
                }
                return true;
            }
            break;
        }
        case QEvent::DragEnter:
            if (obj == m_strip) {
                auto* de = static_cast<QDragEnterEvent*>(ev);
                if (de->mimeData()->hasFormat(K_EFFECT_MIME) || has_loadable_urls(de->mimeData())) {
                    de->acceptProposedAction(); // files: the strip is the drop
                    return true; // target Qt found, so accept here
                }
            }
            break;
        case QEvent::DragMove:
            if (obj == m_strip) {
                auto* de = static_cast<QDragMoveEvent*>(ev);
                if (de->mimeData()->hasFormat(K_EFFECT_MIME)) {
                    de->acceptProposedAction();
                    update_drag_preview(de->position().toPoint());
                    return true;
                }
                if (has_loadable_urls(de->mimeData())) {
                    de->acceptProposedAction();
                    return true;
                }
            }
            break;
        case QEvent::DragLeave:
            if (obj == m_strip) {
                cancel_drag_preview();
            }
            break;
        case QEvent::Drop:
            if (obj == m_strip) {
                auto* de = static_cast<QDropEvent*>(ev);
                if (de->mimeData()->hasFormat(K_EFFECT_MIME)) {
                    de->acceptProposedAction();
                    commit_drag_preview();
                    return true;
                }
                if (de->mimeData()->hasUrls()) {
                    dropEvent(de);
                    return true;
                }
            }
            break;
        default:
            break;
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::setup_audio() {
    QSettings s;
    const QString saved_in = s.value("audio/inputDeviceId").toString();
    const QString saved_out = s.value("audio/outputDeviceId").toString();

    bool started = false;
    if (!saved_in.isEmpty() || !saved_out.isEmpty()) {
        started = m_engine->start_with_devices(saved_in.toStdString(), saved_out.toStdString());
    }
    if (!started) {
        started = m_engine->start("NAMPlayer");
    }

    m_audio_ok = started;
    if (started) {
        m_audio_bar->refresh();
        if (m_engine->supports_port_routing()) {
            restore_connections();
        }
    } else {
        m_audio_bar->set_error("Not connected");
    }
}

void MainWindow::restart_audio_with_current_devices() {
    const QString in_id = m_audio_bar->input_device_id();
    const QString out_id = m_audio_bar->output_device_id();

    m_audio_ok = m_engine->start_with_devices(in_id.toStdString(), out_id.toStdString());
    if (m_audio_ok) {
        m_audio_bar->refresh();
        if (m_engine->supports_port_routing()) {
            restore_connections();
        }
    } else {
        m_audio_bar->set_error("Not connected");
    }
}

// ---------------------------------------------------------------------------
// File slots
// ---------------------------------------------------------------------------

void MainWindow::on_import_nam() {
    const QString path = QFileDialog::getOpenFileName(nullptr, "Import NAM Model",
                                                      QSettings().value("files/lastNAM").toString(),
                                                      "NAM Files (*.nam);;All Files (*)");
    if (!path.isEmpty()) {
        load_model_file(path);
    }
}

void MainWindow::on_unload_nam() {
    m_engine->unload_model();
    QSettings().remove("files/lastNAM");
    m_library->set_loaded_path(LibraryEntry::Kind::MODEL, {});
    update_hero_cards();
}

void MainWindow::on_import_ir() {
    const QString path = QFileDialog::getOpenFileName(nullptr, "Import Impulse Response",
                                                      QSettings().value("files/lastIR").toString(),
                                                      "WAV Files (*.wav);;All Files (*)");
    if (!path.isEmpty()) {
        load_ir_file(path);
    }
}

void MainWindow::on_unload_ir() {
    m_engine->unload_ir();
    QSettings().remove("files/lastIR");
    m_library->set_loaded_path(LibraryEntry::Kind::IR, {});
    update_hero_cards();
}

// ---------------------------------------------------------------------------
// Loading — shared by dialogs, the library, drops, session restore, presets
// ---------------------------------------------------------------------------

bool MainWindow::load_model_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    // Same file again (preset reload, arrow wrap-around on a one-item list):
    // skip the synchronous rebuild + prewarm and the audible state swap.
    if (!(m_engine->is_model_loaded() && QString::fromStdString(m_engine->model_path()) == clean)) {
        if (!m_engine->load_model(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Model",
                                      "Failed to load NAM model:\n" + clean + "\n\n" +
                                          QString::fromStdString(m_engine->model_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastNAM", clean);
    m_library->note_used(clean);
    m_library->set_loaded_path(LibraryEntry::Kind::MODEL, clean);
    update_hero_cards();
    return true;
}

bool MainWindow::load_ir_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    if (!(m_engine->is_ir_loaded() && QString::fromStdString(m_engine->ir_path()) == clean)) {
        if (!m_engine->load_ir(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Impulse Response",
                                      "Failed to load IR:\n" + clean + "\n\n" +
                                          QString::fromStdString(m_engine->ir_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastIR", clean);
    m_library->note_used(clean);
    m_library->set_loaded_path(LibraryEntry::Kind::IR, clean);
    update_hero_cards();
    return true;
}

bool MainWindow::load_irb_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    if (!(m_engine->is_irb_loaded() && QString::fromStdString(m_engine->ir_path_b()) == clean)) {
        if (!m_engine->load_irb(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Impulse Response",
                                      "Failed to load IR (slot B):\n" + clean + "\n\n" +
                                          QString::fromStdString(m_engine->ir_b_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastIRB", clean);
    m_library->note_used(clean);
    update_hero_cards();
    return true;
}

void MainWindow::load_library_entry(LibraryEntry::Kind kind, const QString& path) {
    switch (kind) {
        case LibraryEntry::Kind::MODEL:
            load_model_file(path);
            break;
        case LibraryEntry::Kind::IR:
            load_ir_file(path);
            break;
        case LibraryEntry::Kind::PRESET:
            load_preset_file(path);
            break;
    }
}

void MainWindow::step_library(LibraryEntry::Kind kind, int step) {
    const QString current = kind == LibraryEntry::Kind::MODEL
                                ? QString::fromStdString(m_engine->model_path())
                                : QString::fromStdString(m_engine->ir_path());
    const QString next = m_library->neighbour(kind, current, step);
    if (!next.isEmpty() && next != QDir::cleanPath(current)) {
        load_library_entry(kind, next);
    }
}

bool MainWindow::has_loadable_urls(const QMimeData* mime) {
    if (!mime || !mime->hasUrls()) {
        return false;
    }
    for (const QUrl& u : mime->urls()) {
        bool ok = false;
        LibraryIndex::kind_for_path(u.toLocalFile(), &ok);
        if (u.isLocalFile() && ok) {
            return true;
        }
    }
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* ev) {
    if (has_loadable_urls(ev->mimeData())) {
        ev->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* ev) {
    if (!ev->mimeData()->hasUrls()) {
        return;
    }
    QStringList paths;
    for (const QUrl& u : ev->mimeData()->urls()) {
        if (u.isLocalFile()) {
            paths << u.toLocalFile();
        }
    }
    if (!has_loadable_urls(ev->mimeData())) {
        return;
    }
    ev->acceptProposedAction();
    open_paths(paths);
}

void MainWindow::open_paths(const QStringList& paths) {
    // One file per kind: the last model, the last IR, the last preset win.
    // The preset goes first so a model/IR given alongside overrides its own.
    QString model, ir, preset;
    for (const QString& p : paths) {
        bool ok = false;
        switch (LibraryIndex::kind_for_path(p, &ok)) {
            case LibraryEntry::Kind::MODEL:
                if (ok) {
                    model = p;
                }
                break;
            case LibraryEntry::Kind::IR:
                if (ok) {
                    ir = p;
                }
                break;
            case LibraryEntry::Kind::PRESET:
                if (ok) {
                    preset = p;
                }
                break;
        }
    }
    if (!preset.isEmpty()) {
        load_preset_file(preset);
    }
    if (!model.isEmpty()) {
        load_model_file(model);
    }
    if (!ir.isEmpty()) {
        load_ir_file(ir);
    }
}

// ---------------------------------------------------------------------------
// Parameter registry
// ---------------------------------------------------------------------------

void MainWindow::knob_param(const QString& group, const QString& key, KnobWidget* knob,
                            const std::function<void(double)>& apply) {
    connect(knob, &KnobWidget::value_changed, this, [this, apply](double v) {
        apply(v);
        schedule_session_save();
    });
    m_params.push_back({group, key, knob, nullptr, knob->default_value(), false});
    const QString card = target_label_for(knob, group, key);
    m_midi_mapper->add_knob("param:" + (group.isEmpty() ? key : group + "/" + key), card,
                            key == "syncNote" ? knob->label() + " (note)" : knob->label(), knob);
}

void MainWindow::toggle_param(const QString& group, const QString& key, ToggleSwitch* sw,
                              const std::function<void(bool)>& apply) {
    connect(sw, &ToggleSwitch::toggled, this, [this, apply](bool b) {
        apply(b);
        schedule_session_save();
    });
    m_params.push_back({group, key, nullptr, sw, 0.0, sw->isChecked()});
    const QString label = key == "stereo" ? "Stereo" : key == "sync" ? "Sync" : "On / Off";
    m_midi_mapper->add_toggle("param:" + (group.isEmpty() ? key : group + "/" + key),
                              target_label_for(sw, group, key), label, sw);
}

// ---------------------------------------------------------------------------
// MIDI
// ---------------------------------------------------------------------------

QString MainWindow::target_label_for(QWidget* w, const QString& group, const QString& key) const {
    // The card's display name ("Delay", "Input") groups the target.
    for (QWidget* p = w; p; p = p->parentWidget()) {
        if (p->property("displayName").isValid()) {
            return p->property("displayName").toString();
        }
    }
    return group.isEmpty() ? key : group;
}

void MainWindow::setup_midi(QMenu* midi_menu) {
    // ---- Actions ----
    for (int i = 0; i < PresetBar::K_SLOT_COUNT; ++i) {
        m_midi_mapper->add_action(QString("action:slot%1").arg(i + 1), "Presets",
                                  QString("Slot %1").arg(i + 1), [this, i] {
                                      m_preset_bar->recall_slot(i);
                                  });
    }
    m_midi_mapper->add_action("action:presetNext", "Presets", "Next preset", [this] {
        step_preset(+1);
    });
    m_midi_mapper->add_action("action:presetPrev", "Presets", "Previous preset", [this] {
        step_preset(-1);
    });
    m_midi_mapper->add_action("action:abToggle", "Presets", "Switch A / B", [this] {
        switch_ab(1 - m_ab_active);
    });
    m_midi_mapper->add_action("action:metronome", "Metronome", "Start / stop", [this] {
        m_tempo_bar->toggle_play();
    });
    m_midi_mapper->add_action("action:tap", "Tempo", "Tap", [this] {
        m_tempo_bar->tap_tempo();
    });

    // ---- Learn entries in every context menu ----
    KnobWidget::set_menu_hook([this](KnobWidget* k, QMenu* menu) {
        if (const MidiTarget* t = m_midi_mapper->target_for(k)) {
            menu->addSeparator();
            add_learn_entries(t->id, menu);
        }
    });
    auto hook = [this](const QString& id, QMenu* menu) {
        add_learn_entries(id, menu);
    };
    m_preset_bar->set_learn_menu_hook(hook);
    m_tempo_bar->set_learn_menu_hook(hook);

    m_learn_esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_learn_esc->setEnabled(false);
    connect(m_learn_esc, &QShortcut::activated, m_midi_mapper, &MidiMapper::cancel_learn);
    connect(m_midi_mapper, &MidiMapper::learn_changed, this, &MainWindow::on_learn_changed);

    // ---- Input ----
    m_midi = MidiInput::create(this);
    connect(m_midi.get(), &MidiInput::message_received, m_midi_mapper, &MidiMapper::handle);
    m_midi_ok = m_midi->start();
    connect(m_midi_mapper, &MidiMapper::tempo_from_clock, m_tempo_bar, &TempoBar::set_bpm);

    // Status bar: a light that blinks on incoming MIDI.
    m_lbl_midi = new QLabel;
    m_lbl_midi->setObjectName("statusItem");
    m_lbl_midi->setTextFormat(Qt::RichText);
    statusBar()->addWidget(m_lbl_midi);
    m_midi_blink = new QTimer(this);
    m_midi_blink->setSingleShot(true);
    m_midi_blink->setInterval(120);
    auto show_light = [this](bool lit) {
        const QColor c = !m_midi_ok ? Theme::track()
                         : lit      ? Theme::meter_mid()
                                    : Theme::meter_low();
        m_lbl_midi->setText(QString("<span style=\"color:%1\">●</span> MIDI").arg(c.name()));
    };
    connect(m_midi_blink, &QTimer::timeout, this, [show_light] {
        show_light(false);
    });
    connect(m_midi_mapper, &MidiMapper::activity, this, [this, show_light](const QString& what) {
        show_light(true);
        m_midi_blink->start();
        m_lbl_midi->setToolTip("Last MIDI message: " + what);
    });
    show_light(false);
    m_lbl_midi->setToolTip(
        m_midi_ok ? QString("MIDI input (%1) — MIDI → Mappings…").arg(m_midi->backend_name())
                  : QString("MIDI is not available on this system"));

    // ---- Menu ----
    connect(midi_menu->addAction("Mappings…"), &QAction::triggered, this, [this] {
        MidiMappingsDialog dlg(*m_midi_mapper, m_midi_ok ? m_midi.get() : nullptr, this);
        dlg.exec();
    });
    QMenu* devices = midi_menu->addMenu("Devices");
    connect(devices, &QMenu::aboutToShow, this, [this, devices] {
        devices->clear();
        const QStringList list = m_midi_ok ? m_midi->devices() : QStringList();
        if (list.isEmpty()) {
            devices->addAction(m_midi_ok ? "No MIDI devices found" : "MIDI not available")
                ->setEnabled(false);
        }
        for (const QString& d : list) {
            QAction* a = devices->addAction(d);
            a->setCheckable(true);
            a->setChecked(m_midi->is_enabled(d));
            connect(a, &QAction::toggled, this, [this, d](bool on) {
                m_midi->set_enabled(d, on);
            });
        }
    });
    QAction* clock = midi_menu->addAction("Follow MIDI Clock");
    clock->setCheckable(true);
    connect(midi_menu, &QMenu::aboutToShow, clock, [this, clock] {
        clock->setChecked(m_midi_mapper->follow_clock());
    });
    connect(clock, &QAction::toggled, m_midi_mapper, &MidiMapper::set_follow_clock);
}

void MainWindow::add_learn_entries(const QString& target_id, QMenu* menu) {
    const MidiTarget* t = m_midi_mapper->target(target_id);
    if (!t) {
        return;
    }
    connect(menu->addAction("MIDI Learn"), &QAction::triggered, this, [this, target_id] {
        start_learn(target_id);
    });
    if (m_midi_mapper->has_mapping(target_id)) {
        connect(menu->addAction("Forget MIDI (" + m_midi_mapper->mapping_summary(target_id) + ")"),
                &QAction::triggered, this, [this, target_id] {
                    m_midi_mapper->forget(target_id);
                });
    }
}

QWidget* MainWindow::widget_for_target(const QString& target_id) const {
    if (const MidiTarget* t = m_midi_mapper->target(target_id)) {
        if (t->knob) {
            return t->knob;
        }
        if (t->toggle) {
            return t->toggle;
        }
    }
    if (QWidget* w = m_preset_bar->action_widget(target_id)) {
        return w;
    }
    return m_tempo_bar->action_widget(target_id);
}

void MainWindow::start_learn(const QString& target_id) {
    if (!m_midi_ok) {
        QMessageBox::information(this, "MIDI Learn", "MIDI input is not available on this system.");
        return;
    }
    m_midi_mapper->learn(target_id);
}

void MainWindow::on_learn_changed() {
    // Drop the previous highlight.
    if (m_learn_widget) {
        m_learn_widget->setGraphicsEffect(nullptr);
        m_learn_widget = nullptr;
    }
    const QString target = m_midi_mapper->learning();
    m_learn_esc->setEnabled(!target.isEmpty());
    if (target.isEmpty()) {
        if (!m_learn_target.isEmpty()) {
            const MidiTarget* t = m_midi_mapper->target(m_learn_target);
            const QString summary = m_midi_mapper->mapping_summary(m_learn_target);
            statusBar()->showMessage(
                summary.isEmpty() || !t
                    ? QString("MIDI Learn cancelled")
                    : QString("MIDI: %1 → %2 · %3").arg(summary, t->group, t->label),
                4000);
        }
        m_learn_target.clear();
        return;
    }
    m_learn_target = target;
    const MidiTarget* t = m_midi_mapper->target(target);
    statusBar()->showMessage(
        QString("MIDI Learn: move a control on your MIDI device for %1 · %2 — Esc cancels")
            .arg(t ? t->group : QString(), t ? t->label : target));
    if (QWidget* w = widget_for_target(target)) {
        auto* glow = new QGraphicsDropShadowEffect(w);
        glow->setColor(Theme::meter_mid());
        glow->setBlurRadius(22);
        glow->setOffset(0, 0);
        w->setGraphicsEffect(glow);
        m_learn_widget = w;
    }
}

// ---------------------------------------------------------------------------
// Tempo sync
// ---------------------------------------------------------------------------

void MainWindow::add_tempo_sync(QWidget* card, KnobWidget* free_knob, const QString& label,
                                bool is_time, int default_note, double min, double max,
                                std::function<void(double)> push) {
    auto* lay = row_layout(card);

    // Note knob in the free knob's place, shown while synced.
    auto* note =
        new KnobWidget(label, 0, static_cast<double>(TempoSync::K_NOTES.size() - 1), default_note);
    note->set_snap(1.0);
    note->set_value_formatter([](double v) {
        return TempoSync::label(v);
    });
    note->setToolTip(
        is_time ? QString("%1 as a note value at the global tempo (capped at %2 ms)")
                      .arg(label)
                      .arg(max)
                : QString("%1: one LFO cycle per note value at the global tempo").arg(label));
    note->hide();
    lay->insertWidget(lay->indexOf(free_knob) + 1, note, 0, Qt::AlignVCenter);

    // Sync switch at the end of the row (knob columns stay aligned).
    auto* sw = new ToggleSwitch;
    sw->setCheckable(true);
    sw->setToolTip("Sync to the global tempo");
    auto* slot = new QWidget;
    auto* sl = new QHBoxLayout(slot);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(6);
    auto* cap = new QLabel("Sync");
    cap->setObjectName("toggleCaption");
    sl->addWidget(sw, 0, Qt::AlignVCenter);
    sl->addWidget(cap, 0, Qt::AlignVCenter);
    lay->insertWidget(lay->count() - 1, slot, 0, Qt::AlignVCenter); // before the stretch

    connect(sw, &ToggleSwitch::toggled, this, [free_knob, note](bool on) {
        free_knob->setVisible(!on);
        note->setVisible(on);
    });
    m_synced.push_back({free_knob, note, sw, is_time, min, max, std::move(push)});
}

MainWindow::SyncedParam& MainWindow::sync_of(KnobWidget* free_knob) {
    for (auto& s : m_synced) {
        if (s.free_knob == free_knob) {
            return s;
        }
    }
    Q_UNREACHABLE();
}

void MainWindow::push_synced(KnobWidget* free_knob) {
    const SyncedParam& s = sync_of(free_knob);
    if (!s.sync->isChecked()) {
        s.push(s.free_knob->value());
        return;
    }
    const double idx = s.note_knob->value();
    const double v = s.is_time ? TempoSync::note_ms(m_bpm, idx) : TempoSync::note_hz(m_bpm, idx);
    s.push(std::clamp(v, s.min, s.max));
}

void MainWindow::set_tempo(double bpm) {
    m_bpm = bpm;
    for (auto& s : m_synced) {
        if (s.sync->isChecked()) {
            push_synced(s.free_knob);
        }
    }
}

// ---------------------------------------------------------------------------
// Session state — the registry serialized to QSettings under "session/"
// ---------------------------------------------------------------------------

void MainWindow::schedule_session_save() {
    if (m_session_save_timer) {
        m_session_save_timer->start();
    }
    if (m_preset_state_timer) {
        m_preset_state_timer->start();
    }
}

void MainWindow::save_session_state() {
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : m_params) {
        const QString path = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        s.setValue(path, p.knob ? QVariant(p.knob->value()) : QVariant(p.toggle->isChecked()));
    }
    QStringList order;
    for (const auto& id : m_engine->chain_ids()) {
        order << QString::fromStdString(id);
    }
    s.setValue("effectOrder", order);
    // The current preset and the state it was loaded with, so the name and
    // its `*` come back exactly after a restart.
    s.setValue("presetPath", m_preset_path);
    s.setValue(
        "presetBaseline",
        m_preset_path.isEmpty()
            ? QString()
            : QString::fromUtf8(QJsonDocument(m_preset_baseline).toJson(QJsonDocument::Compact)));
    s.endGroup();
}

void MainWindow::restore_session_state() {
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : m_params) {
        const QString path = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        if (!s.contains(path)) {
            continue; // first run: widgets keep their defaults
        }
        if (p.knob) {
            p.knob->set_value(s.value(path, p.knob_default).toDouble());
        } else {
            p.toggle->setChecked(s.value(path, p.toggle_default).toBool());
        }
    }
    const QStringList order = s.value("effectOrder").toStringList();
    if (!order.isEmpty()) {
        apply_chain_order(order);
    }
    s.endGroup();
}

// Every user-facing parameter, registered exactly once. Runs after setupUI so
// the captured widget states are the defaults (they mirror the engine's).
// Group/key names are the v1 preset schema; an empty group means the key sits
// at the JSON root.
void MainWindow::register_parameters() {
    auto* e = m_engine.get();
    // Time-based knobs: every change of free value, sync toggle or note value
    // re-pushes the effective value (see pushSynced).
    auto synced = [this](KnobWidget* k) {
        return [this, k](double) {
            push_synced(k);
        };
    };

    knob_param("", "inputGain", m_knob_input_gain, [e](double v) {
        e->set_input_gain(static_cast<float>(v));
    });
    knob_param("", "outputGain", m_knob_output_gain, [e](double v) {
        e->set_output_gain(static_cast<float>(v));
    });

    toggle_param("tuner", "enabled", m_sw_tuner_enable, [e](bool b) {
        e->tuner().set_enabled(b);
    });
    m_params.back().in_preset = false; // a practice tool, not part of the sound

    toggle_param("transpose", "enabled", m_sw_transpose_enable, [e](bool b) {
        e->transpose().set_enabled(b);
    });

    toggle_param("noiseGate", "enabled", m_sw_gate_enable, [e](bool b) {
        e->gate().set_enabled(b);
    });
    knob_param("noiseGate", "threshold", m_knob_gate_thresh, [e](double v) {
        e->gate().set_threshold(static_cast<float>(v));
    });
    knob_param("noiseGate", "attack", m_knob_gate_attack, [e](double v) {
        e->gate().set_attack(static_cast<float>(v));
    });
    knob_param("noiseGate", "release", m_knob_gate_release, [e](double v) {
        e->gate().set_release(static_cast<float>(v));
    });

    toggle_param("compressor", "enabled", m_sw_comp_enable, [e](bool b) {
        e->comp().set_enabled(b);
    });
    knob_param("compressor", "threshold", m_knob_comp_thresh, [e](double v) {
        e->comp().set_threshold(static_cast<float>(v));
    });
    knob_param("compressor", "ratio", m_knob_comp_ratio, [e](double v) {
        e->comp().set_ratio(static_cast<float>(v));
    });
    knob_param("compressor", "attack", m_knob_comp_attack, [e](double v) {
        e->comp().set_attack(static_cast<float>(v));
    });
    knob_param("compressor", "release", m_knob_comp_release, [e](double v) {
        e->comp().set_release(static_cast<float>(v));
    });
    knob_param("compressor", "makeup", m_knob_comp_makeup, [e](double v) {
        e->comp().set_makeup(static_cast<float>(v));
    });

    toggle_param("dynamicWah", "enabled", m_sw_wah_enable, [e](bool b) {
        e->wah().set_enabled(b);
    });
    knob_param("dynamicWah", "sensitivity", m_knob_wah_sens, [e](double v) {
        e->wah().set_sensitivity(static_cast<float>(v));
    });
    knob_param("dynamicWah", "resonance", m_knob_wah_res, [e](double v) {
        e->wah().set_resonance(static_cast<float>(v));
    });
    knob_param("dynamicWah", "mix", m_knob_wah_mix, [e](double v) {
        e->wah().set_mix(static_cast<float>(v));
    });

    toggle_param("flanger", "enabled", m_sw_flanger_enable, [e](bool b) {
        e->flanger().set_enabled(b);
    });
    toggle_param("flanger", "stereo", m_sw_flanger_stereo, [e](bool b) {
        e->flanger().set_stereo_mode(b);
    });
    knob_param("flanger", "rate", m_knob_fl_rate, synced(m_knob_fl_rate));
    toggle_param("flanger", "sync", sync_of(m_knob_fl_rate).sync, synced(m_knob_fl_rate));
    knob_param("flanger", "syncNote", sync_of(m_knob_fl_rate).note_knob, synced(m_knob_fl_rate));
    knob_param("flanger", "depth", m_knob_fl_depth, [e](double v) {
        e->flanger().set_depth(static_cast<float>(v));
    });
    knob_param("flanger", "feedback", m_knob_fl_feedback, [e](double v) {
        e->flanger().set_feedback(static_cast<float>(v));
    });
    knob_param("flanger", "mix", m_knob_fl_mix, [e](double v) {
        e->flanger().set_mix(static_cast<float>(v));
    });

    toggle_param("phaser", "enabled", m_sw_phaser_enable, [e](bool b) {
        e->phaser().set_enabled(b);
    });
    toggle_param("phaser", "stereo", m_sw_phaser_stereo, [e](bool b) {
        e->phaser().set_stereo_mode(b);
    });
    knob_param("phaser", "rate", m_knob_ph_rate, synced(m_knob_ph_rate));
    toggle_param("phaser", "sync", sync_of(m_knob_ph_rate).sync, synced(m_knob_ph_rate));
    knob_param("phaser", "syncNote", sync_of(m_knob_ph_rate).note_knob, synced(m_knob_ph_rate));
    knob_param("phaser", "depth", m_knob_ph_depth, [e](double v) {
        e->phaser().set_depth(static_cast<float>(v));
    });
    knob_param("phaser", "feedback", m_knob_ph_feedback, [e](double v) {
        e->phaser().set_feedback(static_cast<float>(v));
    });
    knob_param("phaser", "mix", m_knob_ph_mix, [e](double v) {
        e->phaser().set_mix(static_cast<float>(v));
    });

    toggle_param("drive", "enabled", m_sw_drive_enable, [e](bool b) {
        e->drive().set_enabled(b);
    });
    knob_param("drive", "drive", m_knob_drive, [e](double v) {
        e->drive().set_drive(static_cast<float>(v));
    });
    knob_param("drive", "tone", m_knob_drive_tone, [e](double v) {
        e->drive().set_tone(static_cast<float>(v));
    });
    knob_param("drive", "level", m_knob_drive_level, [e](double v) {
        e->drive().set_level(static_cast<float>(v));
    });

    toggle_param("", "namEnabled", m_sw_nam_enable, [e](bool b) {
        e->nam().set_enabled(b);
    });
    toggle_param("", "namStereo", m_sw_nam_stereo, [e](bool b) {
        e->nam().set_stereo_amp(b);
    });

    toggle_param("eq", "enabled", m_sw_eq_enable, [e](bool b) {
        e->eq().set_enabled(b);
    });
    knob_param("eq", "low", m_knob_low, [e](double v) {
        e->eq().set_low_gain(v);
    });
    knob_param("eq", "mid", m_knob_mid, [e](double v) {
        e->eq().set_mid_gain(v);
    });
    knob_param("eq", "treble", m_knob_treble, [e](double v) {
        e->eq().set_treble_gain(v);
    });

    toggle_param("doubler", "enabled", m_sw_doubler_enable, [e](bool b) {
        e->doubler().set_enabled(b);
    });
    knob_param("doubler", "spread", m_knob_dbl_spread, [e](double v) {
        e->doubler().set_spread(static_cast<float>(v));
    });
    knob_param("doubler", "mix", m_knob_dbl_mix, [e](double v) {
        e->doubler().set_mix(static_cast<float>(v));
    });

    toggle_param("chorus", "enabled", m_sw_chorus_enable, [e](bool b) {
        e->chorus().set_enabled(b);
    });
    toggle_param("chorus", "stereo", m_sw_chorus_stereo, [e](bool b) {
        e->chorus().set_stereo_mode(b);
    });
    knob_param("chorus", "rate", m_knob_ch_rate, synced(m_knob_ch_rate));
    toggle_param("chorus", "sync", sync_of(m_knob_ch_rate).sync, synced(m_knob_ch_rate));
    knob_param("chorus", "syncNote", sync_of(m_knob_ch_rate).note_knob, synced(m_knob_ch_rate));
    knob_param("chorus", "depth", m_knob_ch_depth, [e](double v) {
        e->chorus().set_depth(static_cast<float>(v));
    });
    knob_param("chorus", "mix", m_knob_ch_mix, [e](double v) {
        e->chorus().set_mix(static_cast<float>(v));
    });

    toggle_param("tremolo", "enabled", m_sw_tremolo_enable, [e](bool b) {
        e->tremolo().set_enabled(b);
    });
    toggle_param("tremolo", "stereo", m_sw_tremolo_stereo, [e](bool b) {
        e->tremolo().set_stereo_mode(b);
    });
    knob_param("tremolo", "rate", m_knob_tr_rate, synced(m_knob_tr_rate));
    toggle_param("tremolo", "sync", sync_of(m_knob_tr_rate).sync, synced(m_knob_tr_rate));
    knob_param("tremolo", "syncNote", sync_of(m_knob_tr_rate).note_knob, synced(m_knob_tr_rate));
    knob_param("tremolo", "depth", m_knob_tr_depth, [e](double v) {
        e->tremolo().set_depth(static_cast<float>(v));
    });

    toggle_param("delay", "enabled", m_sw_delay_enable, [e](bool b) {
        e->delay().set_enabled(b);
    });
    toggle_param("delay", "stereo", m_sw_delay_stereo, [e](bool b) {
        e->delay().set_stereo_mode(b);
    });
    knob_param("delay", "time", m_knob_del_time, synced(m_knob_del_time));
    toggle_param("delay", "sync", sync_of(m_knob_del_time).sync, synced(m_knob_del_time));
    knob_param("delay", "syncNote", sync_of(m_knob_del_time).note_knob, synced(m_knob_del_time));
    knob_param("delay", "feedback", m_knob_del_feedback, [e](double v) {
        e->delay().set_feedback(static_cast<float>(v));
    });
    knob_param("delay", "mix", m_knob_del_mix, [e](double v) {
        e->delay().set_mix(static_cast<float>(v));
    });

    toggle_param("reverb", "enabled", m_sw_reverb_enable, [e](bool b) {
        e->reverb().set_enabled(b);
    });
    knob_param("reverb", "roomSize", m_knob_rev_room, [e](double v) {
        e->reverb().set_room_size(static_cast<float>(v));
    });
    knob_param("reverb", "damping", m_knob_rev_damping, [e](double v) {
        e->reverb().set_damping(static_cast<float>(v));
    });
    knob_param("reverb", "width", m_knob_rev_width, [e](double v) {
        e->reverb().set_width(static_cast<float>(v));
    });
    knob_param("reverb", "mix", m_knob_rev_mix, [e](double v) {
        e->reverb().set_mix(static_cast<float>(v));
    });

    toggle_param("", "irEnabled", m_sw_ir_enable, [e](bool b) {
        e->ir().set_enabled(b);
    });

    toggle_param("volume", "enabled", m_sw_volume_enable, [e](bool b) {
        e->volume().set_enabled(b);
    });
    knob_param("volume", "position", m_knob_vol_pos, [e](double v) {
        e->volume().set_position(static_cast<float>(v));
    });
    knob_param("volume", "mid", m_knob_vol_mid, [e](double v) {
        e->volume().set_mid(static_cast<float>(v));
    });

    toggle_param("", "limiter", m_sw_limiter, [e](bool b) {
        e->limiter().set_enabled(b);
    });

    for (auto& reg : m_late_params) {
        reg();
    }
    m_late_params.clear();
}

// ---------------------------------------------------------------------------
// Meters & tuner
// ---------------------------------------------------------------------------

void MainWindow::update_meters() {
    m_meter_in->set_peak(m_engine->take_input_peak());
    m_meter_out->set_peak(m_engine->take_output_peak());
    m_gr_gate->set_reduction(m_engine->gate().take_gain_reduction_db());
    m_gr_comp->set_reduction(m_engine->comp().take_gain_reduction_db());
    m_tempo_bar->poll();
}

void MainWindow::update_meter_timer() {
    if (!m_meter_timer) {
        return;
    }
    const bool visible = isVisible() && !isMinimized();
    if (visible && !m_meter_timer->isActive()) {
        m_meter_timer->start();
    } else if (!visible) {
        m_meter_timer->stop();
    }
}

void MainWindow::changeEvent(QEvent* ev) {
    QMainWindow::changeEvent(ev);
    if (ev->type() == QEvent::WindowStateChange) {
        update_meter_timer();
    }
}

void MainWindow::showEvent(QShowEvent* ev) {
    QMainWindow::showEvent(ev);
    update_meter_timer();
}

void MainWindow::hideEvent(QHideEvent* ev) {
    QMainWindow::hideEvent(ev);
    update_meter_timer();
}

void MainWindow::update_status() {
    m_audio_bar->update_metrics(); // PipeWire can change the quantum while running
    if (m_audio_ok && !m_engine->is_running()) {
        m_audio_ok = false;
        m_audio_bar->set_error("Disconnected");
    }
}

void MainWindow::update_tuner() {
    const float freq = m_engine->tuner().is_enabled() ? m_engine->tuner().last_pitch() : 0.0f;
    if (freq <= 0.0f) {
        m_tuner_display->set_reading("-", 0.0f, false);
        return;
    }

    const float ref = static_cast<float>(m_tuner_ref_hz);
    const int midi = TunerProcessor::freq_to_midi(freq, ref);
    const float cents = TunerProcessor::cents_offset(freq, ref);
    m_tuner_display->set_reading(QString::fromStdString(TunerProcessor::midi_to_name(midi)), cents,
                                 true, freq);
}

// ---------------------------------------------------------------------------
// Persisted file state
// ---------------------------------------------------------------------------

void MainWindow::restore_last_files() {
    QSettings s;

    const QString nam = s.value("files/lastNAM").toString();
    if (!nam.isEmpty() && QFile::exists(nam)) {
        load_model_file(nam, /*quiet=*/true);
    }

    const QString ir = s.value("files/lastIR").toString();
    if (!ir.isEmpty() && QFile::exists(ir)) {
        load_ir_file(ir, /*quiet=*/true);
    }

    const QString ir_b = s.value("files/lastIRB").toString();
    if (!ir_b.isEmpty() && QFile::exists(ir_b)) {
        load_irb_file(ir_b, /*quiet=*/true);
    }

    update_hero_cards();
}

void MainWindow::restore_connections() {
    QSettings s;
    if (!s.contains("connections/saved")) {
        // First run: wire the default system ports so a new user hears sound
        // without opening a patchbay. Failed connects (no such port) are
        // harmless no-ops, and the user's own routing is saved on exit.
        AudioEngine::PortConnections c;
        c.input_sources = {"system:capture_1"};
        c.output_l_dests = {"system:playback_1"};
        c.output_r_dests = {"system:playback_2"};
        m_engine->apply_connections(c);
        return;
    }

    AudioEngine::PortConnections c;
    c.input_sources = to_std_strings(s.value("connections/inputSources").toStringList());
    c.output_l_dests = to_std_strings(s.value("connections/outputLDests").toStringList());
    c.output_r_dests = to_std_strings(s.value("connections/outputRDests").toStringList());
    m_engine->apply_connections(c);
}

void MainWindow::save_connections() {
    if (!m_engine->is_running() || !m_engine->supports_port_routing()) {
        return;
    }

    auto to_list = [](const std::vector<std::string>& v) {
        QStringList l;
        l.reserve(static_cast<int>(v.size()));
        for (const auto& x : v) {
            l << QString::fromStdString(x);
        }
        return l;
    };

    auto c = m_engine->current_connections();
    QSettings s;
    s.setValue("connections/inputSources", to_list(c.input_sources));
    s.setValue("connections/outputLDests", to_list(c.output_l_dests));
    s.setValue("connections/outputRDests", to_list(c.output_r_dests));
    s.setValue("connections/saved", true);
}

// ---------------------------------------------------------------------------
// Hero rows (Amplifier / Cabinet)
// ---------------------------------------------------------------------------

void MainWindow::update_hero_cards() {
    auto set_status = [this](QLabel* name, QLabel* meta, QLabel* origin, bool loaded,
                             const QString& path, const QString& empty_text) {
        if (!loaded) {
            name->setText(empty_text);
            name->setObjectName("fileStatus");
            name->setToolTip({});
            meta->clear();
            meta->setToolTip({});
            origin->hide();
        } else {
            // Metadata comes from the index when the file is in the library,
            // otherwise it is read now (one-off imports, dropped files).
            const LibraryEntry* e = m_library_index->find(path);
            const LibraryEntry fresh = e ? *e : LibraryIndex::describe(path);
            name->setText(
                name->fontMetrics().elidedText(fresh.name, Qt::ElideMiddle, K_HERO_NAME_MAX_WIDTH));
            name->setObjectName("fileStatusOk");
            name->setToolTip(QDir::toNativeSeparators(fresh.path));
            meta->setText(fresh.summary);
            meta->setToolTip(fresh.details);
            origin->setVisible(fresh.from_tone3000());
            origin->setProperty("toneId", fresh.t3k_tone_id);
            origin->setToolTip(fresh.from_tone3000()
                                   ? QString("From TONE3000: %1 by @%2 (click to open)")
                                         .arg(fresh.t3k_title, fresh.t3k_creator)
                                   : QString());
        }
        name->style()->unpolish(name);
        name->style()->polish(name);
    };

    set_status(m_lbl_nam_status, m_lbl_nam_meta, m_lbl_nam_origin, m_engine->is_model_loaded(),
               QString::fromStdString(m_engine->model_path()), "No model loaded");
    set_status(m_lbl_ir_status, m_lbl_ir_meta, m_lbl_ir_origin, m_engine->is_ir_loaded(),
               QString::fromStdString(m_engine->ir_path()), "No impulse response loaded");
    if (m_lbl_irb_status) {
        const bool b = m_engine->is_irb_loaded();
        const QString p = QString::fromStdString(m_engine->ir_path_b());
        m_lbl_irb_status->setText(b ? m_lbl_irb_status->fontMetrics().elidedText(
                                          QFileInfo(p).completeBaseName(), Qt::ElideMiddle, 300)
                                    : QString("Empty — Choose… or ‹ ›"));
        m_lbl_irb_status->setToolTip(b ? QDir::toNativeSeparators(p) : QString());
        m_lbl_irb_status->setObjectName(b ? "fileStatusOk" : "fileStatus");
        m_lbl_irb_status->style()->unpolish(m_lbl_irb_status);
        m_lbl_irb_status->style()->polish(m_lbl_irb_status);
    }
    // Every model/IR change lands here — it counts toward the preset `*`.
    if (m_preset_state_timer) {
        m_preset_state_timer->start();
    }
    update_rig_status();
}

void MainWindow::update_rig_status() {
    auto item = [](bool on, const QString& text) {
        return QString("<span style=\"color:%1\">●</span> %2")
            .arg((on ? Theme::meter_low() : Theme::track()).name(), text.toHtmlEscaped());
    };
    const bool model = m_engine->is_model_loaded();
    const bool ir = m_engine->is_ir_loaded();
    const bool t3k = m_tone3000->is_signed_in();
    QString t3k_text = "TONE3000";
    if (t3k && m_tone3000->user().is_valid()) {
        t3k_text += " @" + m_tone3000->user().username;
    }

    m_lbl_rig_status->setText(item(model, "Model") + "&nbsp;&nbsp;&nbsp;" + item(ir, "IR") +
                              "&nbsp;&nbsp;&nbsp;" + item(t3k, t3k_text));
    m_lbl_rig_status->setToolTip(
        QString("Model: %1\nIR: %2\nTONE3000: %3")
            .arg(model
                     ? QFileInfo(QString::fromStdString(m_engine->model_path())).completeBaseName()
                     : "none",
                 ir ? QFileInfo(QString::fromStdString(m_engine->ir_path())).completeBaseName()
                    : "none",
                 t3k ? "signed in" : "signed out"));
}

// ---------------------------------------------------------------------------
// Preset save / load
// ---------------------------------------------------------------------------

void MainWindow::seed_starter_presets() {
    // Once per config: copy the bundled starters into <presets>/Starter so a
    // first run isn't empty. They carry no model/IR — loading one keeps the
    // current rig. Deleting them sticks: the flag stops a re-seed.
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
        // Resource files copy read-only.
        QFile::setPermissions(dest, QFile::permissions(dest) | QFile::WriteOwner);
    }
    s.setValue("presets/starterSeeded", true);
}

QJsonObject MainWindow::current_preset_json() const {
    QJsonObject obj = PresetSerializer::to_json(m_params, *m_engine);
    // TONE3000 linkage: lets the preset find its model on another machine.
    auto linkage = [this](const std::string& p) {
        QJsonObject o;
        if (const LibraryEntry* e = m_library_index->find(QString::fromStdString(p));
            e && e->from_tone3000()) {
            o["toneId"] = e->t3k_tone_id;
            o["modelId"] = e->t3k_model_id;
            o["title"] = e->t3k_title;
            o["creator"] = e->t3k_creator;
        }
        return o;
    };
    QJsonObject t3k;
    if (m_engine->is_model_loaded()) {
        const QJsonObject m = linkage(m_engine->model_path());
        if (!m.isEmpty()) {
            t3k["model"] = m;
        }
    }
    if (m_engine->is_ir_loaded()) {
        const QJsonObject m = linkage(m_engine->ir_path());
        if (!m.isEmpty()) {
            t3k["ir"] = m;
        }
    }
    if (!t3k.isEmpty()) {
        obj["tone3000"] = t3k;
    }
    return obj;
}

bool MainWindow::write_preset_file(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Preset", "Cannot write preset:\n" + path);
        return false;
    }
    f.write(QJsonDocument(current_preset_json()).toJson());
    f.close();
    return true;
}

// Save-dialog path with the .nampreset suffix; empty when cancelled.
static QString ask_preset_save_path(QWidget* parent, const QString& title, const QString& start) {
    QString path = QFileDialog::getSaveFileName(
        parent, title, start, "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty()) {
        path += ".nampreset"; // portal dialogs don't append the filter's suffix
    }
    return path;
}

void MainWindow::on_save_preset() {
    if (m_preset_path.isEmpty() || !QFile::exists(m_preset_path)) {
        on_save_preset_as();
        return;
    }
    if (write_preset_file(m_preset_path)) {
        set_current_preset(m_preset_path);
    }
}

void MainWindow::on_save_preset_as() {
    // Default to the library's presets folder so a saved preset shows up in
    // the list right away (the folder watcher picks it up).
    QString start = m_preset_path;
    if (start.isEmpty()) {
        start = LibraryRoots::default_preset_dir();
        QDir().mkpath(start);
    }
    QString path = ask_preset_save_path(nullptr, "Save Preset As", start);
    if (path.isEmpty() || !write_preset_file(path)) {
        return;
    }

    path = QDir::cleanPath(path);
    QSettings().setValue("files/lastPreset", path);
    m_library->note_used(path);
    m_library->set_loaded_path(LibraryEntry::Kind::PRESET, path);
    set_current_preset(path);
}

void MainWindow::on_export_preset() {
    const QString name = m_preset_path.isEmpty() ? QString("Untitled.nampreset")
                                                 : QFileInfo(m_preset_path).fileName();
    const QString path = ask_preset_save_path(
        nullptr, "Export Preset",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + name);
    if (!path.isEmpty()) {
        write_preset_file(path); // a copy — the current preset stays
    }
}

void MainWindow::on_import_presets() {
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, "Import Presets",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (files.isEmpty()) {
        return;
    }

    const QString dir = LibraryRoots::default_preset_dir();
    QDir().mkpath(dir);
    QString last;
    QStringList failed;
    for (const QString& src : files) {
        const QFileInfo fi(src);
        if (QDir::cleanPath(fi.absolutePath()) == QDir::cleanPath(dir)) {
            last = src;
            continue;
        }
        // Never overwrite: "Name.nampreset" → "Name (2).nampreset" …
        QString dest = dir + "/" + fi.fileName();
        for (int n = 2; QFile::exists(dest); ++n) {
            dest = QString("%1/%2 (%3).%4").arg(dir, fi.completeBaseName()).arg(n).arg(fi.suffix());
        }
        if (QFile::copy(src, dest)) {
            last = dest;
        } else {
            failed << src;
        }
    }
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Import Presets", "Could not copy:\n" + failed.join('\n'));
    }
    if (!last.isEmpty()) {
        load_preset_file(last);
    }
}

void MainWindow::on_delete_preset() {
    if (m_preset_path.isEmpty() || !QFile::exists(m_preset_path)) {
        return;
    }
    const QString name = QFileInfo(m_preset_path).completeBaseName();
    if (QMessageBox::question(this, "Delete Preset",
                              "Move the preset \"" + name +
                                  "\" to the trash?\n"
                                  "The current sound is kept as an untitled state.") !=
        QMessageBox::Yes) {
        return;
    }
    if (!QFile::moveToTrash(m_preset_path) && !QFile::remove(m_preset_path)) {
        QMessageBox::critical(this, "Delete Preset", "Cannot delete:\n" + m_preset_path);
        return;
    }
    m_library->set_loaded_path(LibraryEntry::Kind::PRESET, {});
    set_current_preset({});
}

void MainWindow::on_locate_preset() {
    const QString dir = m_preset_path.isEmpty() ? LibraryRoots::default_preset_dir()
                                                : QFileInfo(m_preset_path).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::set_current_preset(const QString& path) {
    m_preset_path = path;
    m_preset_baseline = PresetSerializer::to_json(m_params, *m_engine);
    m_ab[0] = m_ab[1] = QJsonObject();
    m_ab_active = 0;
    m_preset_bar->set_ab_state(0);
    update_preset_state();
    schedule_session_save();
}

// Preset state equality with a tolerance on numbers: a knob's dial
// quantizes values (and tapers/resolutions change between versions), so a
// value can come back a hair off what the file says without anyone
// touching it. 1e-3 relative is far below anything audible or displayed.
static bool same_state(const QJsonValue& a, const QJsonValue& b) {
    if (a.isDouble() && b.isDouble()) {
        const double x = a.toDouble(), y = b.toDouble();
        return std::abs(x - y) <= 1e-3 * std::max({1.0, std::abs(x), std::abs(y)});
    }
    if (a.isObject() && b.isObject()) {
        const QJsonObject oa = a.toObject(), ob = b.toObject();
        if (oa.size() != ob.size()) {
            return false;
        }
        for (auto it = oa.begin(); it != oa.end(); ++it) {
            if (!ob.contains(it.key()) || !same_state(it.value(), ob.value(it.key()))) {
                return false;
            }
        }
        return true;
    }
    if (a.isArray() && b.isArray()) {
        const QJsonArray xa = a.toArray(), xb = b.toArray();
        if (xa.size() != xb.size()) {
            return false;
        }
        for (int i = 0; i < xa.size(); ++i) {
            if (!same_state(xa[i], xb[i])) {
                return false;
            }
        }
        return true;
    }
    return a == b;
}

void MainWindow::update_preset_state() {
    const bool modified =
        !m_preset_path.isEmpty() &&
        !same_state(PresetSerializer::to_json(m_params, *m_engine), m_preset_baseline);
    m_preset_bar->set_preset(m_preset_path, modified);
    setWindowTitle(m_preset_path.isEmpty() ? QString("NAM Player")
                                           : QString("%1%2 — NAM Player")
                                                 .arg(QFileInfo(m_preset_path).completeBaseName(),
                                                      modified ? "*" : ""));
}

void MainWindow::restore_preset_state() {
    QSettings s;
    const QString path = s.value("session/presetPath").toString();
    if (path.isEmpty() || !QFile::exists(path)) {
        update_preset_state();
        return;
    }
    m_preset_path = path;
    // A baseline saved by an older version lacks parameters added since;
    // those take the live (= restored) values, so an upgrade alone never
    // marks the preset modified.
    std::function<QJsonObject(QJsonObject, const QJsonObject&)> fill =
        [&fill](QJsonObject base, const QJsonObject& live) {
            for (auto it = live.begin(); it != live.end(); ++it) {
                if (!base.contains(it.key())) {
                    base.insert(it.key(), it.value());
                } else if (it.key() == "effectOrder" && it.value().isArray()) {
                    // Effects added since (new IDs) sit in the live order at
                    // their default slots: if the live order is the saved one
                    // plus new IDs, adopt it.
                    const QJsonArray saved = base.value(it.key()).toArray();
                    QJsonArray known;
                    for (const auto& id : it.value().toArray()) {
                        if (saved.contains(id)) {
                            known.append(id);
                        }
                    }
                    if (known == saved) {
                        base.insert(it.key(), it.value());
                    }
                } else if (it.value().isObject() && base.value(it.key()).isObject()) {
                    base.insert(it.key(),
                                fill(base.value(it.key()).toObject(), it.value().toObject()));
                }
            }
            return base;
        };
    m_preset_baseline = fill(
        QJsonDocument::fromJson(s.value("session/presetBaseline").toString().toUtf8()).object(),
        PresetSerializer::to_json(m_params, *m_engine));
    m_library->set_loaded_path(LibraryEntry::Kind::PRESET, path);
    update_preset_state();
}

void MainWindow::step_preset(int step) {
    const QString next = m_library->neighbour(LibraryEntry::Kind::PRESET, m_preset_path, step);
    if (!next.isEmpty() && next != m_preset_path) {
        load_preset_file(next);
    }
}

void MainWindow::switch_ab(int side) {
    if (side == m_ab_active) {
        return;
    }
    // The side being left keeps the live state; the other side starts as a
    // copy of it the first time it is visited.
    const QJsonObject live = PresetSerializer::to_json(m_params, *m_engine);
    m_ab[m_ab_active] = live;
    m_ab_active = side;
    if (m_ab[side].isEmpty()) {
        m_ab[side] = live;
    } else {
        const auto applied = apply_preset_params(m_ab[side]);
        if (!applied.nam_path.isEmpty() && QFile::exists(applied.nam_path)) {
            load_model_file(applied.nam_path, true);
        }
        if (!applied.ir_path.isEmpty() && QFile::exists(applied.ir_path)) {
            load_ir_file(applied.ir_path, true);
        }
    }
    m_preset_bar->set_ab_state(side);
    schedule_session_save();
}

void MainWindow::copy_ab() {
    m_ab[1 - m_ab_active] = PresetSerializer::to_json(m_params, *m_engine);
}

void MainWindow::on_reset_all() {
    if (QMessageBox::question(this, "Reset All",
                              "Reset all effects, gains, and the chain order to their defaults?\n"
                              "The loaded model and IR are kept.") != QMessageBox::Yes) {
        return;
    }

    // Registry defaults through the widgets — the engine follows via the
    // registry connections, exactly like a preset load.
    for (const auto& p : m_params) {
        if (p.knob) {
            p.knob->set_value(p.knob_default);
        } else {
            p.toggle->setChecked(p.toggle_default);
        }
    }

    QStringList order;
    for (const auto& id : m_engine->default_chain_ids()) {
        order << QString::fromStdString(id);
    }
    apply_chain_order(order);
    schedule_session_save();
}

void MainWindow::on_load_preset() {
    QString start = QSettings().value("files/lastPreset").toString();
    if (start.isEmpty()) {
        start = LibraryRoots::default_preset_dir();
    }
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Load Preset", start, "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (!path.isEmpty()) {
        load_preset_file(path);
    }
}

bool MainWindow::load_preset_file(const QString& path_in) {
    const QString path = QDir::cleanPath(path_in);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Preset", "Cannot read preset:\n" + path);
        return false;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        QMessageBox::critical(
            this, "Preset",
            "Invalid preset:\n" + path + "\n\n" +
                (doc.isNull() ? err.errorString() : QStringLiteral("not a JSON object")));
        return false;
    }
    QSettings settings;
    settings.setValue("files/lastPreset", path);
    m_library->note_used(path);
    m_library->set_loaded_path(LibraryEntry::Kind::PRESET, path);

    const auto applied = apply_preset_params(doc.object());

    // v2 presets embed the model/IR paths. Only a non-empty path acts — a
    // preset saved without a model must not unload the current one — and when
    // the referenced file is missing or fails to load, the current one stays.
    // A referenced file that isn't there: the TONE3000 linkage can still find
    // it (same model id elsewhere in the library) or fetch it.
    const QJsonObject t3k = doc.object().value("tone3000").toObject();
    auto try_load = [this](const QString& p, LibraryEntry::Kind kind) {
        if (p.isEmpty() || !QFile::exists(p)) {
            return false;
        }
        return kind == LibraryEntry::Kind::MODEL ? load_model_file(p, true) : load_ir_file(p, true);
    };
    // A TONE3000 fetch can land after this returns: the fetched model is
    // part of the preset, not an edit, so it moves the baseline.
    auto rebaseline = [this, path] {
        if (m_preset_path == path) {
            set_current_preset(path);
        }
    };

    QStringList failed;
    if (!applied.nam_path.isEmpty() && !try_load(applied.nam_path, LibraryEntry::Kind::MODEL)) {
        const QJsonObject link = t3k.value("model").toObject();
        const int model_id = link.value("modelId").toInt();
        if (model_id > 0) {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.nam_path).completeBaseName()
                                      : link.value("title").toString();
            resolve_tone3000_model(model_id, title, [try_load, rebaseline](const QString& p) {
                if (!p.isEmpty() && try_load(p, LibraryEntry::Kind::MODEL)) {
                    rebaseline();
                }
            });
        } else {
            failed << applied.nam_path;
        }
    }
    if (!applied.ir_path.isEmpty() && !try_load(applied.ir_path, LibraryEntry::Kind::IR)) {
        const QJsonObject link = t3k.value("ir").toObject();
        const int model_id = link.value("modelId").toInt();
        if (model_id > 0) {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.ir_path).completeBaseName()
                                      : link.value("title").toString();
            resolve_tone3000_model(model_id, title, [try_load, rebaseline](const QString& p) {
                if (!p.isEmpty() && try_load(p, LibraryEntry::Kind::IR)) {
                    rebaseline();
                }
            });
        } else {
            failed << applied.ir_path;
        }
    }
    // Slot B: presets made before the dual cab don't mention it (left alone);
    // newer ones say exactly what slot B holds, empty included.
    if (applied.has_ir_path_b) {
        if (applied.ir_path_b.isEmpty()) {
            m_engine->unload_irb();
            QSettings().remove("files/lastIRB");
        } else if (!(QFile::exists(applied.ir_path_b) && load_irb_file(applied.ir_path_b, true))) {
            failed << applied.ir_path_b;
        }
    }
    update_hero_cards();
    set_current_preset(path);
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, "Preset",
                             "The preset references files that could not be loaded "
                             "(the current model/IR was kept):\n" +
                                 failed.join('\n'));
    }
    return failed.isEmpty();
}

PresetSerializer::Applied MainWindow::apply_preset_params(const QJsonObject& obj) {
    // Widgets first (engine follows via the registry connections), then the
    // chain order (engine-side sanitizer handles unknown/missing IDs).
    const auto applied = PresetSerializer::apply(obj, m_params);
    if (!applied.effect_order.isEmpty()) {
        apply_chain_order(applied.effect_order);
    }
    return applied;
}

void MainWindow::resolve_tone3000_model(int model_id, const QString& title,
                                        std::function<void(const QString&)> then) {
    // 1. Already in the library under another path (downloaded on this machine).
    if (const LibraryEntry* e = m_library_index->find_tone3000_model(model_id)) {
        then(e->path);
        return;
    }

    // 2. Offer to fetch it.
    if (!Tone3000Client::has_client_id()) {
        QMessageBox::warning(this, "Preset",
                             "This preset uses \"" + title +
                                 "\" from TONE3000, which isn't on this computer, "
                                 "and TONE3000 isn't set up in this build (Library → TONE3000).");
        then({});
        return;
    }
    if (!m_tone3000->is_signed_in()) {
        QMessageBox::information(
            this, "Preset",
            "This preset uses \"" + title +
                "\" from TONE3000, which isn't on this computer.\n\n"
                "Sign in on the Library's TONE3000 tab, then load the preset again to fetch it.");
        m_library_dock->show();
        m_library_tabs->set_tab(LibraryDock::TONE3000);
        then({});
        return;
    }
    if (QMessageBox::question(this, "Preset",
                              "This preset uses \"" + title +
                                  "\" from TONE3000, which isn't on this computer.\n\n"
                                  "Download it into the library now?") != QMessageBox::Yes) {
        then({});
        return;
    }

    m_tone3000->fetch_model(model_id, [this, then, title](const Tone3000Client::Result& r) {
        if (!r.ok) {
            QMessageBox::warning(this, "TONE3000",
                                 "Could not look up \"" + title + "\": " + r.error);
            then({});
            return;
        }
        const T3kModel model = Tone3000Client::parse_model(r.object());
        m_tone3000->fetch_tone(
            model.tone_id, [this, then, title, model](const Tone3000Client::Result& tr) {
                if (!tr.ok) {
                    QMessageBox::warning(this, "TONE3000",
                                         "Could not look up the tone of \"" + title +
                                             "\": " + tr.error);
                    then({});
                    return;
                }
                const T3kTone tone = Tone3000Client::parse_tone(tr.object());
                m_tone3000->download_model(
                    model, tone, [this, then, title](bool ok, QString path, QString err) {
                        if (!ok) {
                            QMessageBox::warning(this, "TONE3000",
                                                 "Download of \"" + title + "\" failed: " + err);
                            then({});
                            return;
                        }
                        m_library_index->rescan();
                        then(path);
                    });
            });
    });
}

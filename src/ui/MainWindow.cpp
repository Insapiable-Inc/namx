#include "MainWindow.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "AudioStatusBar.h"
#include "Common.h"
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

namespace npl
{

// Mime type carrying the effect ID of a dragged strip card.
static const char EFFECT_MIME[] = "application/x-nam-effect-id";

// QStringList → the engine's std::string list (effect IDs, JACK port names).
static vector<string> to_std_strings(const QStringList& list) {
    vector<string> out;
    for (const QString& s : list) {
        out.push_back(s.toStdString());
    }
    return out;
}

// Fixed columns at the left edge of every row card, so the enable toggles,
// stereo slots, and first knobs line up vertically across the strip.
static constexpr int GRIP_COLUMN_WIDTH = 22; // "≡" drag handle
static constexpr int NAME_COLUMN_WIDTH = 120; // fits "Dynamic Wah"
static constexpr int ENABLE_SLOT_WIDTH = 74; // toggle + "On"/"Off" caption
static constexpr int STEREO_SLOT_WIDTH = 96; // toggle + "Stereo" caption
static constexpr int SYNC_SLOT_WIDTH = 112; // toggle + "Sync" / "Manual" / "Continuous"
// Every row is exactly as tall as a knob row (40 px dial + knob margins +
// card margins + border), so knob-less rows and the tuner match the rest.
static constexpr int ROW_CARD_HEIGHT = 62;
static constexpr int DETAIL_ROW_HEIGHT = 54; // the ▾ row of advanced knobs
// Loaded file name on the Amplifier/Cabinet rows elides beyond this.
static constexpr int HERO_NAME_MAX_WIDTH = 420;

// Enable toggle with a live "On"/"Off" caption beside it — one line, so the
// switch stays on the row's vertical centerline.
static QWidget* make_enable_slot(ToggleSwitch* sw) {
    auto* w = new QWidget;
    w->setFixedWidth(ENABLE_SLOT_WIDTH);
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
    w->setFixedWidth(STEREO_SLOT_WIDTH);
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

// The Sync column, right of Stereo: reserved on every row like the stereo slot, and
// filled by add_tempo_sync on the rows whose time/rate knob can follow the tempo.
static QWidget* make_sync_slot() {
    auto* w = new QWidget;
    w->setObjectName("syncSlot");
    w->setFixedWidth(SYNC_SLOT_WIDTH);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    return w;
}

// A card's main row (grip, name, enable slot, stereo slot, sync slot, controls…).
static QHBoxLayout* row_layout(QWidget* card) {
    return static_cast<QHBoxLayout*>(
        card->findChild<QWidget*>("cardRow", Qt::FindDirectChildrenOnly)->layout());
}

// The Chilean flag, `height` px tall (3:2): blue canton with a white star over
// a white band, red lower half. Painted rather than an emoji so it shows the
// same everywhere (Windows has no flag emoji).
static QPixmap chile_flag(int height, qreal dpr) {
    const qreal w = height * 1.5;
    const qreal h = height;
    QPixmap pm(QSize(qRound(w * dpr), qRound(h * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::flag_white());
    p.drawRect(QRectF(0, 0, w, h / 2));
    p.setBrush(Theme::flag_red());
    p.drawRect(QRectF(0, h / 2, w, h / 2));
    const QRectF canton(0, 0, h / 2, h / 2);
    p.setBrush(Theme::flag_blue());
    p.drawRect(canton);

    // Five-pointed star, its diameter half the canton's side.
    const qreal outer = canton.width() / 4.0;
    const qreal inner = outer * 0.382;
    QPolygonF star;
    for (int i = 0; i < 10; ++i) {
        const qreal r = i % 2 == 0 ? outer : inner;
        const qreal a = -std::numbers::pi / 2 + i * std::numbers::pi / 5;
        star << canton.center() + QPointF(r * std::cos(a), r * std::sin(a));
    }
    p.setBrush(Theme::flag_white());
    p.drawPolygon(star);
    return pm;
}

// "Made in Chile" + flag, in the menu bar's right corner.
static QWidget* make_made_in_chile() {
    auto* w = new QWidget;
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 10, 0);
    lay->setSpacing(7);
    auto* text = new QLabel("Made in Chile");
    text->setObjectName("madeInChile");
    auto* flag = new QLabel;
    flag->setPixmap(chile_flag(14, qApp->devicePixelRatio()));
    lay->addWidget(text, 0, Qt::AlignVCenter);
    lay->addWidget(flag, 0, Qt::AlignVCenter);
    w->setToolTip("Hecho en Chile");
    return w;
}

// A switch with a fixed caption beside it ("Vibe"), for an either/or mode.
static QWidget* make_caption_switch(ToggleSwitch* sw, const QString& caption) {
    auto* w = new QWidget;
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* label = new QLabel(caption);
    label->setObjectName("toggleCaption");
    lay->addWidget(sw, 0, Qt::AlignVCenter);
    lay->addWidget(label, 0, Qt::AlignVCenter);
    return w;
}

// Puts `w` in a card's Stereo column, on rows that have no stereo mode.
static void put_in_stereo_column(QWidget* card, QWidget* w) {
    // Main row: grip, name, enable slot, stereo slot, sync slot, …
    QWidget* stereo_slot = row_layout(card)->itemAt(3)->widget();
    static_cast<QHBoxLayout*>(stereo_slot->layout())->addWidget(w, 0, Qt::AlignVCenter);
}

// Puts `w` in a card's Sync column (the column right of Stereo).
static void put_in_sync_column(QWidget* card, QWidget* w) {
    QWidget* slot = card->findChild<QWidget*>("syncSlot");
    static_cast<QHBoxLayout*>(slot->layout())->addWidget(w, 0, Qt::AlignVCenter);
}

// Adds `w` at the end of a card's main row, after its knobs.
static void append_to_row(QWidget* card, QWidget* w) {
    QHBoxLayout* lay = row_layout(card);
    lay->insertWidget(lay->count() - 1, w, 0, Qt::AlignVCenter); // before the stretch
}

// Rows without Stereo or Sync (Gate, Compressor) show their gain-reduction meter
// across both switch columns; the knob column stays aligned.
static void place_across_switch_columns(QWidget* card, QWidget* w) {
    // Main row: grip, name, enable slot, stereo slot, sync slot, …
    QWidget* stereo_slot = row_layout(card)->itemAt(3)->widget();
    QWidget* sync_slot = card->findChild<QWidget*>("syncSlot");
    const int width = STEREO_SLOT_WIDTH + row_layout(card)->spacing() + SYNC_SLOT_WIDTH;
    sync_slot->hide(); // a hidden widget takes no space or spacing in the row
    stereo_slot->setFixedWidth(width);
    w->setFixedWidth(width);
    stereo_slot->layout()->addWidget(w);
}

static QString format_hz(double hz, double off_at) {
    if ((off_at > 0.0 && hz <= off_at) || (off_at < 0.0 && hz >= -off_at)) {
        return QString("Off");
    }
    return hz < 1000.0 ? QString("%1 Hz").arg(qRound(hz))
                       : QString("%1 kHz").arg(hz / 1000.0, 0, 'f', hz < 10000.0 ? 1 : 0);
}

// Frequency readout for EQ/cut knobs: "250 Hz", "3.2 kHz", or "Off" at the
// knob's open end (offAt > 0: at or below it; offAt < 0: at or above −offAt).
static std::function<QString(double)> hz_label(double off_at = 0.0) {
    return [off_at](double hz) {
        return format_hz(hz, off_at);
    };
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , engine(make_unique<AudioEngine>()) {
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
    library_index = new LibraryIndex(this);
    tone3000 = new Tone3000Client(this);
    midi_mapper = new MidiMapper(this); // targets fill in as parameters register

    setup_ui();
    register_parameters();
    setup_midi(midi_menu);

    // Debounced session save — every registry widget change restarts it, so
    // the session survives a crash without a write per knob tick. Created
    // before restoreSessionState so the restore's widget signals find it.
    session_save_timer = new QTimer(this);
    session_save_timer->setSingleShot(true);
    session_save_timer->setInterval(1000);
    connect(session_save_timer, &QTimer::timeout, this, &MainWindow::save_session_state);
    // The preset `*` follows the same changes, coalesced to one compare per
    // event-loop pass (a preset load moves dozens of widgets).
    preset_state_timer = new QTimer(this);
    preset_state_timer->setSingleShot(true);
    preset_state_timer->setInterval(0);
    connect(preset_state_timer, &QTimer::timeout, this, &MainWindow::update_preset_state);

    // Knob tapers (Phase 9): times, rates and frequencies on log dials.
    for (KnobWidget* k : {knob_gate_attack, knob_gate_release, knob_comp_attack, knob_comp_release,
                          knob_fl_rate, knob_ph_rate, knob_ch_rate, knob_tr_rate, knob_del_time}) {
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
        library_dock->setFloating(false); // a floating dock without a title bar can't be moved
    }

    setup_audio();
    restore_last_files();
    restore_preset_state();

    meter_timer = new QTimer(this);
    meter_timer->setTimerType(Qt::PreciseTimer);
    meter_timer->setInterval(16);
    connect(meter_timer, &QTimer::timeout, this, &MainWindow::update_meters);
    update_meter_timer();

    status_timer = new QTimer(this);
    connect(status_timer, &QTimer::timeout, this, &MainWindow::update_status);
    status_timer->start(50);

    tuner_timer = new QTimer(this);
    connect(tuner_timer, &QTimer::timeout, this, &MainWindow::update_tuner);
    tuner_timer->start(100);
}

MainWindow::~MainWindow() {
    meter_timer->stop();
    status_timer->stop();
    tuner_timer->stop();
    session_save_timer->stop();
    preset_state_timer->stop();
    QSettings().setValue("window/geometry", saveGeometry());
    QSettings().setValue("window/state", saveState());
    save_session_state();
    save_connections();
    engine->stop();
    delete midi; // stop MIDI input before the rest of the window is torn down
}

QFrame* MainWindow::make_row_card(const QString& title) {
    auto* card = new QFrame;
    card->setProperty("fxCard", true); // card chrome lives in the Theme QSS
    card->setProperty("displayName", title); // context menu + drag bookkeeping
    card->setFixedHeight(ROW_CARD_HEIGHT);
    // Card = main row (+ an optional detail row, see addDetailRow).
    auto* outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* row = new QWidget;
    row->setObjectName("cardRow");
    row->setFixedHeight(ROW_CARD_HEIGHT - 2); // inside the 1 px border
    outer->addWidget(row);
    outer->addStretch();
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(10);

    // Grip column: the ≡ handle is the only place a reorder drag can start,
    // so toggle/knob clicks can never move a card.
    auto* grip = new QLabel("≡");
    grip->setObjectName("fxGrip");
    grip->setFixedWidth(GRIP_COLUMN_WIDTH);
    grip->setAlignment(Qt::AlignCenter);
    grip->setCursor(Qt::OpenHandCursor);
    grip->setToolTip("Drag to reorder");
    lay->addWidget(grip, 0, Qt::AlignVCenter);
    card_grips[card] = grip;

    auto* name = new QLabel(title);
    name->setObjectName("fxName");
    name->setFixedWidth(NAME_COLUMN_WIDTH);
    lay->addWidget(name, 0, Qt::AlignVCenter);

    return card;
}

KnobWidget* MainWindow::late_knob(const QString& group, const QString& key, KnobWidget* k,
                                  std::function<void(double)> apply) {
    late_params.push_back([this, group, key, k, apply] {
        knob_param(group, key, k, apply);
    });
    return k;
}

// Shows or hides a card's detail rows and resizes the card to fit.
static void show_details(QWidget* card, QWidget* container, QToolButton* more, bool open) {
    container->setVisible(open);
    card->setFixedHeight(ROW_CARD_HEIGHT +
                         (open ? container->layout()->count() * DETAIL_ROW_HEIGHT : 0));
    more->setText(open ? "▴" : "▾");
    more->setToolTip(open ? "Fewer controls" : "More controls");
}

// The ▾ button: show/hide the details and remember it per card across sessions.
static void toggle_details(QWidget* card, QWidget* container, QToolButton* more,
                           const QString& name, bool open) {
    show_details(card, container, more, open);
    QSettings s;
    QStringList expanded = s.value("ui/expandedCards").toStringList();
    expanded.removeAll(name);
    if (open) {
        expanded << name;
    }
    s.setValue("ui/expandedCards", expanded);
}

void MainWindow::add_detail_row(QWidget* card, const QList<QWidget*>& widgets) {
    // All detail rows of a card live in one container behind one ▾ toggle;
    // a second call adds a second row.
    QWidget* container = card->findChild<QWidget*>("cardDetail", Qt::FindDirectChildrenOnly);
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
    row->setFixedHeight(DETAIL_ROW_HEIGHT);
    auto* lay = new QHBoxLayout(row);
    // Indent to the knob column: margin + grip + name + enable, stereo and sync slots.
    const int indent = 10 + GRIP_COLUMN_WIDTH + NAME_COLUMN_WIDTH + ENABLE_SLOT_WIDTH +
                       STEREO_SLOT_WIDTH + SYNC_SLOT_WIDTH + 5 * 10;
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
            card->setFixedHeight(ROW_CARD_HEIGHT + rows * DETAIL_ROW_HEIGHT);
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
    connect(more, &QToolButton::toggled, this, [card, container, more, name](bool open) {
        toggle_details(card, container, more, name, open);
    });
    const bool open = QSettings().value("ui/expandedCards").toStringList().contains(name);
    more->setChecked(open);
    show_details(card, container, more, open);
}

QWidget* MainWindow::make_effect_row(const QString& title, ToggleSwitch*& toggle_out,
                                     const std::initializer_list<KnobWidget*>& knobs,
                                     ToggleSwitch** stereo_toggle_out) {
    QFrame* card = make_row_card(title);
    QHBoxLayout* lay = row_layout(card);

    toggle_out = new ToggleSwitch;
    toggle_out->setCheckable(true);
    toggle_out->setChecked(false);
    lay->addWidget(make_enable_slot(toggle_out), 0, Qt::AlignVCenter);

    lay->addWidget(make_stereo_slot(stereo_toggle_out), 0, Qt::AlignVCenter);
    lay->addWidget(make_sync_slot(), 0, Qt::AlignVCenter);

    for (auto* k : knobs) {
        lay->addWidget(k, 0, Qt::AlignVCenter);
    }

    lay->addStretch();
    return card;
}

// GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): used by the disabled
// Cabinet Blend knob.
// // Cabinet blend readout: "A = B", "A 70", "B 65".
// static QString format_blend(double v) {
//     const int b = static_cast<int>(std::lround(v));
//     return b == 50  ? QString("A = B")
//            : b < 50 ? QString("A %1").arg(100 - b)
//                     : QString("B %1").arg(b);
// }

// GUIDELINE EXCEPTION (commented-out code): used by the disabled Chorus Mode knob.
// static QString format_chorus_mode(double v) {
//     static const char* names[] = {"Normal", "Tri", "Vintage", "Spatial"};
//     return QString(names[std::clamp(static_cast<int>(std::lround(v)), 0, 3)]);
// }

// Whole steps as "+3 st"; Transpose's continuous mode adds a decimal ("+3.4 st").
static QString format_semitones(double v) {
    const double tenths = std::round(v * 10.0) / 10.0;
    if (tenths == 0.0) {
        return QString("0 st");
    }
    const bool whole = std::fmod(tenths, 1.0) == 0.0;
    return QString("%1%2 st").arg(tenths > 0 ? "+" : "").arg(tenths, 0, 'f', whole ? 0 : 1);
}

// Reverse switch for a pedal-like knob (Stereo column): MIDI controllers sweep that
// knob backwards while it's on (MidiTarget::reverse).
static ToggleSwitch* make_reverse_switch(QWidget* card, const QString& knob_name) {
    auto* sw = new ToggleSwitch;
    sw->setCheckable(true);
    sw->setToolTip(QString("Reverse the expression pedal: heel and toe swap for every MIDI "
                           "controller mapped to %1")
                       .arg(knob_name));
    put_in_stereo_column(card, make_caption_switch(sw, "Reverse"));
    return sw;
}

void MainWindow::setup_ui() {
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* root_layout = new QVBoxLayout(central);
    root_layout->setContentsMargins(8, 4, 8, 6);
    root_layout->setSpacing(4);

    // Menu bar
    QMenu* library_menu = nullptr;
    QMenu* preset_menu = nullptr;
    QMenu* midi_top_menu = nullptr;
    {
        QMenuBar* mb = menuBar();
        mb->setCornerWidget(make_made_in_chile(), Qt::TopRightCorner);
        preset_menu = mb->addMenu("Preset");
        QAction* act_save = preset_menu->addAction("Save");
        act_save->setShortcut(QKeySequence::Save); // Ctrl+S
        connect(act_save, &QAction::triggered, this, &MainWindow::on_save_preset);
        QAction* act_save_as = preset_menu->addAction("Save As…");
        act_save_as->setShortcut(QKeySequence("Ctrl+Shift+S"));
        connect(act_save_as, &QAction::triggered, this, &MainWindow::on_save_preset_as);
        QAction* act_load = preset_menu->addAction("Open…");
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
        midi_top_menu = mb->addMenu("MIDI"); // filled by setupMidi

        QMenu* settings_menu = mb->addMenu("Settings");
        QAction* act_settings = settings_menu->addAction("Settings…");
        act_settings->setShortcut(QKeySequence("Ctrl+,"));
        connect(act_settings, &QAction::triggered, this, [this] {
            open_settings(SettingsDialog::Page::AUDIO);
        });
        connect(settings_menu->addAction("Keyboard Shortcuts…"), &QAction::triggered, this, [this] {
            open_settings(SettingsDialog::Page::SHORTCUTS);
        });

        QMenu* help_menu = mb->addMenu("Help");
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
    preset_bar = new PresetBar(*library_index);
    root_layout->addWidget(preset_bar);
    connect(preset_bar, &PresetBar::preset_chosen, this, [this](const QString& p) {
        load_preset_file(p);
    });
    connect(preset_bar, &PresetBar::step_requested, this, &MainWindow::step_preset);
    connect(preset_bar, &PresetBar::save_requested, this, &MainWindow::on_save_preset);
    connect(preset_bar, &PresetBar::save_as_requested, this, &MainWindow::on_save_preset_as);
    connect(preset_bar, &PresetBar::ab_switch_requested, this, &MainWindow::switch_ab);
    connect(preset_bar, &PresetBar::ab_copy_requested, this, &MainWindow::copy_ab);

    // ---- Tempo bar: global tempo (session setting) + metronome.
    tempo_bar = new TempoBar(engine->get_metronome());
    bpm = tempo_bar->get_bpm();
    root_layout->addWidget(tempo_bar);
    connect(tempo_bar, &TempoBar::bpm_changed, this, &MainWindow::set_tempo);

    // ---- Status bar (bottom right): backend, rate, buffer, latency, devices.
    audio_bar = new AudioStatusBar(*engine);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(audio_bar);
    lbl_rig_status = new QLabel;
    lbl_rig_status->setObjectName("statusItem");
    lbl_rig_status->setTextFormat(Qt::RichText);
    statusBar()->addWidget(lbl_rig_status);
    connect(tone3000, &Tone3000Client::auth_changed, this, &MainWindow::update_rig_status);
    connect(tone3000, &Tone3000Client::user_changed, this, &MainWindow::update_rig_status);
    connect(audio_bar, &AudioStatusBar::device_selection_changed, this,
            &MainWindow::restart_audio_with_current_devices);
    connect(audio_bar, &AudioStatusBar::reconnect_requested, this,
            &MainWindow::restart_audio_with_current_devices);

    // Scrollable effect strip
    // (NAM model / IR file pickers live inside the Amp and IR rows below.)
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    strip = new QWidget;
    strip_lay = new QVBoxLayout(strip);
    strip_lay->setContentsMargins(2, 2, 2, 2);
    strip_lay->setSpacing(4);
    strip_lay->setAlignment(Qt::AlignTop);

    // Effect columns are created once here, keyed by EffectBase*. They are
    // NOT added to the layout — rebuildEffectStrip lays them out in engine
    // chain order, so audio order and UI order stay in sync automatically.

    // Tuner
    {
        QFrame* col = make_row_card("Tuner");
        QHBoxLayout* lay = row_layout(col);

        sw_tuner_enable = new ToggleSwitch;
        sw_tuner_enable->setCheckable(true);
        lay->addWidget(make_enable_slot(sw_tuner_enable), 0, Qt::AlignVCenter);
        lay->addWidget(make_stereo_slot(nullptr), 0, Qt::AlignVCenter);
        lay->addWidget(make_sync_slot(), 0, Qt::AlignVCenter);

        tuner_display = new TunerDisplay;
        tuner_display->setMaximumWidth(560); // one compact instrument, not a strip
        lay->addWidget(tuner_display, 1, Qt::AlignVCenter);

        // Reference pitch + Hz readout (Settings → Tuner): machine settings (QSettings tuner/*),
        // not part of presets or the registry.
        QSettings s;
        tuner_ref_hz = std::clamp(s.value("tuner/reference", 440).toInt(), 420, 460);
        tuner_reference_box = new NumberField(
            {.minimum = 420, .maximum = 460, .decimals = 0, .step = 1, .unit = "Hz"});
        NumberField* ref = tuner_reference_box;
        ref->set_value(tuner_ref_hz);
        ref->setToolTip("Reference pitch (concert pitch is 440 Hz): drag up/down or scroll to "
                        "change, click to type");
        connect(ref, &NumberField::value_changed, this, [this](double hz) {
            set_tuner_reference(qRound(hz));
        });
        tuner_display->set_reference(tuner_ref_hz);

        tuner_display->set_show_hz(s.value("tuner/showHz", false).toBool());

        lay->addSpacing(8);
        lay->addWidget(ref, 0, Qt::AlignVCenter);
        lay->addStretch();

        Theme::tag_card(col, "tuner"); // after children — pushes accents to them
        fx_cols[&engine->get_tuner()] = col;
    }

    // Transpose
    {
        knob_transpose_semis =
            late_knob("transpose", "semitones", new KnobWidget("Semitones", -12.0, 12.0, 0.0),
                      [this](double v) {
                          engine->get_transpose().set_semitones(static_cast<float>(v));
                      });
        KnobWidget* semis = knob_transpose_semis;
        semis->set_snap(1.0); // Discrete until the Continuous switch says otherwise
        semis->set_scroll_step(0.1); // used in continuous mode
        semis->set_value_formatter(&format_semitones);
        semis->setToolTip("Pitch shift in semitones: −1 / −2 for drop tunings, ±12 for octaves "
                          "(adds ~25 ms latency when not 0)");
        KnobWidget* mix = late_knob(
            "transpose", "mix", new KnobWidget("Mix", 0.0, 100.0, 100.0, "%"), [this](double v) {
                engine->get_transpose().set_mix_pct(static_cast<float>(v));
            });
        mix->set_scroll_step(1.0);
        QWidget* col = make_effect_row("Transpose", sw_transpose_enable, {semis, mix});
        sw_transpose_reverse = make_reverse_switch(col, "Semitones");
        sw_transpose_continuous = new ToggleSwitch;
        sw_transpose_continuous->setCheckable(true);
        sw_transpose_continuous->setToolTip(
            "Continuous: Semitones glides smoothly (whammy-style bends with an expression "
            "pedal) · off = Discrete: whole semitones");
        put_in_sync_column(col, make_caption_switch(sw_transpose_continuous, "Continuous"));
        Theme::tag_card(col, "transpose");
        fx_cols[&engine->get_transpose()] = col;
    }

    // Noise Gate
    {
        using namespace NAMConstants;
        knob_gate_thresh = new KnobWidget("Threshold", GATE_THRESH_MIN, GATE_THRESH_MAX,
                                          GATE_THRESH_DEFAULT, "dB");
        knob_gate_attack =
            new KnobWidget("Attack", GATE_ATTACK_MIN, GATE_ATTACK_MAX, GATE_ATTACK_DEFAULT, "ms");
        knob_gate_release = new KnobWidget("Release", GATE_RELEASE_MIN, GATE_RELEASE_MAX,
                                           GATE_RELEASE_DEFAULT, "ms");
        knob_gate_thresh->set_scroll_step(1.0);
        knob_gate_attack->set_scroll_step(1.0);
        knob_gate_release->set_scroll_step(10.0);

        QWidget* col = make_effect_row("Noise Gate", sw_gate_enable,
                                       {knob_gate_thresh, knob_gate_attack, knob_gate_release});
        gr_gate = new GainReductionMeter(40.0f, Theme::accent("gate"));
        place_across_switch_columns(col, gr_gate);
        Theme::tag_card(col, "gate");

        fx_cols[&engine->get_gate()] = col;
    }

    // Compressor
    {
        using namespace NAMConstants;
        knob_comp_thresh = new KnobWidget("Threshold", COMP_THRESH_MIN, COMP_THRESH_MAX,
                                          COMP_THRESH_DEFAULT, "dB");
        knob_comp_ratio =
            new KnobWidget("Ratio", COMP_RATIO_MIN, COMP_RATIO_MAX, COMP_RATIO_DEFAULT, ":1");
        knob_comp_attack =
            new KnobWidget("Attack", COMP_ATTACK_MIN, COMP_ATTACK_MAX, COMP_ATTACK_DEFAULT, "ms");
        knob_comp_release = new KnobWidget("Release", COMP_RELEASE_MIN, COMP_RELEASE_MAX,
                                           COMP_RELEASE_DEFAULT, "ms");
        knob_comp_makeup = new KnobWidget("Makeup Gain", COMP_MAKEUP_MIN, COMP_MAKEUP_MAX,
                                          COMP_MAKEUP_DEFAULT, "dB");
        knob_comp_thresh->set_scroll_step(1.0);
        knob_comp_ratio->set_scroll_step(0.5);
        knob_comp_attack->set_scroll_step(1.0);
        knob_comp_release->set_scroll_step(10.0);
        knob_comp_makeup->set_scroll_step(0.5);

        QWidget* col = make_effect_row("Compressor", sw_comp_enable,
                                       {knob_comp_thresh, knob_comp_ratio, knob_comp_attack,
                                        knob_comp_release, knob_comp_makeup});
        gr_comp = new GainReductionMeter(20.0f, Theme::accent("comp"));
        place_across_switch_columns(col, gr_comp);
        Theme::tag_card(col, "comp");

        fx_cols[&engine->get_comp()] = col;
    }

    // Dynamic Wah
    {
        using namespace NAMConstants;
        knob_wah_sens =
            new KnobWidget("Sensitivity", WAH_SENS_MIN, WAH_SENS_MAX, WAH_SENS_DEFAULT, "%");
        knob_wah_res = new KnobWidget("Resonance", WAH_RES_MIN, WAH_RES_MAX, WAH_RES_DEFAULT, "%");
        knob_wah_mix = new KnobWidget("Mix", WAH_MIX_MIN, WAH_MIX_MAX, WAH_MIX_DEFAULT, "%");
        knob_wah_sens->set_scroll_step(1.0);
        knob_wah_res->set_scroll_step(1.0);
        knob_wah_mix->set_scroll_step(1.0);

        // Mode: Auto, or Manual when switched on (registered as "dynamicWah/mode").
        sw_wah_manual = new ToggleSwitch;
        sw_wah_manual->setCheckable(true);
        sw_wah_manual->setToolTip("Manual: Position sweeps the wah (map an expression pedal) · "
                                  "off = Auto: your playing sweeps it");
        // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): the Mode knob
        // this switch replaced, kept for reference.
        // KnobWidget* mode =
        //     late_knob("dynamicWah", "mode", new KnobWidget("Mode", 0, 1, 0), [this](double v) {
        //         engine->get_wah().set_pedal_mode(v > 0.5);
        //     });
        // mode->set_snap(1.0);
        // mode->set_value_formatter([](double v) {
        //     return v > 0.5 ? QString("Pedal") : QString("Auto");
        // });
        // mode->setToolTip("Auto: the envelope sweeps the wah · Pedal: Position sweeps it (map an "
        //                  "expression pedal)");
        KnobWidget* pos = late_knob("dynamicWah", "position",
                                    new KnobWidget("Position", WAH_POSITION_MIN, WAH_POSITION_MAX,
                                                   WAH_POSITION_DEFAULT, "%"),
                                    [this](double v) {
                                        engine->get_wah().set_position_pct(static_cast<float>(v));
                                    });
        pos->set_scroll_step(1.0);
        QWidget* col = make_effect_row("Dynamic Wah", sw_wah_enable,
                                       {knob_wah_mix, knob_wah_sens, knob_wah_res, pos});
        sw_wah_reverse = make_reverse_switch(col, "Position");
        put_in_sync_column(col, make_caption_switch(sw_wah_manual, "Manual"));
        Theme::tag_card(col, "wah");

        fx_cols[&engine->get_wah()] = col;
    }

    // Drive (TS-style boost in front of the amp)
    {
        using namespace NAMConstants;
        knob_drive = new KnobWidget("Drive", DRIVE_MIN, DRIVE_MAX, DRIVE_DEFAULT, "%");
        knob_drive_tone =
            new KnobWidget("Tone", DRIVE_TONE_MIN, DRIVE_TONE_MAX, DRIVE_TONE_DEFAULT, "%");
        knob_drive_level =
            new KnobWidget("Level", DRIVE_LEVEL_MIN, DRIVE_LEVEL_MAX, DRIVE_LEVEL_DEFAULT, "dB");
        knob_drive->set_scroll_step(1.0);
        knob_drive_tone->set_scroll_step(1.0);
        knob_drive_level->set_scroll_step(0.5);
        QWidget* col = make_effect_row("Drive", sw_drive_enable,
                                       {knob_drive, knob_drive_tone, knob_drive_level});
        Theme::tag_card(col, "drive");
        fx_cols[&engine->get_drive()] = col;
    }

    // Amp (NAM)
    {
        QWidget* col = make_effect_row("Amplifier", sw_nam_enable, {}, &sw_nam_stereo);
        sw_nam_stereo->setToolTip("Stereo input: run the amp per channel (twice the CPU).\n"
                                  "Off: a stereo effect before the amp is summed to mono first.");
        sw_nam_enable->setChecked(true);
        make_hero_controls(col, LibraryEntry::Kind::MODEL, lbl_nam_status, lbl_nam_meta,
                           lbl_nam_origin);

        Theme::tag_card(col, "nam");
        fx_cols[&engine->get_nam()] = col;
    }

    // EQ
    {
        using namespace NAMConstants;
        knob_low = new KnobWidget("Bass", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB");
        knob_mid = new KnobWidget("Middle", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB");
        knob_treble = new KnobWidget("Treble", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB");
        knob_low->set_scroll_step(0.1);
        knob_mid->set_scroll_step(0.1);
        knob_treble->set_scroll_step(0.1);

        auto eq_set = [this](EQProcessor::Band b, bool gain) {
            return [this, b, gain](double v) {
                gain ? engine->get_eq().set_gain(b, v) : engine->get_eq().set_freq(b, v);
            };
        };
        KnobWidget* hi_mid =
            late_knob("eq", "highMid",
                      new KnobWidget("High-Mid", EQ_BAND_MIN, EQ_BAND_MAX, EQ_BAND_DEFAULT, "dB"),
                      eq_set(EQProcessor::HIGH_MID, true));
        hi_mid->set_scroll_step(0.1);
        QWidget* col =
            make_effect_row("Equalizer", sw_eq_enable, {knob_low, knob_mid, hi_mid, knob_treble});
        add_detail_row(
            col,
            {
                make_eq_freq_knob("lowFreq", "Bass Freq", EQ_BASS_FREQ_MIN, EQ_BASS_FREQ_MAX,
                                  EQ_BASS_FREQ_DEFAULT, EQProcessor::BASS, 0),
                make_eq_freq_knob("midFreq", "Mid Freq", EQ_MID_FREQ_MIN, EQ_MID_FREQ_MAX,
                                  EQ_MID_FREQ_DEFAULT, EQProcessor::MIDDLE, 0),
                make_eq_freq_knob("highMidFreq", "High-Mid Freq", EQ_HI_MID_FREQ_MIN,
                                  EQ_HI_MID_FREQ_MAX, EQ_HI_MID_FREQ_DEFAULT, EQProcessor::HIGH_MID,
                                  0),
                make_eq_freq_knob("trebleFreq", "Treble Freq", EQ_TREBLE_FREQ_MIN,
                                  EQ_TREBLE_FREQ_MAX, EQ_TREBLE_FREQ_DEFAULT, EQProcessor::TREBLE,
                                  0),
                make_eq_freq_knob("lowCut", "Low Cut", LOW_CUT_MIN, LOW_CUT_MAX, LOW_CUT_DEFAULT,
                                  EQProcessor::LOW_CUT, LOW_CUT_MIN + 0.5),
                make_eq_freq_knob("highCut", "High Cut", HIGH_CUT_MIN, HIGH_CUT_MAX,
                                  HIGH_CUT_DEFAULT, EQProcessor::HIGH_CUT, -(HIGH_CUT_MAX - 1.0)),
            });
        sw_eq_enable->setChecked(true);
        Theme::tag_card(col, "eq");
        fx_cols[&engine->get_eq()] = col;
    }

    // Volume pedal
    {
        using namespace NAMConstants;
        knob_vol_pos =
            new KnobWidget("Volume", VOLUME_POS_MIN, VOLUME_POS_MAX, VOLUME_POS_DEFAULT, "%");
        knob_vol_mid =
            new KnobWidget("Mid", VOLUME_MID_MIN, VOLUME_MID_MAX, VOLUME_MID_DEFAULT, "dB");
        knob_vol_pos->set_scroll_step(1.0);
        knob_vol_mid->set_scroll_step(1.0);
        knob_vol_mid->setToolTip("Level at half travel: lower = swells stay quiet longer");
        QWidget* col =
            make_effect_row("Volume Pedal", sw_volume_enable, {knob_vol_pos, knob_vol_mid});
        sw_volume_reverse = make_reverse_switch(col, "Volume");
        Theme::tag_card(col, "volume");
        fx_cols[&engine->get_volume()] = col;
    }

    // Flanger
    {
        using namespace NAMConstants;
        knob_fl_rate =
            new KnobWidget("Rate", FLANGER_RATE_MIN, FLANGER_RATE_MAX, FLANGER_RATE_DEFAULT, "Hz");
        knob_fl_depth = new KnobWidget("Depth", FLANGER_DEPTH_MIN, FLANGER_DEPTH_MAX,
                                       FLANGER_DEPTH_DEFAULT, "%");
        knob_fl_feedback = new KnobWidget("Feedback", FLANGER_FEEDBACK_MIN, FLANGER_FEEDBACK_MAX,
                                          FLANGER_FEEDBACK_DEFAULT, "%");
        knob_fl_mix =
            new KnobWidget("Mix", FLANGER_MIX_MIN, FLANGER_MIX_MAX, FLANGER_MIX_DEFAULT, "%");
        knob_fl_rate->set_scroll_step(0.1);
        knob_fl_depth->set_scroll_step(1.0);
        knob_fl_feedback->set_scroll_step(1.0);
        knob_fl_mix->set_scroll_step(1.0);

        QWidget* col = make_effect_row("Flanger", sw_flanger_enable,
                                       {knob_fl_mix, knob_fl_rate, knob_fl_depth, knob_fl_feedback},
                                       &sw_flanger_stereo);
        add_tempo_sync(col, knob_fl_rate, "Rate", false, 1 /* 2 bars */, FLANGER_RATE_MIN,
                       FLANGER_RATE_MAX, [this](double v) {
                           engine->get_flanger().set_rate_hz(static_cast<float>(v));
                       });
        KnobWidget* range =
            late_knob("flanger", "range",
                      new KnobWidget("Range", FLANGER_RANGE_MIN, FLANGER_RANGE_MAX,
                                     FLANGER_RANGE_DEFAULT, "%"),
                      [this](double v) {
                          engine->get_flanger().set_range_pct(static_cast<float>(v));
                      });
        range->set_scroll_step(1.0);
        range->setToolTip("Sweep length: low = short, metallic · high = long jet sweep");
        append_to_row(col, range);
        Theme::tag_card(col, "flanger");

        fx_cols[&engine->get_flanger()] = col;
    }

    // Phaser
    {
        using namespace NAMConstants;
        knob_ph_rate =
            new KnobWidget("Rate", PHASER_RATE_MIN, PHASER_RATE_MAX, PHASER_RATE_DEFAULT, "Hz");
        knob_ph_depth =
            new KnobWidget("Depth", PHASER_DEPTH_MIN, PHASER_DEPTH_MAX, PHASER_DEPTH_DEFAULT, "%");
        knob_ph_feedback = new KnobWidget("Feedback", PHASER_FEEDBACK_MIN, PHASER_FEEDBACK_MAX,
                                          PHASER_FEEDBACK_DEFAULT, "%");
        knob_ph_mix =
            new KnobWidget("Mix", PHASER_MIX_MIN, PHASER_MIX_MAX, PHASER_MIX_DEFAULT, "%");
        knob_ph_rate->set_scroll_step(0.1);
        knob_ph_depth->set_scroll_step(1.0);
        knob_ph_feedback->set_scroll_step(1.0);
        knob_ph_mix->set_scroll_step(1.0);

        QWidget* col = make_effect_row("Phaser", sw_phaser_enable,
                                       {knob_ph_mix, knob_ph_rate, knob_ph_depth, knob_ph_feedback},
                                       &sw_phaser_stereo);
        add_tempo_sync(col, knob_ph_rate, "Rate", false, 4 /* 1/2 */, PHASER_RATE_MIN,
                       PHASER_RATE_MAX, [this](double v) {
                           engine->get_phaser().set_rate_hz(static_cast<float>(v));
                       });
        // Mode: Classic, or Vibe when switched on (registered as "phaser/mode").
        sw_phaser_vibe = new ToggleSwitch;
        sw_phaser_vibe->setCheckable(true);
        sw_phaser_vibe->setToolTip(
            "Vibe: staggered stages, lamp-shaped sweep, no feedback (off = classic phaser)");
        append_to_row(col, make_caption_switch(sw_phaser_vibe, "Vibe"));
        // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): the Mode knob
        // this switch replaced, kept for reference.
        // KnobWidget* mode =
        //     late_knob("phaser", "mode", new KnobWidget("Mode", 0, 1, 0), [this](double v) {
        //         engine->get_phaser().set_vibe(v > 0.5);
        //     });
        // mode->set_snap(1.0);
        // mode->set_value_formatter([](double v) {
        //     return v > 0.5 ? QString("Vibe") : QString("Classic");
        // });
        // mode->setToolTip(
        //     "Classic phaser, or Vibe: staggered stages, lamp-shaped sweep, no feedback");
        // append_to_row(col, mode);
        Theme::tag_card(col, "phaser");

        fx_cols[&engine->get_phaser()] = col;
    }

    // Chorus
    {
        using namespace NAMConstants;
        knob_ch_rate =
            new KnobWidget("Rate", CHORUS_RATE_MIN, CHORUS_RATE_MAX, CHORUS_RATE_DEFAULT, "Hz");
        knob_ch_depth =
            new KnobWidget("Depth", CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX, CHORUS_DEPTH_DEFAULT, "%");
        knob_ch_mix =
            new KnobWidget("Mix", CHORUS_MIX_MIN, CHORUS_MIX_MAX, CHORUS_MIX_DEFAULT, "%");
        knob_ch_rate->set_scroll_step(0.1);
        knob_ch_depth->set_scroll_step(1.0);
        knob_ch_mix->set_scroll_step(1.0);

        QWidget* col =
            make_effect_row("Chorus", sw_chorus_enable, {knob_ch_mix, knob_ch_rate, knob_ch_depth},
                            &sw_chorus_stereo);
        add_tempo_sync(col, knob_ch_rate, "Rate", false, 2 /* 1/1 */, CHORUS_RATE_MIN,
                       CHORUS_RATE_MAX, [this](double v) {
                           engine->get_chorus().set_rate_hz(static_cast<float>(v));
                       });
        // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): Chorus modes are
        // off for now; the chorus stays in its first mode, Normal (the engine default).
        // KnobWidget* mode =
        //     late_knob("chorus", "mode", new KnobWidget("Mode", 0, 3, 0), [this](double v) {
        //         engine->get_chorus().set_mode(static_cast<int>(std::lround(v)));
        //     });
        // mode->set_snap(1.0);
        // mode->set_value_formatter(&format_chorus_mode);
        // mode->setToolTip("Normal · Tri (three voices) · Vintage (darker, triangle LFO) · "
        //                  "Spatial (wider stereo)");
        // append_to_row(col, mode);
        Theme::tag_card(col, "chorus");

        fx_cols[&engine->get_chorus()] = col;
    }

    // Tremolo
    {
        using namespace NAMConstants;
        knob_tr_rate =
            new KnobWidget("Rate", TREMOLO_RATE_MIN, TREMOLO_RATE_MAX, TREMOLO_RATE_DEFAULT, "Hz");
        knob_tr_depth = new KnobWidget("Depth", TREMOLO_DEPTH_MIN, TREMOLO_DEPTH_MAX,
                                       TREMOLO_DEPTH_DEFAULT, "%");
        knob_tr_rate->set_scroll_step(0.1);
        knob_tr_depth->set_scroll_step(1.0);

        QWidget* col = make_effect_row("Tremolo", sw_tremolo_enable, {knob_tr_rate, knob_tr_depth},
                                       &sw_tremolo_stereo);
        add_tempo_sync(col, knob_tr_rate, "Rate", false, 10 /* 1/8 */, TREMOLO_RATE_MIN,
                       TREMOLO_RATE_MAX, [this](double v) {
                           engine->get_tremolo().set_rate_hz(static_cast<float>(v));
                       });
        Theme::tag_card(col, "tremolo");

        fx_cols[&engine->get_tremolo()] = col;
    }

    // Delay
    {
        using namespace NAMConstants;
        knob_del_time =
            new KnobWidget("Time", DELAY_TIME_MIN, DELAY_TIME_MAX, DELAY_TIME_DEFAULT, "ms");
        knob_del_feedback = new KnobWidget("Feedback", DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX,
                                           DELAY_FEEDBACK_DEFAULT, "%");
        knob_del_mix = new KnobWidget("Mix", DELAY_MIX_MIN, DELAY_MIX_MAX, DELAY_MIX_DEFAULT, "%");
        knob_del_time->set_scroll_step(1.0);
        knob_del_feedback->set_scroll_step(1.0);
        knob_del_mix->set_scroll_step(1.0);

        QWidget* col =
            make_effect_row("Delay", sw_delay_enable,
                            {knob_del_mix, knob_del_time, knob_del_feedback}, &sw_delay_stereo);
        add_tempo_sync(col, knob_del_time, "Time", true, 8 /* 1/8. */, DELAY_TIME_MIN,
                       DELAY_TIME_MAX, [this](double v) {
                           engine->get_delay().set_time(static_cast<float>(v));
                       });
        KnobWidget* low_cut = late_knob(
            "delay", "lowCut",
            new KnobWidget("Low Cut", LOW_CUT_MIN, DELAY_LOW_CUT_MAX, LOW_CUT_DEFAULT, "Hz"),
            [this](double v) {
                engine->get_delay().set_low_cut(static_cast<float>(v));
            });
        low_cut->set_log_taper(true);
        low_cut->set_value_formatter(hz_label(LOW_CUT_MIN + 0.5));
        KnobWidget* high_cut = late_knob(
            "delay", "highCut",
            new KnobWidget("High Cut", DELAY_HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT, "Hz"),
            [this](double v) {
                engine->get_delay().set_high_cut(static_cast<float>(v));
            });
        high_cut->set_log_taper(true);
        high_cut->set_value_formatter(hz_label(-(HIGH_CUT_MAX - 1.0)));
        add_detail_row(
            col,
            {
                make_delay_pct_knob(
                    "rightRatio", "R Time", DELAY_RIGHT_MIN, DELAY_RIGHT_MAX, DELAY_RIGHT_DEFAULT,
                    [this](double v) {
                        engine->get_delay().set_right_ratio(static_cast<float>(v));
                    },
                    "Right repeat time as a share of Time (stereo): 75 % or 150 % for a wide, "
                    "uneven bounce"),
                // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): Cross-feed is
                // off — the delay stays ping-pong (its 100 % default).
                // make_delay_pct_knob(
                //     "crossFeed", "Cross-feed", DELAY_CROSS_MIN, DELAY_CROSS_MAX,
                //     DELAY_CROSS_DEFAULT,
                //     [this](double v) {
                //         engine->get_delay().set_cross_feed(static_cast<float>(v));
                //     },
                //     "100 % = ping-pong between the sides · 0 % = two separate echoes"),
                low_cut,
                high_cut,
                make_delay_pct_knob(
                    "saturation", "Saturation", DELAY_SAT_MIN, DELAY_SAT_MAX, DELAY_SAT_DEFAULT,
                    [this](double v) {
                        engine->get_delay().set_saturation(static_cast<float>(v));
                    },
                    "Tape-style rounding of the repeats as they build up"),
                // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): Modulation is
                // off; repeats stay steady (its 0 % default).
                // make_delay_pct_knob(
                //     "modulation", "Modulation", DELAY_MOD_MIN, DELAY_MOD_MAX, DELAY_MOD_DEFAULT,
                //     [this](double v) {
                //         engine->get_delay().set_modulation(static_cast<float>(v));
                //     },
                //     "Wobble on the repeats (tape flutter / chorused echoes)"),
                // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): Crystals is off
                // (pitch shifter quality); repeats stay at their pitch.
                // make_delay_pct_knob(
                //     "crystals", "Crystals", 0.0, 100.0, 0.0,
                //     [this](double v) {
                //         engine->get_delay().set_crystals(static_cast<float>(v));
                //     },
                //     "Octave-up repeats: each echo climbs higher"),
            });
        sw_delay_stereo->setChecked(true);
        Theme::tag_card(col, "delay");

        fx_cols[&engine->get_delay()] = col;
    }

    // Reverb
    {
        using namespace NAMConstants;
        knob_rev_room =
            new KnobWidget("Decay", REVERB_ROOM_MIN, REVERB_ROOM_MAX, REVERB_ROOM_DEFAULT, "%");
        knob_rev_damping = new KnobWidget("Damping", REVERB_DAMPING_MIN, REVERB_DAMPING_MAX,
                                          REVERB_DAMPING_DEFAULT, "%");
        knob_rev_width =
            new KnobWidget("Width", REVERB_WIDTH_MIN, REVERB_WIDTH_MAX, REVERB_WIDTH_DEFAULT, "%");
        knob_rev_mix =
            new KnobWidget("Mix", REVERB_MIX_MIN, REVERB_MIX_MAX, REVERB_MIX_DEFAULT, "%");
        knob_rev_room->set_scroll_step(1.0);
        knob_rev_damping->set_scroll_step(1.0);
        knob_rev_width->set_scroll_step(1.0);
        knob_rev_mix->set_scroll_step(1.0);

        QWidget* col =
            make_effect_row("Reverb", sw_reverb_enable,
                            {knob_rev_mix, knob_rev_room, knob_rev_damping, knob_rev_width});
        KnobWidget* pre =
            late_knob("reverb", "preDelay",
                      new KnobWidget("Pre-delay", REVERB_PRE_DELAY_MIN, REVERB_PRE_DELAY_MAX,
                                     REVERB_PRE_DELAY_DEFAULT, "ms"),
                      [this](double v) {
                          engine->get_reverb().set_pre_delay(static_cast<float>(v));
                      });
        pre->set_scroll_step(5.0);
        pre->setToolTip("Gap before the tail starts: keeps the note clear of the room");
        KnobWidget* low_cut = late_knob(
            "reverb", "lowCut",
            new KnobWidget("Low Cut", LOW_CUT_MIN, REVERB_LOW_CUT_MAX, LOW_CUT_DEFAULT, "Hz"),
            [this](double v) {
                engine->get_reverb().set_low_cut(static_cast<float>(v));
            });
        low_cut->set_log_taper(true);
        low_cut->set_value_formatter(hz_label(LOW_CUT_MIN + 0.5));
        KnobWidget* high_cut = late_knob(
            "reverb", "highCut",
            new KnobWidget("High Cut", REVERB_HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT, "Hz"),
            [this](double v) {
                engine->get_reverb().set_high_cut(static_cast<float>(v));
            });
        high_cut->set_log_taper(true);
        high_cut->set_value_formatter(hz_label(-(HIGH_CUT_MAX - 1.0)));
        // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): Shimmer is off
        // for now (the pitch shifter isn't good enough); the reverb runs without it.
        // KnobWidget* shimmer = late_knob(
        //     "reverb", "shimmer", new KnobWidget("Shimmer", 0.0, 100.0, 0.0, "%"), [this](double v) {
        //         engine->get_reverb().set_shimmer_pct(static_cast<float>(v));
        //     });
        // shimmer->set_scroll_step(1.0);
        // shimmer->setToolTip("Octave-up feedback into the tail — the shimmer reverb");
        add_detail_row(col, {pre, low_cut, high_cut});
        Theme::tag_card(col, "reverb");

        fx_cols[&engine->get_reverb()] = col;
    }

    // IR toggle
    {
        QWidget* col = make_effect_row("Cabinet", sw_ir_enable, {});
        sw_ir_enable->setChecked(true);
        make_hero_controls(col, LibraryEntry::Kind::IR, lbl_ir_status, lbl_ir_meta, lbl_ir_origin);

        // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): the Cabinet's
        // advanced options (per-IR level/pan/phase, blend, cuts, slot B) are off; it is a
        // single-IR cab at neutral settings.
        // // Dual cab (Phase 17): A/B mixer + cuts, then slot B's picker.
        // using namespace NAMConstants;
        // KnobWidget* blend = late_knob("ir", "blend", new KnobWidget("Blend", 0.0, 100.0, 50.0, "%"),
        //                               [this](double v) {
        //                                   engine->get_ir().set_blend_pct(static_cast<float>(v));
        //                               });
        // blend->set_scroll_step(1.0);
        // blend->set_value_formatter(&format_blend);
        // blend->setToolTip("Crossfade between the two IRs (only when both slots are loaded)");
        // KnobWidget* low_cut =
        //     late_knob("ir", "lowCut",
        //               new KnobWidget("Low Cut", LOW_CUT_MIN, LOW_CUT_MAX, LOW_CUT_DEFAULT, "Hz"),
        //               [this](double v) {
        //                   engine->get_ir().set_low_cut(static_cast<float>(v));
        //               });
        // low_cut->set_log_taper(true);
        // low_cut->set_value_formatter(hz_label(LOW_CUT_MIN + 0.5));
        // KnobWidget* high_cut = late_knob(
        //     "ir", "highCut",
        //     new KnobWidget("High Cut", HIGH_CUT_MIN, HIGH_CUT_MAX, HIGH_CUT_DEFAULT, "Hz"),
        //     [this](double v) {
        //         engine->get_ir().set_high_cut(static_cast<float>(v));
        //     });
        // high_cut->set_log_taper(true);
        // high_cut->set_value_formatter(hz_label(-(HIGH_CUT_MAX - 1.0)));
        // add_detail_row(col, {make_ir_level_knob(0), make_ir_pan_knob(0), make_ir_phase_switch(0),
        //                      blend, low_cut, high_cut});
        //
        // // Slot B: ‹ › through the library's IRs, Choose… (file dialog), ×.
        // auto* b_title = new QLabel("IR B");
        // b_title->setObjectName("fxName");
        // auto* choose = new QPushButton("Choose…");
        // choose->setFocusPolicy(Qt::NoFocus);
        // connect(choose, &QPushButton::clicked, this, [this] {
        //     choose_ir_b();
        // });
        // auto* clear = new QToolButton;
        // clear->setObjectName("heroArrow");
        // clear->setText("×");
        // clear->setAutoRaise(true);
        // clear->setFocusPolicy(Qt::NoFocus);
        // clear->setToolTip("Empty slot B");
        // connect(clear, &QToolButton::clicked, this, [this] {
        //     clear_ir_b();
        // });
        // lbl_irb_status = new QLabel;
        // lbl_irb_status->setObjectName("fileStatus");
        // add_detail_row(col, {b_title, make_ir_b_arrow("‹", -1), make_ir_b_arrow("›", +1), choose,
        //                      clear, lbl_irb_status, make_ir_level_knob(1), make_ir_pan_knob(1),
        //                      make_ir_phase_switch(1)});

        Theme::tag_card(col, "ir");
        fx_cols[&engine->get_ir()] = col;
    }

    // Input/Output gain columns. Not draggable and not part of the chain, so
    // they flank the strip: input on the left, output on the right — signal
    // flows in → chain → out. The full-height meter reads bottom→top.

    input_card = make_gain_column("Input", knob_input_gain, meter_in);
    output_card = make_gain_column("Output", knob_output_gain, meter_out);
    {
        // Output limiter switch under the gain knob.
        sw_limiter = new ToggleSwitch;
        sw_limiter->setCheckable(true);
        sw_limiter->setToolTip(
            "Look-ahead limiter: catches peaks at −0.3 dBFS (adds 1 ms latency)");
        sw_limiter->set_accent(Theme::generic_accent());
        auto* row = new QWidget;
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(2);
        auto* cap = new QLabel("Limit");
        cap->setObjectName("toggleCaption");
        cap->setAlignment(Qt::AlignHCenter);
        rl->addWidget(sw_limiter, 0, Qt::AlignHCenter);
        rl->addWidget(cap);
        static_cast<QVBoxLayout*>(output_card->layout())->insertWidget(2, row, 0, Qt::AlignHCenter);
    }

    // Keyboard shortcut targets per card; keys are (re)assigned left to right
    // on every strip rebuild so they always match the visual order.
    fx_toggles = {
        {&engine->get_tuner(), sw_tuner_enable},
        {&engine->get_gate(), sw_gate_enable},
        {&engine->get_comp(), sw_comp_enable},
        {&engine->get_wah(), sw_wah_enable},
        {&engine->get_flanger(), sw_flanger_enable},
        {&engine->get_phaser(), sw_phaser_enable},
        {&engine->get_nam(), sw_nam_enable},
        {&engine->get_eq(), sw_eq_enable},
        {&engine->get_chorus(), sw_chorus_enable},
        {&engine->get_tremolo(), sw_tremolo_enable},
        {&engine->get_delay(), sw_delay_enable},
        {&engine->get_reverb(), sw_reverb_enable},
        {&engine->get_ir(), sw_ir_enable},
        {&engine->get_drive(), sw_drive_enable},
        {&engine->get_volume(), sw_volume_enable},
        {&engine->get_transpose(), sw_transpose_enable},
    };

    for (const auto& [fx, enable] : fx_toggles) {
        connect(enable, &ToggleSwitch::toggled, this, [this, fx](bool enabled) {
            update_card_bypass_state(fx, enabled);
        });
        update_card_bypass_state(fx, enable->isChecked());
    }

    // Drag sources + context menus: every card. Drop target: the strip itself.
    for (const auto& [fx, card] : fx_cols) {
        card->installEventFilter(this);
    }
    input_card->installEventFilter(this); // context menu only — not draggable
    output_card->installEventFilter(this);
    lbl_nam_origin->installEventFilter(this); // T3K mark click
    lbl_ir_origin->installEventFilter(this);
    strip->setAcceptDrops(true);
    strip->installEventFilter(this);

    rebuild_effect_strip();
    midi_menu = midi_top_menu;

    scroll->setWidget(strip);

    // Rack row: input column | effect strip | output column.
    auto* rack_row = new QHBoxLayout;
    rack_row->setSpacing(6);
    rack_row->addWidget(input_card);
    rack_row->addWidget(scroll, 1);
    rack_row->addWidget(output_card);
    root_layout->addLayout(rack_row, 1);

    // Library dock (left of the rig; closable, movable, remembers state)
    library_tabs = new LibraryDock(*library_index, *tone3000);
    library = library_tabs->get_local();
    t3k_panel = library_tabs->tone3000();
    connect(library, &LibraryPanel::activated, this, &MainWindow::load_library_entry);
    connect(t3k_panel, &Tone3000Panel::load_requested, this, &MainWindow::load_library_entry);
    connect(tone3000, &Tone3000Client::auth_finished, this, [this](bool ok, const QString&, int) {
        if (ok) {
            library_dock->show();
            library_tabs->set_tab(LibraryDock::TONE3000);
        }
    });

    library_dock = new QDockWidget("Library", this);
    library_dock->setObjectName("libraryDock"); // saveState() needs a name
    library_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    // No title bar: the Library / TONE3000 tabs head the dock; Ctrl+L shows and hides it.
    library_dock->setTitleBarWidget(new QWidget(library_dock));
    library_dock->setFeatures(QDockWidget::DockWidgetClosable);
    library_dock->setWidget(library_tabs);
    addDockWidget(Qt::LeftDockWidgetArea, library_dock);
    resizeDocks({library_dock}, {320}, Qt::Horizontal);

    {
        QAction* show = library_dock->toggleViewAction();
        show->setText("Show Library");
        show->setShortcut(QKeySequence("Ctrl+L"));
        library_menu->addAction(show);

        QAction* find = library_menu->addAction("Find in Library");
        find->setShortcut(QKeySequence::Find); // Ctrl+F
        connect(find, &QAction::triggered, this, [this] {
            find_in_library();
        });
        QAction* t3k = library_menu->addAction("Browse TONE3000…");
        t3k->setIcon(
            QIcon(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(14, Qt::SmoothTransformation)));
        connect(t3k, &QAction::triggered, this, [this] {
            browse_tone3000();
        });
        library_menu->addSeparator();
        connect(library_menu->addAction("Folders…"), &QAction::triggered, this, [this] {
            open_settings(SettingsDialog::Page::LIBRARY);
        });
        connect(library, &LibraryPanel::folders_requested, this, [this] {
            open_settings(SettingsDialog::Page::LIBRARY);
        });
        connect(library_menu->addAction("Rescan"), &QAction::triggered, library_index,
                &LibraryIndex::rescan);
        library_menu->addSeparator();
        connect(library_menu->addAction("Import Model…"), &QAction::triggered, this,
                &MainWindow::on_import_nam);
        connect(library_menu->addAction("Import Impulse Response…"), &QAction::triggered, this,
                &MainWindow::on_import_ir);
    }
}

void MainWindow::set_tuner_reference(int hz) {
    tuner_ref_hz = hz;
    tuner_display->set_reference(hz);
    QSettings().setValue("tuner/reference", hz);
}

void MainWindow::set_transpose_continuous(bool on) {
    engine->get_transpose().set_continuous(on);
    knob_transpose_semis->set_snap(on ? 0.0 : 1.0);
    if (!on) {
        knob_transpose_semis->set_value(knob_transpose_semis->value()); // round to a step
    }
}

void MainWindow::set_tuner_show_hz(bool on) {
    tuner_display->set_show_hz(on);
    QSettings().setValue("tuner/showHz", on);
}

// EQ band frequency knob (log taper, "Off" past off_at).
QWidget* MainWindow::make_eq_freq_knob(const QString& key, const QString& label, double lo,
                                       double hi, double def, EQProcessor::Band b, double off_at) {
    KnobWidget* k =
        late_knob("eq", key, new KnobWidget(label, lo, hi, def, "Hz"), [this, b](double v) {
            engine->get_eq().set_freq(b, v);
        });
    k->set_log_taper(true);
    k->set_value_formatter(hz_label(off_at));
    return static_cast<QWidget*>(k);
}

QWidget* MainWindow::make_delay_pct_knob(const QString& key, const QString& label, double lo,
                                         double hi, double def, std::function<void(double)> f,
                                         const QString& tip) {
    KnobWidget* k = late_knob("delay", key, new KnobWidget(label, lo, hi, def, "%"), std::move(f));
    k->set_scroll_step(1.0);
    k->setToolTip(tip);
    return static_cast<QWidget*>(k);
}

QWidget* MainWindow::make_ir_level_knob(int slot) {
    KnobWidget* k = late_knob("ir", slot ? "levelB" : "levelA",
                              new KnobWidget(slot ? "Level B" : "Level A", -24.0, 6.0, 0.0, "dB"),
                              [this, slot](double v) {
                                  engine->get_ir().set_level(slot, static_cast<float>(v));
                              });
    k->set_scroll_step(0.5);
    return static_cast<QWidget*>(k);
}

// Pan readout: "Center", "40 L", "25 R".
static QString format_pan(double v) {
    const int p = static_cast<int>(std::lround(v));
    return p == 0 ? QString("Center") : QString("%1 %2").arg(std::abs(p)).arg(p < 0 ? "L" : "R");
}

QWidget* MainWindow::make_ir_pan_knob(int slot) {
    KnobWidget* k = late_knob("ir", slot ? "panB" : "panA",
                              new KnobWidget(slot ? "Pan B" : "Pan A", -100.0, 100.0, 0.0, "%"),
                              [this, slot](double v) {
                                  engine->get_ir().set_pan(slot, static_cast<float>(v));
                              });
    k->set_scroll_step(10.0);
    k->set_value_formatter(&format_pan);
    return static_cast<QWidget*>(k);
}

// Cabinet slot polarity switch with its "Ø A/B" caption.
QWidget* MainWindow::make_ir_phase_switch(int slot) {
    auto* sw = new ToggleSwitch;
    sw->setCheckable(true);
    sw->setToolTip("Invert this slot's polarity (fix phase cancellation between two mics)");
    late_params.push_back([this, sw, slot] {
        toggle_param("ir", slot ? "invertB" : "invertA", sw, [this, slot](bool b) {
            engine->get_ir().set_phase(slot, b);
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
}

QToolButton* MainWindow::make_ir_b_arrow(const QString& glyph, int step) {
    auto* b = new QToolButton;
    b->setObjectName("heroArrow");
    b->setText(glyph);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(step < 0 ? "Previous IR in the library (slot B)"
                           : "Next IR in the library (slot B)");
    connect(b, &QToolButton::clicked, this, [this, step] {
        step_ir_b(step);
    });
    return b;
}

void MainWindow::step_ir_b(int step) {
    const QString next = library->neighbour(LibraryEntry::Kind::IR,
                                            QString::fromStdString(engine->ir_path_b()), step);
    if (!next.isEmpty()) {
        load_irb_file(next);
    }
}

void MainWindow::choose_ir_b() {
    QString start = QString::fromStdString(engine->ir_path_b());
    if (start.isEmpty()) {
        start = QString::fromStdString(engine->ir_path());
    }
    const QString p = QFileDialog::getOpenFileName(nullptr, "Impulse Response for Slot B", start,
                                                   "WAV Files (*.wav);;All Files (*)");
    if (!p.isEmpty()) {
        load_irb_file(p);
    }
}

void MainWindow::clear_ir_b() {
    engine->unload_irb();
    QSettings().remove("files/lastIRB");
    update_hero_cards();
}

// Input/Output column: title, big gain knob, full-height meter.
QWidget* MainWindow::make_gain_column(const QString& title, KnobWidget*& knob, VUMeter*& meter) {
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
    knob =
        new KnobWidget("Gain", GAIN_MIN, GAIN_MAX, GAIN_DEFAULT, "dB", KnobWidget::Form::STACKED);
    knob->set_scroll_step(0.1);
    meter = new VUMeter(Qt::Vertical);

    lay->addWidget(name);
    lay->addWidget(knob, 0, Qt::AlignHCenter);
    lay->addWidget(meter, 1, Qt::AlignHCenter);
    Theme::tag_card(col, "neutral"); // after children — pushes accents to them
    return col;
}

void MainWindow::find_in_library() {
    library_dock->show();
    library_tabs->set_tab(LibraryDock::LOCAL);
    library->focus_search();
}

void MainWindow::browse_tone3000() {
    library_dock->show();
    library_tabs->set_tab(LibraryDock::TONE3000);
    if (tone3000->is_signed_in()) {
        t3k_panel->browse_catalog();
    }
}

void MainWindow::make_hero_controls(QWidget* card, LibraryEntry::Kind kind, QLabel*& name_out,
                                    QLabel*& meta_out, QLabel*& origin_out) {
    QHBoxLayout* lay = row_layout(card);
    const bool model = kind == LibraryEntry::Kind::MODEL;

    // ‹ › cycle the library section in its current visible order.
    QToolButton* prev = make_library_arrow(
        "‹", -1, model ? "Previous model in the library" : "Previous IR in the library", kind);
    QToolButton* next = make_library_arrow(
        "›", +1, model ? "Next model in the library" : "Next IR in the library", kind);

    // TONE3000 origin marker (the compact T3K mark), shown only when the
    // loaded file carries a download sidecar.
    origin_out = new QLabel;
    origin_out->setObjectName("heroOrigin");
    origin_out->setPixmap(
        QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(13, Qt::SmoothTransformation));
    origin_out->setCursor(Qt::PointingHandCursor);
    origin_out->hide();

    // The file name opens the Library on this row's section: the library is where
    // models and IRs are browsed (and imported).
    name_out = new QLabel(model ? "No model loaded" : "No impulse response loaded");
    name_out->setObjectName("fileStatus");
    name_out->setCursor(Qt::PointingHandCursor);
    name_out->setToolTip(model ? "Show the models in the Library"
                               : "Show the impulse responses in the Library");
    name_out->setProperty("heroKind", static_cast<int>(kind));
    name_out->installEventFilter(this);
    meta_out = new QLabel;
    meta_out->setObjectName("fileMeta");

    // ‹ › in the Sync column; the file name starts at the knob column.
    put_in_sync_column(card, prev);
    put_in_sync_column(card, next);
    int i = lay->indexOf(card->findChild<QWidget*>("syncSlot")) + 1;
    lay->insertWidget(i++, origin_out, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, name_out, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, meta_out, 1, Qt::AlignVCenter);
}

// ‹ › on a hero row: step through the library section in its visible order.
QToolButton* MainWindow::make_library_arrow(const QString& glyph, int step, const QString& tip,
                                            LibraryEntry::Kind kind) {
    auto* b = new QToolButton;
    b->setObjectName("heroArrow");
    b->setText(glyph);
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    b->setToolTip(tip);
    connect(b, &QToolButton::clicked, this, [this, kind, step] {
        step_library(kind, step);
    });
    const QString action_id = library_step_action(kind, step);
    library_arrows.insert(action_id, b);
    b->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(b, &QWidget::customContextMenuRequested, this, [this, b, action_id](const QPoint& p) {
        QMenu menu(this);
        add_learn_entries(action_id, &menu);
        if (!menu.isEmpty()) {
            menu.exec(b->mapToGlobal(p));
        }
    });
    return b;
}

QString MainWindow::library_step_action(LibraryEntry::Kind kind, int step) {
    const char* what = kind == LibraryEntry::Kind::MODEL ? "model" : "ir";
    return QString("action:%1%2").arg(what, step > 0 ? "Next" : "Prev");
}

void MainWindow::show_library_section(LibraryEntry::Kind kind) {
    library_dock->show();
    library_tabs->set_tab(LibraryDock::LOCAL);
    library->show_section(kind);
}

void MainWindow::layout_strip(const vector<EffectBase*>& order) {
    // Empty the layout: the stretch is recreated below, cards are reused
    // (they stay children of the strip while out of the layout).
    while (QLayoutItem* item = strip_lay->takeAt(0)) {
        delete item;
    }

    for (auto* fx : order) {
        auto it = fx_cols.find(fx);
        if (it != fx_cols.end()) {
            strip_lay->addWidget(it->second);
        }
    }
    strip_lay->addStretch();
    strip_lay->activate(); // fresh geometry now — the drag preview reads card centres
}

void MainWindow::rebuild_effect_strip() {
    layout_strip(engine->get_chain());

    // Effect keys (1…0, Q…P) always follow the rows top to bottom.
    shortcuts.clear();
    for (auto* fx : engine->get_chain()) {
        auto tg = fx_toggles.find(fx);
        if (tg != fx_toggles.end()) {
            shortcuts.add_effect(tg->second);
        }
    }
}

void MainWindow::update_card_bypass_state(EffectBase* effect, bool enabled) {
    const auto it = fx_cols.find(effect);
    if (it == fx_cols.end()) {
        return;
    }

    QWidget* card = it->second;
    if (enabled) {
        card->setGraphicsEffect(nullptr);
        return;
    }
    auto* opacity = new QGraphicsOpacityEffect(card);
    opacity->setOpacity(0.62); // dimmer reads as off; lower and dark accents vanish
    card->setGraphicsEffect(opacity);
}

void MainWindow::apply_chain_order(const QStringList& ids) {
    engine->set_chain_order(to_std_strings(ids));
    rebuild_effect_strip();
}

void MainWindow::start_card_drag(QWidget* card) {
    EffectBase* fx = nullptr;
    for (const auto& [e, w] : fx_cols) {
        if (w == card) {
            fx = e;
            break;
        }
    }
    if (!fx) {
        return;
    }
    const string id = engine->id_for_effect(fx);
    if (id.empty()) {
        return;
    }

    drag_id = id;
    preview_ids = engine->chain_ids();

    auto* mime = new QMimeData;
    mime->setData(EFFECT_MIME, QByteArray::fromStdString(id));
    auto* drag = new QDrag(card);
    drag->setMimeData(mime);
    drag->setPixmap(card->grab()); // grab before dimming
    drag->setHotSpot(drag_start_pos);

    // Dim the in-strip card: it marks its landing slot in the live preview
    // while the full-opacity pixmap follows the cursor.
    auto* dim = new QGraphicsOpacityEffect(card);
    dim->setOpacity(0.35);
    card->setGraphicsEffect(dim);

    drag->exec(Qt::MoveAction);

    // Drop committed inside the event filter; a cancelled drag just falls
    // back to the engine order here.
    auto toggles = fx_toggles.find(fx);
    update_card_bypass_state(fx, toggles != fx_toggles.end() && toggles->second->isChecked());
    drag_id.clear();
    rebuild_effect_strip();
}

void MainWindow::update_drag_preview(const QPoint& pos) {
    if (drag_id.empty()) {
        return;
    }

    vector<string> ids = preview_ids;
    ids.erase(std::remove(ids.begin(), ids.end(), drag_id), ids.end());
    if (ids.empty()) {
        return;
    }

    // Insert after every card whose centre lies above the cursor.
    int insert = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        auto it = fx_cols.find(engine->effect_by_id(ids[i]));
        if (it != fx_cols.end() && pos.y() > it->second->geometry().center().y()) {
            insert = static_cast<int>(i) + 1;
        }
    }
    ids.insert(ids.begin() + insert, drag_id);

    if (ids == preview_ids) {
        return;
    }
    preview_ids = ids;

    vector<EffectBase*> order;
    order.reserve(ids.size());
    for (const auto& s : ids) {
        if (EffectBase* e = engine->effect_by_id(s)) {
            order.push_back(e);
        }
    }
    layout_strip(order);
}

void MainWindow::commit_drag_preview() {
    if (drag_id.empty()) {
        return;
    }
    QStringList qids;
    for (const auto& s : preview_ids) {
        qids << QString::fromStdString(s);
    }
    apply_chain_order(qids);
    schedule_session_save();
}

void MainWindow::cancel_drag_preview() {
    if (drag_id.empty()) {
        return;
    }
    preview_ids = engine->chain_ids();
    rebuild_effect_strip();
}

void MainWindow::reset_card_to_defaults(QWidget* card) {
    // The enable toggle keeps its state — resetting restores the effect's
    // sound, it doesn't kick it in or out of the chain.
    ToggleSwitch* enable = nullptr;
    for (const auto& [fx, w] : fx_cols) {
        if (w == card) {
            auto tg = fx_toggles.find(fx);
            if (tg != fx_toggles.end()) {
                enable = tg->second;
            }
            break;
        }
    }

    // A parameter belongs to this card iff its widget lives inside it — the
    // registry needs no extra per-effect bookkeeping. Widget signals update
    // the engine and schedule the session save.
    for (const auto& p : params) {
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
            auto it = card ? card_grips.find(card) : card_grips.end();
            if (it != card_grips.end() && me->button() == Qt::LeftButton &&
                it->second->geometry().contains(me->pos())) {
                drag_card = card;
                drag_start_pos = me->pos();
            }
            break;
        }
        case QEvent::MouseMove: {
            if (!drag_card || obj != drag_card) {
                break;
            }
            auto* me = static_cast<QMouseEvent*>(ev);
            if (!(me->buttons() & Qt::LeftButton)) {
                drag_card = nullptr;
                break;
            }
            if ((me->pos() - drag_start_pos).manhattanLength() <
                QApplication::startDragDistance()) {
                break;
            }
            QWidget* card = drag_card;
            drag_card = nullptr;
            start_card_drag(card);
            return true;
        }
        case QEvent::MouseButtonRelease:
            if (obj == drag_card) {
                drag_card = nullptr;
            }
            if (const QVariant kind = obj->property("heroKind"); kind.isValid()) {
                show_library_section(static_cast<LibraryEntry::Kind>(kind.toInt()));
                return true;
            }
            if (obj == lbl_nam_origin || obj == lbl_ir_origin) {
                // T3K mark on a hero row: open that tone in the TONE3000 tab.
                const int tone_id = static_cast<QWidget*>(obj)->property("toneId").toInt();
                if (tone_id > 0) {
                    library_dock->show();
                    library_tabs->set_tab(LibraryDock::TONE3000);
                    t3k_panel->show_tone(tone_id);
                }
                return true;
            }
            break;
        case QEvent::ContextMenu: {
            // Right-click on a card (knobs keep their own menu): per-effect reset.
            auto* card = qobject_cast<QWidget*>(obj);
            if (card && obj != strip && card->property("fxCard").toBool()) {
                auto* ce = static_cast<QContextMenuEvent*>(ev);
                const QString name = card->property("displayName").toString();
                QMenu menu(card);

                // Hero rows: Import (file dialog) and Unload live here — the
                // library is the primary path, so they left the row itself.
                const bool is_amp = card == fx_cols[&engine->get_nam()];
                const bool is_cab = card == fx_cols[&engine->get_ir()];
                if (is_amp || is_cab) {
                    connect(menu.addAction(is_amp ? "Import Model…" : "Import Impulse Response…"),
                            &QAction::triggered, this,
                            is_amp ? &MainWindow::on_import_nam : &MainWindow::on_import_ir);
                    QAction* unload =
                        menu.addAction(is_amp ? "Unload Model" : "Unload Impulse Response");
                    unload->setEnabled(is_amp ? engine->is_model_loaded() : engine->is_ir_loaded());
                    connect(unload, &QAction::triggered, this,
                            is_amp ? &MainWindow::on_unload_nam : &MainWindow::on_unload_ir);
                    menu.addSeparator();
                }

                QAction* act = menu.addAction("Reset " + name + " to defaults");

                // A right-click on a switch: MIDI Learn for that switch.
                for (QWidget* w = card->childAt(card->mapFromGlobal(ce->globalPos()));
                     w && w != card; w = w->parentWidget()) {
                    if (const MidiTarget* t = midi_mapper->target_for(w); t && t->toggle) {
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
            if (obj == strip) {
                auto* de = static_cast<QDragEnterEvent*>(ev);
                if (de->mimeData()->hasFormat(EFFECT_MIME) || has_loadable_urls(de->mimeData())) {
                    de->acceptProposedAction(); // files: the strip is the drop
                    return true; // target Qt found, so accept here
                }
            }
            break;
        case QEvent::DragMove:
            if (obj == strip) {
                auto* de = static_cast<QDragMoveEvent*>(ev);
                if (de->mimeData()->hasFormat(EFFECT_MIME)) {
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
            if (obj == strip) {
                cancel_drag_preview();
            }
            break;
        case QEvent::Drop:
            if (obj == strip) {
                auto* de = static_cast<QDropEvent*>(ev);
                if (de->mimeData()->hasFormat(EFFECT_MIME)) {
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
        started = engine->start_with_devices(saved_in.toStdString(), saved_out.toStdString());
    }
    if (!started) {
        started = engine->start("NAMPlayer");
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

void MainWindow::restart_audio_with_current_devices() {
    const QString in_id = audio_bar->input_device_id();
    const QString out_id = audio_bar->output_device_id();

    audio_ok = engine->start_with_devices(in_id.toStdString(), out_id.toStdString());
    if (audio_ok) {
        audio_bar->refresh();
        if (engine->supports_port_routing()) {
            restore_connections();
        }
    } else {
        audio_bar->set_error("Not connected");
    }
}

void MainWindow::on_import_nam() {
    const QString path = QFileDialog::getOpenFileName(nullptr, "Import NAM Model",
                                                      QSettings().value("files/lastNAM").toString(),
                                                      "NAM Files (*.nam);;All Files (*)");
    if (!path.isEmpty()) {
        load_model_file(path);
    }
}

void MainWindow::on_unload_nam() {
    engine->unload_model();
    QSettings().remove("files/lastNAM");
    library->set_loaded_path(LibraryEntry::Kind::MODEL, {});
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
    engine->unload_ir();
    QSettings().remove("files/lastIR");
    library->set_loaded_path(LibraryEntry::Kind::IR, {});
    update_hero_cards();
}

bool MainWindow::load_model_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    // Same file again (preset reload, arrow wrap-around on a one-item list):
    // skip the synchronous rebuild + prewarm and the audible state swap.
    if (!(engine->is_model_loaded() && QString::fromStdString(engine->model_path()) == clean)) {
        if (!engine->load_model(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Model",
                                      "Failed to load NAM model:\n" + clean + "\n\n" +
                                          QString::fromStdString(engine->model_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastNAM", clean);
    library->note_used(clean);
    library->set_loaded_path(LibraryEntry::Kind::MODEL, clean);
    update_hero_cards();
    return true;
}

bool MainWindow::load_ir_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    if (!(engine->is_ir_loaded() && QString::fromStdString(engine->ir_path()) == clean)) {
        if (!engine->load_ir(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Impulse Response",
                                      "Failed to load IR:\n" + clean + "\n\n" +
                                          QString::fromStdString(engine->ir_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastIR", clean);
    library->note_used(clean);
    library->set_loaded_path(LibraryEntry::Kind::IR, clean);
    update_hero_cards();
    return true;
}

bool MainWindow::load_irb_file(const QString& path, bool quiet) {
    const QString clean = QDir::cleanPath(path);
    if (!(engine->is_irb_loaded() && QString::fromStdString(engine->ir_path_b()) == clean)) {
        if (!engine->load_irb(clean.toStdString())) {
            if (!quiet) {
                QMessageBox::critical(this, "Impulse Response",
                                      "Failed to load IR (slot B):\n" + clean + "\n\n" +
                                          QString::fromStdString(engine->ir_b_load_error()));
            }
            return false;
        }
    }
    QSettings().setValue("files/lastIRB", clean);
    library->note_used(clean);
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
                                ? QString::fromStdString(engine->model_path())
                                : QString::fromStdString(engine->ir_path());
    const QString next = library->neighbour(kind, current, step);
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

void MainWindow::knob_param(const QString& group, const QString& key, KnobWidget* knob,
                            const std::function<void(double)>& apply) {
    connect(knob, &KnobWidget::value_changed, this, apply);
    connect(knob, &KnobWidget::value_changed, this, &MainWindow::schedule_session_save);
    params.push_back({group, key, knob, nullptr, knob->get_default_value(), false});
    const QString card = target_label_for(knob, group, key);
    midi_mapper->add_knob("param:" + (group.isEmpty() ? key : group + "/" + key), card,
                          key == "syncNote" ? knob->get_label() + " (note)" : knob->get_label(),
                          knob);
}

void MainWindow::toggle_param(const QString& group, const QString& key, ToggleSwitch* sw,
                              const std::function<void(bool)>& apply) {
    connect(sw, &ToggleSwitch::toggled, this, apply);
    connect(sw, &ToggleSwitch::toggled, this, &MainWindow::schedule_session_save);
    params.push_back({group, key, nullptr, sw, 0.0, sw->isChecked()});
    const QString label = key == "stereo" ? "Stereo" : key == "sync" ? "Sync" : "On / Off";
    midi_mapper->add_toggle("param:" + (group.isEmpty() ? key : group + "/" + key),
                            target_label_for(sw, group, key), label, sw);
}

QString MainWindow::target_label_for(QWidget* w, const QString& group, const QString& key) const {
    // The card's display name ("Delay", "Input") groups the target.
    for (QWidget* p = w; p; p = p->parentWidget()) {
        if (p->property("displayName").isValid()) {
            return p->property("displayName").toString();
        }
    }
    return group.isEmpty() ? key : group;
}

void MainWindow::setup_midi(QMenu* top_menu) {
    // Actions
    for (int i = 0; i < PresetBar::SLOT_COUNT; ++i) {
        midi_mapper->add_action(QString("action:slot%1").arg(i + 1), "Presets",
                                QString("Slot %1").arg(i + 1), [this, i] {
                                    preset_bar->recall_slot(i);
                                });
    }
    midi_mapper->add_action("action:presetNext", "Presets", "Next preset", [this] {
        step_preset(+1);
    });
    midi_mapper->add_action("action:presetPrev", "Presets", "Previous preset", [this] {
        step_preset(-1);
    });
    for (const auto kind : {LibraryEntry::Kind::MODEL, LibraryEntry::Kind::IR}) {
        const bool model = kind == LibraryEntry::Kind::MODEL;
        const QString group = model ? "Amplifier" : "Cabinet";
        const QString what = model ? "model" : "IR";
        for (const int step : {+1, -1}) {
            midi_mapper->add_action(library_step_action(kind, step), group,
                                    QString(step > 0 ? "Next %1" : "Previous %1").arg(what),
                                    [this, kind, step] {
                                        step_library(kind, step);
                                    });
        }
    }
    midi_mapper->add_action("action:abToggle", "Presets", "Switch A / B", [this] {
        switch_ab(1 - ab_active);
    });
    midi_mapper->add_action("action:metronome", "Metronome", "Start / stop", [this] {
        tempo_bar->toggle_play();
    });
    midi_mapper->add_action("action:tap", "Tempo", "Tap", [this] {
        tempo_bar->tap_tempo();
    });

    // The cards' Reverse switches flip their pedal knob's sweep.
    midi_mapper->set_reverse_switch("param:dynamicWah/position", sw_wah_reverse);
    midi_mapper->set_reverse_switch("param:transpose/semitones", sw_transpose_reverse);
    midi_mapper->set_reverse_switch("param:volume/position", sw_volume_reverse);

    // Learn entries in every context menu
    KnobWidget::set_menu_hook([this](KnobWidget* k, QMenu* menu) {
        if (const MidiTarget* t = midi_mapper->target_for(k)) {
            menu->addSeparator();
            add_learn_entries(t->id, menu);
        }
    });
    auto hook = [this](const QString& id, QMenu* menu) {
        add_learn_entries(id, menu);
    };
    preset_bar->set_learn_menu_hook(hook);
    tempo_bar->set_learn_menu_hook(hook);

    learn_esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    learn_esc->setEnabled(false);
    learn_esc->setWhatsThis("Cancel MIDI learn");
    connect(learn_esc, &QShortcut::activated, midi_mapper, &MidiMapper::cancel_learn);
    connect(midi_mapper, &MidiMapper::learn_changed, this, &MainWindow::on_learn_changed);

    // Input
    midi = MidiInput::create(this);
    connect(midi, &MidiInput::message_received, midi_mapper, &MidiMapper::handle);
    midi_ok = midi->start();
    connect(midi_mapper, &MidiMapper::tempo_from_clock, tempo_bar, &TempoBar::set_bpm);

    // Status bar: a light that blinks on incoming MIDI.
    lbl_midi = new QLabel;
    lbl_midi->setObjectName("statusItem");
    lbl_midi->setTextFormat(Qt::RichText);
    statusBar()->addWidget(lbl_midi);
    midi_blink = new QTimer(this);
    midi_blink->setSingleShot(true);
    midi_blink->setInterval(120);
    connect(midi_blink, &QTimer::timeout, this, [this] {
        show_midi_light(false);
    });
    connect(midi_mapper, &MidiMapper::activity, this, &MainWindow::on_midi_activity);
    show_midi_light(false);
    lbl_midi->setToolTip(
        midi_ok ? QString("MIDI input (%1) — MIDI → Mappings…").arg(midi->backend_name())
                : QString("MIDI is not available on this system"));

    // Menu
    connect(top_menu->addAction("Mappings…"), &QAction::triggered, this, [this] {
        open_midi_mappings();
    });
    QMenu* devices = top_menu->addMenu("Devices");
    connect(devices, &QMenu::aboutToShow, this, [this, devices] {
        populate_midi_devices_menu(devices);
    });
    QAction* clock = top_menu->addAction("Follow MIDI Clock");
    clock->setCheckable(true);
    connect(top_menu, &QMenu::aboutToShow, clock, [this, clock] {
        clock->setChecked(midi_mapper->get_follow_clock());
    });
    connect(clock, &QAction::toggled, midi_mapper, &MidiMapper::set_follow_clock);
}

void MainWindow::open_settings(SettingsDialog::Page page) {
    if (!settings_dialog) {
        settings_dialog = new SettingsDialog(
            {
                .engine = *engine,
                .status_bar = *audio_bar,
                .midi = midi_ok ? midi : nullptr,
                .midi_mapper = *midi_mapper,
                .library_index = *library_index,
                .tuner_reference = *tuner_reference_box,
                .tuner_display = *tuner_display,
                .set_tuner_show_hz =
                    [this](bool on) {
                        set_tuner_show_hz(on);
                    },
                .window = *this,
                .open_midi_mappings =
                    [this] {
                        open_midi_mappings();
                    },
            },
            this);
    }
    settings_dialog->show_page(page);
}

void MainWindow::open_midi_mappings() {
    MidiMappingsDialog dlg(*midi_mapper, midi_ok ? midi : nullptr, this);
    dlg.exec();
}

void MainWindow::populate_midi_devices_menu(QMenu* devices) {
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
}

void MainWindow::add_learn_entries(const QString& target_id, QMenu* menu) {
    const MidiTarget* t = midi_mapper->target(target_id);
    if (!t) {
        return;
    }
    connect(menu->addAction("MIDI Learn"), &QAction::triggered, this, [this, target_id] {
        start_learn(target_id);
    });
    if (midi_mapper->has_mapping(target_id)) {
        connect(menu->addAction("Forget MIDI (" + midi_mapper->mapping_summary(target_id) + ")"),
                &QAction::triggered, this, [this, target_id] {
                    midi_mapper->forget(target_id);
                });
    }
}

QWidget* MainWindow::widget_for_target(const QString& target_id) const {
    if (const MidiTarget* t = midi_mapper->target(target_id)) {
        if (t->knob) {
            return t->knob;
        }
        if (t->toggle) {
            return t->toggle;
        }
    }
    if (QWidget* w = preset_bar->action_widget(target_id)) {
        return w;
    }
    if (QWidget* w = library_arrows.value(target_id)) {
        return w;
    }
    return tempo_bar->action_widget(target_id);
}

void MainWindow::start_learn(const QString& target_id) {
    if (!midi_ok) {
        QMessageBox::information(this, "MIDI Learn", "MIDI input is not available on this system.");
        return;
    }
    midi_mapper->learn(target_id);
}

// Synced: the note-value knob replaces the free one.
static void show_synced_knob(KnobWidget* free_knob, KnobWidget* note, bool on) {
    free_knob->setVisible(!on);
    note->setVisible(on);
}

void MainWindow::on_learn_changed() {
    // Drop the previous highlight.
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
    if (QWidget* w = widget_for_target(target)) {
        auto* glow = new QGraphicsDropShadowEffect(w);
        glow->setColor(Theme::meter_mid());
        glow->setBlurRadius(22);
        glow->setOffset(0, 0);
        w->setGraphicsEffect(glow);
        learn_widget = w;
    }
}

void MainWindow::add_tempo_sync(QWidget* card, KnobWidget* free_knob, const QString& label,
                                bool is_time, int default_note, double min, double max,
                                std::function<void(double)> push) {
    QHBoxLayout* lay = row_layout(card);

    // Note knob in the free knob's place, shown while synced.
    auto* note =
        new KnobWidget(label, 0, static_cast<double>(TempoSync::NOTES.size() - 1), default_note);
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

    // Sync switch in the row's Sync column, right of Stereo.
    auto* sw = new ToggleSwitch;
    sw->setCheckable(true);
    sw->setToolTip(QString("Sync %1 to the global tempo").arg(label));
    auto* cap = new QLabel("Sync");
    cap->setObjectName("toggleCaption");
    QWidget* slot = card->findChild<QWidget*>("syncSlot");
    auto* sl = static_cast<QHBoxLayout*>(slot->layout());
    sl->addWidget(sw, 0, Qt::AlignVCenter);
    sl->addWidget(cap, 0, Qt::AlignVCenter);
    sl->addStretch();

    connect(sw, &ToggleSwitch::toggled, this, [free_knob, note](bool on) {
        show_synced_knob(free_knob, note, on);
    });
    synced.push_back({free_knob, note, sw, is_time, min, max, std::move(push)});
}

MainWindow::SyncedParam& MainWindow::sync_of(KnobWidget* free_knob) {
    for (auto& s : synced) {
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
    const double v = s.is_time ? TempoSync::note_ms(bpm, idx) : TempoSync::note_hz(bpm, idx);
    s.push(std::clamp(v, s.min, s.max));
}

void MainWindow::set_tempo(double new_bpm) {
    bpm = new_bpm;
    for (auto& s : synced) {
        if (s.sync->isChecked()) {
            push_synced(s.free_knob);
        }
    }
}

void MainWindow::schedule_session_save() {
    if (session_save_timer) {
        session_save_timer->start();
    }
    if (preset_state_timer) {
        preset_state_timer->start();
    }
}

void MainWindow::save_session_state() {
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : params) {
        const QString path = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        s.setValue(path, p.knob ? QVariant(p.knob->value()) : QVariant(p.toggle->isChecked()));
    }
    QStringList order;
    for (const auto& id : engine->chain_ids()) {
        order << QString::fromStdString(id);
    }
    s.setValue("effectOrder", order);
    // The current preset and the state it was loaded with, so the name and
    // its `*` come back exactly after a restart.
    s.setValue("presetPath", preset_path);
    s.setValue(
        "presetBaseline",
        preset_path.isEmpty()
            ? QString()
            : QString::fromUtf8(QJsonDocument(preset_baseline).toJson(QJsonDocument::Compact)));
    s.endGroup();
}

void MainWindow::restore_session_state() {
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : params) {
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
    AudioEngine* e = engine.get();
    // Time-based knobs: every change of free value, sync toggle or note value
    // re-pushes the effective value (see pushSynced).
    auto synced_handler = [this](KnobWidget* k) {
        return [this, k](double) {
            push_synced(k);
        };
    };

    knob_param("", "inputGain", knob_input_gain, [e](double v) {
        e->set_input_gain(static_cast<float>(v));
    });
    knob_param("", "outputGain", knob_output_gain, [e](double v) {
        e->set_output_gain(static_cast<float>(v));
    });

    toggle_param("tuner", "enabled", sw_tuner_enable, [e](bool b) {
        e->get_tuner().set_enabled(b);
    });
    params.back().in_preset = false; // a practice tool, not part of the sound

    toggle_param("transpose", "enabled", sw_transpose_enable, [e](bool b) {
        e->get_transpose().set_enabled(b);
    });
    // Registered before Semitones (a late knob), so a preset's fractional value
    // isn't rounded by the discrete snap on its way in.
    toggle_param("transpose", "continuous", sw_transpose_continuous, [this](bool b) {
        set_transpose_continuous(b);
    });
    // Reverse: read by the MIDI mapper (MidiTarget::reverse), nothing for the engine.
    toggle_param("transpose", "reverse", sw_transpose_reverse, [](bool) {});

    toggle_param("noiseGate", "enabled", sw_gate_enable, [e](bool b) {
        e->get_gate().set_enabled(b);
    });
    knob_param("noiseGate", "threshold", knob_gate_thresh, [e](double v) {
        e->get_gate().set_threshold(static_cast<float>(v));
    });
    knob_param("noiseGate", "attack", knob_gate_attack, [e](double v) {
        e->get_gate().set_attack(static_cast<float>(v));
    });
    knob_param("noiseGate", "release", knob_gate_release, [e](double v) {
        e->get_gate().set_release(static_cast<float>(v));
    });

    toggle_param("compressor", "enabled", sw_comp_enable, [e](bool b) {
        e->get_comp().set_enabled(b);
    });
    knob_param("compressor", "threshold", knob_comp_thresh, [e](double v) {
        e->get_comp().set_threshold(static_cast<float>(v));
    });
    knob_param("compressor", "ratio", knob_comp_ratio, [e](double v) {
        e->get_comp().set_ratio(static_cast<float>(v));
    });
    knob_param("compressor", "attack", knob_comp_attack, [e](double v) {
        e->get_comp().set_attack(static_cast<float>(v));
    });
    knob_param("compressor", "release", knob_comp_release, [e](double v) {
        e->get_comp().set_release(static_cast<float>(v));
    });
    knob_param("compressor", "makeup", knob_comp_makeup, [e](double v) {
        e->get_comp().set_makeup(static_cast<float>(v));
    });

    toggle_param("dynamicWah", "enabled", sw_wah_enable, [e](bool b) {
        e->get_wah().set_enabled(b);
    });
    toggle_param("dynamicWah", "mode", sw_wah_manual, [e](bool b) {
        e->get_wah().set_pedal_mode(b);
    });
    // Reverse: read by the MIDI mapper (MidiTarget::reverse), nothing for the engine.
    toggle_param("dynamicWah", "reverse", sw_wah_reverse, [](bool) {});
    knob_param("dynamicWah", "sensitivity", knob_wah_sens, [e](double v) {
        e->get_wah().set_sensitivity_pct(static_cast<float>(v));
    });
    knob_param("dynamicWah", "resonance", knob_wah_res, [e](double v) {
        e->get_wah().set_resonance_pct(static_cast<float>(v));
    });
    knob_param("dynamicWah", "mix", knob_wah_mix, [e](double v) {
        e->get_wah().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("flanger", "enabled", sw_flanger_enable, [e](bool b) {
        e->get_flanger().set_enabled(b);
    });
    toggle_param("flanger", "stereo", sw_flanger_stereo, [e](bool b) {
        e->get_flanger().set_stereo_mode(b);
    });
    knob_param("flanger", "rate", knob_fl_rate, synced_handler(knob_fl_rate));
    toggle_param("flanger", "sync", sync_of(knob_fl_rate).sync, synced_handler(knob_fl_rate));
    knob_param("flanger", "syncNote", sync_of(knob_fl_rate).note_knob,
               synced_handler(knob_fl_rate));
    knob_param("flanger", "depth", knob_fl_depth, [e](double v) {
        e->get_flanger().set_depth_pct(static_cast<float>(v));
    });
    knob_param("flanger", "feedback", knob_fl_feedback, [e](double v) {
        e->get_flanger().set_feedback_pct(static_cast<float>(v));
    });
    knob_param("flanger", "mix", knob_fl_mix, [e](double v) {
        e->get_flanger().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("phaser", "enabled", sw_phaser_enable, [e](bool b) {
        e->get_phaser().set_enabled(b);
    });
    toggle_param("phaser", "stereo", sw_phaser_stereo, [e](bool b) {
        e->get_phaser().set_stereo_mode(b);
    });
    toggle_param("phaser", "mode", sw_phaser_vibe, [e](bool b) {
        e->get_phaser().set_vibe(b);
    });
    knob_param("phaser", "rate", knob_ph_rate, synced_handler(knob_ph_rate));
    toggle_param("phaser", "sync", sync_of(knob_ph_rate).sync, synced_handler(knob_ph_rate));
    knob_param("phaser", "syncNote", sync_of(knob_ph_rate).note_knob, synced_handler(knob_ph_rate));
    knob_param("phaser", "depth", knob_ph_depth, [e](double v) {
        e->get_phaser().set_depth_pct(static_cast<float>(v));
    });
    knob_param("phaser", "feedback", knob_ph_feedback, [e](double v) {
        e->get_phaser().set_feedback_pct(static_cast<float>(v));
    });
    knob_param("phaser", "mix", knob_ph_mix, [e](double v) {
        e->get_phaser().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("drive", "enabled", sw_drive_enable, [e](bool b) {
        e->get_drive().set_enabled(b);
    });
    knob_param("drive", "drive", knob_drive, [e](double v) {
        e->get_drive().set_drive_pct(static_cast<float>(v));
    });
    knob_param("drive", "tone", knob_drive_tone, [e](double v) {
        e->get_drive().set_tone_pct(static_cast<float>(v));
    });
    knob_param("drive", "level", knob_drive_level, [e](double v) {
        e->get_drive().set_level(static_cast<float>(v));
    });

    toggle_param("", "namEnabled", sw_nam_enable, [e](bool b) {
        e->get_nam().set_enabled(b);
    });
    toggle_param("", "namStereo", sw_nam_stereo, [e](bool b) {
        e->get_nam().set_stereo_amp(b);
    });

    toggle_param("eq", "enabled", sw_eq_enable, [e](bool b) {
        e->get_eq().set_enabled(b);
    });
    knob_param("eq", "low", knob_low, [e](double v) {
        e->get_eq().set_low_gain(v);
    });
    knob_param("eq", "mid", knob_mid, [e](double v) {
        e->get_eq().set_mid_gain(v);
    });
    knob_param("eq", "treble", knob_treble, [e](double v) {
        e->get_eq().set_treble_gain(v);
    });

    toggle_param("chorus", "enabled", sw_chorus_enable, [e](bool b) {
        e->get_chorus().set_enabled(b);
    });
    toggle_param("chorus", "stereo", sw_chorus_stereo, [e](bool b) {
        e->get_chorus().set_stereo_mode(b);
    });
    knob_param("chorus", "rate", knob_ch_rate, synced_handler(knob_ch_rate));
    toggle_param("chorus", "sync", sync_of(knob_ch_rate).sync, synced_handler(knob_ch_rate));
    knob_param("chorus", "syncNote", sync_of(knob_ch_rate).note_knob, synced_handler(knob_ch_rate));
    knob_param("chorus", "depth", knob_ch_depth, [e](double v) {
        e->get_chorus().set_depth_pct(static_cast<float>(v));
    });
    knob_param("chorus", "mix", knob_ch_mix, [e](double v) {
        e->get_chorus().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("tremolo", "enabled", sw_tremolo_enable, [e](bool b) {
        e->get_tremolo().set_enabled(b);
    });
    toggle_param("tremolo", "stereo", sw_tremolo_stereo, [e](bool b) {
        e->get_tremolo().set_stereo_mode(b);
    });
    knob_param("tremolo", "rate", knob_tr_rate, synced_handler(knob_tr_rate));
    toggle_param("tremolo", "sync", sync_of(knob_tr_rate).sync, synced_handler(knob_tr_rate));
    knob_param("tremolo", "syncNote", sync_of(knob_tr_rate).note_knob,
               synced_handler(knob_tr_rate));
    knob_param("tremolo", "depth", knob_tr_depth, [e](double v) {
        e->get_tremolo().set_depth_pct(static_cast<float>(v));
    });

    toggle_param("delay", "enabled", sw_delay_enable, [e](bool b) {
        e->get_delay().set_enabled(b);
    });
    toggle_param("delay", "stereo", sw_delay_stereo, [e](bool b) {
        e->get_delay().set_stereo_mode(b);
    });
    knob_param("delay", "time", knob_del_time, synced_handler(knob_del_time));
    toggle_param("delay", "sync", sync_of(knob_del_time).sync, synced_handler(knob_del_time));
    knob_param("delay", "syncNote", sync_of(knob_del_time).note_knob,
               synced_handler(knob_del_time));
    knob_param("delay", "feedback", knob_del_feedback, [e](double v) {
        e->get_delay().set_feedback_pct(static_cast<float>(v));
    });
    knob_param("delay", "mix", knob_del_mix, [e](double v) {
        e->get_delay().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("reverb", "enabled", sw_reverb_enable, [e](bool b) {
        e->get_reverb().set_enabled(b);
    });
    knob_param("reverb", "roomSize", knob_rev_room, [e](double v) {
        e->get_reverb().set_room_size_pct(static_cast<float>(v));
    });
    knob_param("reverb", "damping", knob_rev_damping, [e](double v) {
        e->get_reverb().set_damping_pct(static_cast<float>(v));
    });
    knob_param("reverb", "width", knob_rev_width, [e](double v) {
        e->get_reverb().set_width_pct(static_cast<float>(v));
    });
    knob_param("reverb", "mix", knob_rev_mix, [e](double v) {
        e->get_reverb().set_mix_pct(static_cast<float>(v));
    });

    toggle_param("", "irEnabled", sw_ir_enable, [e](bool b) {
        e->get_ir().set_enabled(b);
    });

    toggle_param("volume", "enabled", sw_volume_enable, [e](bool b) {
        e->get_volume().set_enabled(b);
    });
    // Reverse: read by the MIDI mapper (MidiTarget::reverse), nothing for the engine.
    toggle_param("volume", "reverse", sw_volume_reverse, [](bool) {});
    knob_param("volume", "position", knob_vol_pos, [e](double v) {
        e->get_volume().set_position_pct(static_cast<float>(v));
    });
    knob_param("volume", "mid", knob_vol_mid, [e](double v) {
        e->get_volume().set_mid(static_cast<float>(v));
    });

    toggle_param("", "limiter", sw_limiter, [e](bool b) {
        e->get_limiter().set_enabled(b);
    });

    for (auto& reg : late_params) {
        reg();
    }
    late_params.clear();
}

void MainWindow::update_meters() {
    meter_in->set_peak(engine->take_input_peak());
    meter_out->set_peak(engine->take_output_peak());
    gr_gate->set_reduction(engine->get_gate().take_gain_reduction_db());
    gr_comp->set_reduction(engine->get_comp().take_gain_reduction_db());
    tempo_bar->poll();
}

void MainWindow::update_meter_timer() {
    if (!meter_timer) {
        return;
    }
    const bool visible = isVisible() && !isMinimized();
    if (visible && !meter_timer->isActive()) {
        meter_timer->start();
    } else if (!visible) {
        meter_timer->stop();
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
    audio_bar->update_metrics(); // PipeWire can change the quantum while running
    if (audio_ok && !engine->is_running()) {
        audio_ok = false;
        audio_bar->set_error("Disconnected");
    }
}

void MainWindow::update_tuner() {
    const float freq =
        engine->get_tuner().is_enabled() ? engine->get_tuner().get_last_pitch() : 0.0f;
    if (freq <= 0.0f) {
        tuner_display->set_reading("-", 0.0f, false);
        return;
    }

    const float ref = static_cast<float>(tuner_ref_hz);
    const int midi_note = TunerProcessor::freq_to_midi(freq, ref);
    const float cents = TunerProcessor::cents_offset(freq, ref);
    tuner_display->set_reading(QString::fromStdString(TunerProcessor::midi_to_name(midi_note)),
                               cents, true, freq);
}

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

    // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): slot B has no
    // controls while the Cabinet's advanced options are off, so it isn't restored.
    // const QString ir_b = s.value("files/lastIRB").toString();
    // if (!ir_b.isEmpty() && QFile::exists(ir_b)) {
    //     load_irb_file(ir_b, /*quiet=*/true);
    // }

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
        engine->apply_connections(c);
        return;
    }

    AudioEngine::PortConnections c;
    c.input_sources = to_std_strings(s.value("connections/inputSources").toStringList());
    c.output_l_dests = to_std_strings(s.value("connections/outputLDests").toStringList());
    c.output_r_dests = to_std_strings(s.value("connections/outputRDests").toStringList());
    engine->apply_connections(c);
}

static QStringList to_string_list(const vector<string>& v) {
    QStringList l;
    l.reserve(static_cast<int>(v.size()));
    for (const auto& x : v) {
        l << QString::fromStdString(x);
    }
    return l;
}

void MainWindow::save_connections() {
    if (!engine->is_running() || !engine->supports_port_routing()) {
        return;
    }

    IAudioBackend::PortConnections c = engine->current_connections();
    QSettings s;
    s.setValue("connections/inputSources", to_string_list(c.input_sources));
    s.setValue("connections/outputLDests", to_string_list(c.output_l_dests));
    s.setValue("connections/outputRDests", to_string_list(c.output_r_dests));
    s.setValue("connections/saved", true);
}

void MainWindow::update_hero_cards() {
    show_file_status(lbl_nam_status, lbl_nam_meta, lbl_nam_origin, engine->is_model_loaded(),
                     QString::fromStdString(engine->model_path()), "No model loaded");
    show_file_status(lbl_ir_status, lbl_ir_meta, lbl_ir_origin, engine->is_ir_loaded(),
                     QString::fromStdString(engine->ir_path()), "No impulse response loaded");
    if (lbl_irb_status) {
        const bool b = engine->is_irb_loaded();
        const QString p = QString::fromStdString(engine->ir_path_b());
        lbl_irb_status->setText(b ? lbl_irb_status->fontMetrics().elidedText(
                                        QFileInfo(p).completeBaseName(), Qt::ElideMiddle, 300)
                                  : QString("Empty — Choose… or ‹ ›"));
        lbl_irb_status->setToolTip(b ? QDir::toNativeSeparators(p) : QString());
        lbl_irb_status->setObjectName(b ? "fileStatusOk" : "fileStatus");
        lbl_irb_status->style()->unpolish(lbl_irb_status);
        lbl_irb_status->style()->polish(lbl_irb_status);
    }
    // Every model/IR change lands here — it counts toward the preset `*`.
    if (preset_state_timer) {
        preset_state_timer->start();
    }
    update_rig_status();
}

// Fills one hero row (name, metadata, TONE3000 origin) for a loaded file or its empty state.
void MainWindow::show_file_status(QLabel* name, QLabel* meta, QLabel* origin, bool loaded,
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
        const LibraryEntry* e = library_index->find(path);
        const LibraryEntry fresh = e ? *e : LibraryIndex::describe(path);
        name->setText(
            name->fontMetrics().elidedText(fresh.name, Qt::ElideMiddle, HERO_NAME_MAX_WIDTH));
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
}

void MainWindow::update_rig_status() {
    auto item = [](bool on, const QString& text) {
        return QString("<span style=\"color:%1\">●</span> %2")
            .arg((on ? Theme::meter_low() : Theme::track()).name(), text.toHtmlEscaped());
    };
    const bool model = engine->is_model_loaded();
    const bool ir = engine->is_ir_loaded();
    const bool t3k = tone3000->is_signed_in();
    QString t3k_text = "TONE3000";
    if (t3k && tone3000->get_user().is_valid()) {
        t3k_text += " @" + tone3000->get_user().username;
    }

    lbl_rig_status->setText(item(model, "Model") + "&nbsp;&nbsp;&nbsp;" + item(ir, "IR") +
                            "&nbsp;&nbsp;&nbsp;" + item(t3k, t3k_text));
    lbl_rig_status->setToolTip(
        QString("Model: %1\nIR: %2\nTONE3000: %3")
            .arg(model ? QFileInfo(QString::fromStdString(engine->model_path())).completeBaseName()
                       : "none",
                 ir ? QFileInfo(QString::fromStdString(engine->ir_path())).completeBaseName()
                    : "none",
                 t3k ? "signed in" : "signed out"));
}

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
    QJsonObject obj = PresetSerializer::to_json(params, *engine);
    // TONE3000 linkage: lets the preset find its model on another machine.
    QJsonObject t3k;
    if (engine->is_model_loaded()) {
        const QJsonObject m = tone3000_linkage(engine->model_path());
        if (!m.isEmpty()) {
            t3k["model"] = m;
        }
    }
    if (engine->is_ir_loaded()) {
        const QJsonObject m = tone3000_linkage(engine->ir_path());
        if (!m.isEmpty()) {
            t3k["ir"] = m;
        }
    }
    if (!t3k.isEmpty()) {
        obj["tone3000"] = t3k;
    }
    return obj;
}

// TONE3000 linkage of a loaded file, so a preset can find it on another machine.
QJsonObject MainWindow::tone3000_linkage(const string& p) const {
    QJsonObject o;
    if (const LibraryEntry* e = library_index->find(QString::fromStdString(p));
        e && e->from_tone3000()) {
        o["toneId"] = e->t3k_tone_id;
        o["modelId"] = e->t3k_model_id;
        o["title"] = e->t3k_title;
        o["creator"] = e->t3k_creator;
    }
    return o;
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
    if (preset_path.isEmpty() || !QFile::exists(preset_path)) {
        on_save_preset_as();
        return;
    }
    if (write_preset_file(preset_path)) {
        set_current_preset(preset_path);
    }
}

void MainWindow::on_save_preset_as() {
    // Default to the library's presets folder so a saved preset shows up in
    // the list right away (the folder watcher picks it up).
    QString start = preset_path;
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
    library->note_used(path);
    library->set_loaded_path(LibraryEntry::Kind::PRESET, path);
    set_current_preset(path);
}

void MainWindow::on_export_preset() {
    const QString name =
        preset_path.isEmpty() ? QString("Untitled.nampreset") : QFileInfo(preset_path).fileName();
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
    if (preset_path.isEmpty() || !QFile::exists(preset_path)) {
        return;
    }
    const QString name = QFileInfo(preset_path).completeBaseName();
    if (QMessageBox::question(this, "Delete Preset",
                              "Move the preset \"" + name +
                                  "\" to the trash?\n"
                                  "The current sound is kept as an untitled state.") !=
        QMessageBox::Yes) {
        return;
    }
    if (!QFile::moveToTrash(preset_path) && !QFile::remove(preset_path)) {
        QMessageBox::critical(this, "Delete Preset", "Cannot delete:\n" + preset_path);
        return;
    }
    library->set_loaded_path(LibraryEntry::Kind::PRESET, {});
    set_current_preset({});
}

void MainWindow::on_locate_preset() {
    const QString dir = preset_path.isEmpty() ? LibraryRoots::default_preset_dir()
                                              : QFileInfo(preset_path).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::set_current_preset(const QString& path) {
    preset_path = path;
    preset_baseline = PresetSerializer::to_json(params, *engine);
    ab[0] = ab[1] = QJsonObject();
    ab_active = 0;
    preset_bar->set_ab_state(0);
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
    const bool modified = !preset_path.isEmpty() &&
                          !same_state(PresetSerializer::to_json(params, *engine), preset_baseline);
    preset_bar->set_preset(preset_path, modified);
    setWindowTitle(preset_path.isEmpty()
                       ? QString("NAM Player")
                       : QString("%1%2 — NAM Player")
                             .arg(QFileInfo(preset_path).completeBaseName(), modified ? "*" : ""));
}

// `base` plus whatever `live` has that `base` lacks (recursively), so keys added
// since a baseline was saved don't count as edits.
static QJsonObject merge_new_keys(QJsonObject base, const QJsonObject& live) {
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
                        merge_new_keys(base.value(it.key()).toObject(), it.value().toObject()));
        }
    }
    return base;
}

void MainWindow::restore_preset_state() {
    QSettings s;
    const QString path = s.value("session/presetPath").toString();
    if (path.isEmpty() || !QFile::exists(path)) {
        update_preset_state();
        return;
    }
    preset_path = path;
    // A baseline saved by an older version lacks parameters added since;
    // those take the live (= restored) values, so an upgrade alone never
    // marks the preset modified.
    preset_baseline = merge_new_keys(
        QJsonDocument::fromJson(s.value("session/presetBaseline").toString().toUtf8()).object(),
        PresetSerializer::to_json(params, *engine));
    library->set_loaded_path(LibraryEntry::Kind::PRESET, path);
    update_preset_state();
}

void MainWindow::step_preset(int step) {
    const QString next = library->neighbour(LibraryEntry::Kind::PRESET, preset_path, step);
    if (!next.isEmpty() && next != preset_path) {
        load_preset_file(next);
    }
}

void MainWindow::switch_ab(int side) {
    if (side == ab_active) {
        return;
    }
    // The side being left keeps the live state; the other side starts as a
    // copy of it the first time it is visited.
    const QJsonObject live = PresetSerializer::to_json(params, *engine);
    ab[ab_active] = live;
    ab_active = side;
    if (ab[side].isEmpty()) {
        ab[side] = live;
    } else {
        const PresetSerializer::Applied applied = apply_preset_params(ab[side]);
        if (!applied.nam_path.isEmpty() && QFile::exists(applied.nam_path)) {
            load_model_file(applied.nam_path, true);
        }
        if (!applied.ir_path.isEmpty() && QFile::exists(applied.ir_path)) {
            load_ir_file(applied.ir_path, true);
        }
    }
    preset_bar->set_ab_state(side);
    schedule_session_save();
}

void MainWindow::copy_ab() {
    ab[1 - ab_active] = PresetSerializer::to_json(params, *engine);
}

void MainWindow::on_reset_all() {
    if (QMessageBox::question(this, "Reset All",
                              "Reset all effects, gains, and the chain order to their defaults?\n"
                              "The loaded model and IR are kept.") != QMessageBox::Yes) {
        return;
    }

    // Registry defaults through the widgets — the engine follows via the
    // registry connections, exactly like a preset load.
    for (const auto& p : params) {
        if (p.knob) {
            p.knob->set_value(p.knob_default);
        } else {
            p.toggle->setChecked(p.toggle_default);
        }
    }

    QStringList order;
    for (const auto& id : engine->default_chain_ids()) {
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
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject()) {
        QMessageBox::critical(
            this, "Preset",
            "Invalid preset:\n" + path + "\n\n" +
                (doc.isNull() ? err.errorString() : QStringLiteral("not a JSON object")));
        return false;
    }
    QSettings settings;
    settings.setValue("files/lastPreset", path);
    library->note_used(path);
    library->set_loaded_path(LibraryEntry::Kind::PRESET, path);

    const PresetSerializer::Applied applied = apply_preset_params(doc.object());

    // v2 presets embed the model/IR paths. Only a non-empty path acts — a
    // preset saved without a model must not unload the current one — and when
    // the referenced file is missing or fails to load, the current one stays.
    // A referenced file that isn't there: the TONE3000 linkage can still find
    // it (same model id elsewhere in the library) or fetch it.
    const QJsonObject t3k = doc.object().value("tone3000").toObject();
    // A TONE3000 fetch can land after this returns: the fetched model is
    // part of the preset, not an edit, so it moves the baseline.
    auto rebaseline = [this, path] {
        if (preset_path == path) {
            set_current_preset(path);
        }
    };

    QStringList failed;
    if (!applied.nam_path.isEmpty() &&
        !try_load_referenced_file(applied.nam_path, LibraryEntry::Kind::MODEL)) {
        const QJsonObject link = t3k.value("model").toObject();
        const int model_id = link.value("modelId").toInt();
        if (model_id > 0) {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.nam_path).completeBaseName()
                                      : link.value("title").toString();
            resolve_tone3000_model(model_id, title, [this, rebaseline](const QString& p) {
                if (!p.isEmpty() && try_load_referenced_file(p, LibraryEntry::Kind::MODEL)) {
                    rebaseline();
                }
            });
        } else {
            failed << applied.nam_path;
        }
    }
    if (!applied.ir_path.isEmpty() &&
        !try_load_referenced_file(applied.ir_path, LibraryEntry::Kind::IR)) {
        const QJsonObject link = t3k.value("ir").toObject();
        const int model_id = link.value("modelId").toInt();
        if (model_id > 0) {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.ir_path).completeBaseName()
                                      : link.value("title").toString();
            resolve_tone3000_model(model_id, title, [this, rebaseline](const QString& p) {
                if (!p.isEmpty() && try_load_referenced_file(p, LibraryEntry::Kind::IR)) {
                    rebaseline();
                }
            });
        } else {
            failed << applied.ir_path;
        }
    }
    // GUIDELINE EXCEPTION (commented-out code, user's call 2026-10-01): slot B has no
    // controls while the Cabinet's advanced options are off, so presets don't load it.
    // // Slot B: presets made before the dual cab don't mention it (left alone);
    // // newer ones say exactly what slot B holds, empty included.
    // if (applied.has_ir_path_b) {
    //     if (applied.ir_path_b.isEmpty()) {
    //         engine->unload_irb();
    //         QSettings().remove("files/lastIRB");
    //     } else if (!(QFile::exists(applied.ir_path_b) && load_irb_file(applied.ir_path_b, true))) {
    //         failed << applied.ir_path_b;
    //     }
    // }

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
    const PresetSerializer::Applied applied = PresetSerializer::apply(obj, params);
    if (!applied.effect_order.isEmpty()) {
        apply_chain_order(applied.effect_order);
    }
    return applied;
}

void MainWindow::resolve_tone3000_model(int model_id, const QString& title,
                                        std::function<void(const QString&)> then) {
    // 1. Already in the library under another path (downloaded on this machine).
    if (const LibraryEntry* e = library_index->find_tone3000_model(model_id)) {
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
    if (!tone3000->is_signed_in()) {
        QMessageBox::information(
            this, "Preset",
            "This preset uses \"" + title +
                "\" from TONE3000, which isn't on this computer.\n\n"
                "Sign in on the Library's TONE3000 tab, then load the preset again to fetch it.");
        library_dock->show();
        library_tabs->set_tab(LibraryDock::TONE3000);
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

    tone3000->fetch_model(model_id, [this, then, title](const Tone3000Client::Result& r) {
        on_t3k_model_fetched(then, title, r);
    });
}

// resolve_tone3000_model, step 1 of 3: model → its tone → download.
void MainWindow::on_t3k_model_fetched(const std::function<void(const QString&)>& then,
                                      const QString& title, const Tone3000Client::Result& r) {
    if (!r.ok) {
        QMessageBox::warning(this, "TONE3000", "Could not look up \"" + title + "\": " + r.error);
        then({});
        return;
    }
    const T3kModel model = Tone3000Client::parse_model(r.object());
    tone3000->fetch_tone(model.tone_id,
                         [this, then, title, model](const Tone3000Client::Result& tr) {
                             on_t3k_tone_fetched(then, title, model, tr);
                         });
}

void MainWindow::on_t3k_tone_fetched(const std::function<void(const QString&)>& then,
                                     const QString& title, const T3kModel& model,
                                     const Tone3000Client::Result& tr) {
    if (!tr.ok) {
        QMessageBox::warning(this, "TONE3000",
                             "Could not look up the tone of \"" + title + "\": " + tr.error);
        then({});
        return;
    }
    const T3kTone tone = Tone3000Client::parse_tone(tr.object());
    tone3000->download_model(model, tone, [this, then, title](bool ok, QString path, QString err) {
        on_t3k_model_downloaded(then, title, ok, path, err);
    });
}

void MainWindow::on_t3k_model_downloaded(const std::function<void(const QString&)>& then,
                                         const QString& title, bool ok, const QString& path,
                                         const QString& err) {
    if (!ok) {
        QMessageBox::warning(this, "TONE3000", "Download of \"" + title + "\" failed: " + err);
        then({});
        return;
    }
    library_index->rescan();
    then(path);
}

// A model/IR path a preset references: loaded if the file is there.
bool MainWindow::try_load_referenced_file(const QString& path, LibraryEntry::Kind kind) {
    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }
    return kind == LibraryEntry::Kind::MODEL ? load_model_file(path, true)
                                             : load_ir_file(path, true);
}

// Status bar MIDI light: lit briefly on activity, grey without MIDI.
void MainWindow::show_midi_light(bool lit) {
    const QColor c = !midi_ok ? Theme::track() : lit ? Theme::meter_mid() : Theme::meter_low();
    lbl_midi->setText(QString("<span style=\"color:%1\">●</span> MIDI").arg(c.name()));
}

void MainWindow::on_midi_activity(const QString& what) {
    show_midi_light(true);
    midi_blink->start();
    lbl_midi->setToolTip("Last MIDI message: " + what);
}

} // namespace npl

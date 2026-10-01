#include "MainWindow.h"
#include "AudioStatusBar.h"
#include "Constants.h"
#include "EffectShortcuts.h"
#include "GainReductionMeter.h"
#include "LibraryDock.h"
#include "LibraryPanel.h"
#include "PresetBar.h"
#include "PresetSerializer.h"
#include "Tone3000Client.h"
#include "Tone3000Panel.h"
#include "Theme.h"
#include "TunerDisplay.h"
#include "TunerProcessor.h"
#include "VUMeter.h"

#include <QAction>
#include <QApplication>
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
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Mime type carrying the effect ID of a dragged strip card.
static const char kEffectMime[] = "application/x-nam-effect-id";

// Fixed columns at the left edge of every row card, so the enable toggles,
// stereo slots, and first knobs line up vertically across the strip.
static constexpr int kGripColumnWidth = 22;  // "≡" drag handle
static constexpr int kNameColumnWidth = 120; // fits "Dynamic Wah"
static constexpr int kEnableSlotWidth = 74;  // toggle + "On"/"Off" caption
static constexpr int kStereoSlotWidth = 96;  // toggle + "Stereo" caption
// Every row is exactly as tall as a knob row (40 px dial + knob margins +
// card margins + border), so knob-less rows and the tuner match the rest.
static constexpr int kRowCardHeight   = 62;
// Loaded file name on the Amplifier/Cabinet rows elides beyond this.
static constexpr int kHeroNameMaxWidth = 420;

// Enable toggle with a live "On"/"Off" caption beside it — one line, so the
// switch stays on the row's vertical centerline.
static QWidget* makeEnableSlot(ToggleSwitch* sw)
{
    auto* w = new QWidget;
    w->setFixedWidth(kEnableSlotWidth);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    auto* lbl = new QLabel(sw->isChecked() ? "On" : "Off");
    lbl->setObjectName("toggleCaption");
    QObject::connect(sw, &ToggleSwitch::toggled, lbl,
                     [lbl](bool on) { lbl->setText(on ? "On" : "Off"); });

    lay->addWidget(sw, 0, Qt::AlignVCenter);
    lay->addWidget(lbl, 0, Qt::AlignVCenter);
    lay->addStretch();
    return w;
}

// The stereo slot is reserved on every effect row (blank when the effect has
// no stereo mode) so the knob column starts at the same X on all rows.
// Toggle and caption sit on one line for the same centering reason.
static QWidget* makeStereoSlot(ToggleSwitch** outToggle)
{
    auto* w = new QWidget;
    w->setFixedWidth(kStereoSlotWidth);
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    if (outToggle) {
        auto* sw = new ToggleSwitch;
        sw->setCheckable(true);
        sw->setChecked(false);
        auto* lbl = new QLabel("Stereo");
        lbl->setObjectName("toggleCaption");
        lay->addWidget(sw, 0, Qt::AlignVCenter);
        lay->addWidget(lbl, 0, Qt::AlignVCenter);
        lay->addStretch();
        *outToggle = sw;
    }

    return w;
}

// Rows without a stereo mode (Gate, Compressor) show their gain-reduction
// meter in the reserved stereo slot, so the knob column stays aligned.
static void placeInStereoSlot(QWidget* card, QWidget* w)
{
    // Card layout: grip, name, enable slot, stereo slot, …
    QWidget* slot = card->layout()->itemAt(3)->widget();
    slot->layout()->addWidget(w);
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player");
    // Laptop-friendly minimum; the effect strip scrolls when it doesn't fit.
    // The default size still shows the full strip — window managers clamp it
    // on smaller screens, and the session restores the user's geometry.
    setMinimumSize(1280, 720);
    resize(1800, 1000);
    setAcceptDrops(true);

    seedStarterPresets(); // before the first scan, so they are in it

    // The index starts its first scan immediately (worker thread); the panel
    // created in setupUI picks the entries up when they land.
    mLibraryIndex = new LibraryIndex(this);
    mTone3000     = new Tone3000Client(this);

    setupUI();
    registerParameters();

    // Debounced session save — every registry widget change restarts it, so
    // the session survives a crash without a write per knob tick. Created
    // before restoreSessionState so the restore's widget signals find it.
    mSessionSaveTimer = new QTimer(this);
    mSessionSaveTimer->setSingleShot(true);
    mSessionSaveTimer->setInterval(1000);
    connect(mSessionSaveTimer, &QTimer::timeout, this, &MainWindow::saveSessionState);
    // The preset `*` follows the same changes, coalesced to one compare per
    // event-loop pass (a preset load moves dozens of widgets).
    mPresetStateTimer = new QTimer(this);
    mPresetStateTimer->setSingleShot(true);
    mPresetStateTimer->setInterval(0);
    connect(mPresetStateTimer, &QTimer::timeout, this, &MainWindow::updatePresetState);

    restoreSessionState();
    {
        QSettings s;
        const QByteArray geo = s.value("window/geometry").toByteArray();
        if (!geo.isEmpty()) restoreGeometry(geo);
        const QByteArray state = s.value("window/state").toByteArray();
        if (!state.isEmpty()) restoreState(state); // dock position + visibility
    }

    setupAudio();
    restoreLastFiles();
    restorePresetState();

    mMeterTimer = new QTimer(this);
    mMeterTimer->setTimerType(Qt::PreciseTimer);
    mMeterTimer->setInterval(16);
    connect(mMeterTimer, &QTimer::timeout, this, &MainWindow::updateMeters);
    updateMeterTimer();

    mStatusTimer = new QTimer(this);
    connect(mStatusTimer, &QTimer::timeout, this, &MainWindow::updateStatus);
    mStatusTimer->start(50);

    mTunerTimer = new QTimer(this);
    connect(mTunerTimer, &QTimer::timeout, this, &MainWindow::updateTuner);
    mTunerTimer->start(100);
}

MainWindow::~MainWindow()
{
    mMeterTimer->stop();
    mStatusTimer->stop();
    mTunerTimer->stop();
    mSessionSaveTimer->stop();
    mPresetStateTimer->stop();
    QSettings().setValue("window/geometry", saveGeometry());
    QSettings().setValue("window/state", saveState());
    saveSessionState();
    saveConnections();
    mEngine->stop();
}

// ---------------------------------------------------------------------------

QFrame* MainWindow::makeRowCard(const QString& title)
{
    auto* card = new QFrame;
    card->setProperty("fxCard", true);        // card chrome lives in the Theme QSS
    card->setProperty("displayName", title);  // context menu + drag bookkeeping
    card->setFixedHeight(kRowCardHeight);
    auto* lay = new QHBoxLayout(card);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(10);

    // Grip column: the ≡ handle is the only place a reorder drag can start,
    // so toggle/knob clicks can never move a card.
    auto* grip = new QLabel("≡");
    grip->setObjectName("fxGrip");
    grip->setFixedWidth(kGripColumnWidth);
    grip->setAlignment(Qt::AlignCenter);
    grip->setCursor(Qt::OpenHandCursor);
    grip->setToolTip("Drag to reorder");
    lay->addWidget(grip, 0, Qt::AlignVCenter);
    mCardGrips[card] = grip;

    auto* name = new QLabel(title);
    name->setObjectName("fxName");
    name->setFixedWidth(kNameColumnWidth);
    lay->addWidget(name, 0, Qt::AlignVCenter);

    return card;
}

QWidget* MainWindow::makeEffectRow(const QString& title,
                                   ToggleSwitch*& toggleOut,
                                   const std::initializer_list<KnobWidget*>& knobs,
                                   ToggleSwitch** stereoToggleOut)
{
    QFrame* card = makeRowCard(title);
    auto* lay = static_cast<QHBoxLayout*>(card->layout());

    toggleOut = new ToggleSwitch;
    toggleOut->setCheckable(true);
    toggleOut->setChecked(false);
    lay->addWidget(makeEnableSlot(toggleOut), 0, Qt::AlignVCenter);

    lay->addWidget(makeStereoSlot(stereoToggleOut), 0, Qt::AlignVCenter);

    for (auto* k : knobs)
        lay->addWidget(k, 0, Qt::AlignVCenter);

    lay->addStretch();
    return card;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void MainWindow::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 4, 8, 6);
    rootLayout->setSpacing(4);

    // ---- Menu bar ----
    QMenu* libraryMenu = nullptr;
    QMenu* presetMenu  = nullptr;
    {
        auto* mb         = menuBar();
        presetMenu = mb->addMenu("Preset"); // also behind ⋯ on the preset bar
        auto* actSave = presetMenu->addAction("Save");
        actSave->setShortcut(QKeySequence::Save); // Ctrl+S
        connect(actSave, &QAction::triggered, this, &MainWindow::onSavePreset);
        auto* actSaveAs = presetMenu->addAction("Save As…");
        actSaveAs->setShortcut(QKeySequence("Ctrl+Shift+S"));
        connect(actSaveAs, &QAction::triggered, this, &MainWindow::onSavePresetAs);
        auto* actLoad = presetMenu->addAction("Open…");
        actLoad->setShortcut(QKeySequence::Open); // Ctrl+O
        connect(actLoad, &QAction::triggered, this, &MainWindow::onLoadPreset);
        presetMenu->addSeparator();
        connect(presetMenu->addAction("Import…"), &QAction::triggered, this, &MainWindow::onImportPresets);
        connect(presetMenu->addAction("Export…"), &QAction::triggered, this, &MainWindow::onExportPreset);
        presetMenu->addSeparator();
        connect(presetMenu->addAction("Show in Folder"), &QAction::triggered, this, &MainWindow::onLocatePreset);
        connect(presetMenu->addAction("Delete…"), &QAction::triggered, this, &MainWindow::onDeletePreset);
        presetMenu->addSeparator();
        connect(presetMenu->addAction("Reset All to Defaults…"), &QAction::triggered, this, &MainWindow::onResetAll);

        libraryMenu = mb->addMenu("Library"); // filled once the dock exists (below)

        auto* helpMenu = mb->addMenu("Help");
        connect(helpMenu->addAction("About NAM Player"), &QAction::triggered, this, [this] {
            QMessageBox::about(this, "About NAM Player",
                QString("<b>NAM Player</b> v%1<br><br>"
                        "Standalone player for Neural Amp Modeler profiles.<br>"
                        "Built on <a href=\"https://github.com/sdatkinson/NeuralAmpModelerCore\">"
                        "NeuralAmpModelerCore</a> by Steven Atkinson.<br><br>"
                        "Not affiliated with the official Neural Amp Modeler plugin.")
                    .arg(QApplication::applicationVersion()));
        });
    }

    // ---- Preset bar (top): name, ‹ ›, Save, quick slots, A/B, Preset menu.
    mPresetBar = new PresetBar(*mLibraryIndex, presetMenu);
    rootLayout->addWidget(mPresetBar);
    connect(mPresetBar, &PresetBar::presetChosen,      this, [this](const QString& p) { loadPresetFile(p); });
    connect(mPresetBar, &PresetBar::stepRequested,     this, &MainWindow::stepPreset);
    connect(mPresetBar, &PresetBar::saveRequested,     this, &MainWindow::onSavePreset);
    connect(mPresetBar, &PresetBar::saveAsRequested,   this, &MainWindow::onSavePresetAs);
    connect(mPresetBar, &PresetBar::abSwitchRequested, this, &MainWindow::switchAb);
    connect(mPresetBar, &PresetBar::abCopyRequested,   this, &MainWindow::copyAb);

    // ---- Status bar (bottom right): backend, rate, buffer, latency, devices.
    mAudioBar = new AudioStatusBar(*mEngine);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(mAudioBar);
    mLblRigStatus = new QLabel;
    mLblRigStatus->setObjectName("statusItem");
    mLblRigStatus->setTextFormat(Qt::RichText);
    statusBar()->addWidget(mLblRigStatus);
    connect(mTone3000, &Tone3000Client::authChanged, this, &MainWindow::updateRigStatus);
    connect(mTone3000, &Tone3000Client::userChanged, this, &MainWindow::updateRigStatus);
    connect(mAudioBar, &AudioStatusBar::deviceSelectionChanged,
            this, &MainWindow::restartAudioWithCurrentDevices);
    connect(mAudioBar, &AudioStatusBar::reconnectRequested,
            this, &MainWindow::restartAudioWithCurrentDevices);

    // ---- Scrollable effect strip ----
    // (NAM model / IR file pickers live inside the Amp and IR rows below.)
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    mStrip    = new QWidget;
    mStripLay = new QVBoxLayout(mStrip);
    mStripLay->setContentsMargins(2, 2, 2, 2);
    mStripLay->setSpacing(4);
    mStripLay->setAlignment(Qt::AlignTop);

    // Effect columns are created once here, keyed by EffectBase*. They are
    // NOT added to the layout — rebuildEffectStrip lays them out in engine
    // chain order, so audio order and UI order stay in sync automatically.

    // ---- Tuner ----
    {
        QFrame* col = makeRowCard("Tuner");
        auto* lay = static_cast<QHBoxLayout*>(col->layout());

        mSwTunerEnable = new ToggleSwitch;
        mSwTunerEnable->setCheckable(true);
        lay->addWidget(makeEnableSlot(mSwTunerEnable), 0, Qt::AlignVCenter);
        lay->addWidget(makeStereoSlot(nullptr), 0, Qt::AlignVCenter);

        mTunerDisplay = new TunerDisplay;
        mTunerDisplay->setMaximumWidth(560); // one compact instrument, not a strip
        lay->addWidget(mTunerDisplay, 1, Qt::AlignVCenter);

        // Reference pitch + Hz readout: machine settings (QSettings tuner/*),
        // not part of presets or the registry.
        QSettings s;
        mTunerRefHz = std::clamp(s.value("tuner/reference", 440).toInt(), 420, 460);
        auto* ref = new QSpinBox;
        ref->setObjectName("tunerRef");
        ref->setRange(420, 460);
        ref->setPrefix("A4 = ");
        ref->setSuffix(" Hz");
        ref->setValue(mTunerRefHz);
        ref->setFocusPolicy(Qt::ClickFocus); // keep digit keys for effect shortcuts
        ref->setToolTip("Reference pitch (concert pitch is 440 Hz)");
        connect(ref, &QSpinBox::valueChanged, this, [this](int hz) {
            mTunerRefHz = hz;
            mTunerDisplay->setReference(hz);
            QSettings().setValue("tuner/reference", hz);
        });
        mTunerDisplay->setReference(mTunerRefHz);

        auto* hz = new QToolButton;
        hz->setObjectName("tunerOption");
        hz->setText("Hz");
        hz->setCheckable(true);
        hz->setFocusPolicy(Qt::NoFocus);
        hz->setToolTip("Show the detected frequency");
        hz->setChecked(s.value("tuner/showHz", false).toBool());
        mTunerDisplay->setShowHz(hz->isChecked());
        connect(hz, &QToolButton::toggled, this, [this](bool on) {
            mTunerDisplay->setShowHz(on);
            QSettings().setValue("tuner/showHz", on);
        });

        ref->setFixedWidth(112);
        lay->addSpacing(8);
        lay->addWidget(ref, 0, Qt::AlignVCenter);
        lay->addWidget(hz, 0, Qt::AlignVCenter);
        lay->addStretch();

        Theme::tagCard(col, "tuner"); // after children — pushes accents to them
        mFxCols[&mEngine->tuner()] = col;
    }

    // ---- Noise Gate ----
    {
        using namespace NAMConstants;
        mKnobGateThresh  = new KnobWidget("Threshold", kGateThreshMin,  kGateThreshMax,  kGateThreshDefault,  "dB");
        mKnobGateAttack  = new KnobWidget("Attack",  kGateAttackMin,  kGateAttackMax,  kGateAttackDefault,  "ms");
        mKnobGateRelease = new KnobWidget("Release", kGateReleaseMin, kGateReleaseMax, kGateReleaseDefault, "ms");
        mKnobGateThresh->setScrollStep(1.0);
        mKnobGateAttack->setScrollStep(1.0);
        mKnobGateRelease->setScrollStep(10.0);

        auto* col = makeEffectRow("Noise Gate", mSwGateEnable,
            {mKnobGateThresh, mKnobGateAttack, mKnobGateRelease});
        mGrGate = new GainReductionMeter(40.0f, Theme::accent("gate"));
        placeInStereoSlot(col, mGrGate);
        Theme::tagCard(col, "gate");

        mFxCols[&mEngine->gate()] = col;
    }

    // ---- Compressor ----
    {
        using namespace NAMConstants;
        mKnobCompThresh  = new KnobWidget("Threshold", kCompThreshMin,  kCompThreshMax,  kCompThreshDefault,  "dB");
        mKnobCompRatio   = new KnobWidget("Ratio",   kCompRatioMin,   kCompRatioMax,   kCompRatioDefault,   ":1");
        mKnobCompAttack  = new KnobWidget("Attack",  kCompAttackMin,  kCompAttackMax,  kCompAttackDefault,  "ms");
        mKnobCompRelease = new KnobWidget("Release", kCompReleaseMin, kCompReleaseMax, kCompReleaseDefault, "ms");
        mKnobCompMakeup  = new KnobWidget("Makeup Gain", kCompMakeupMin,  kCompMakeupMax,  kCompMakeupDefault,  "dB");
        mKnobCompThresh->setScrollStep(1.0);
        mKnobCompRatio->setScrollStep(0.5);
        mKnobCompAttack->setScrollStep(1.0);
        mKnobCompRelease->setScrollStep(10.0);
        mKnobCompMakeup->setScrollStep(0.5);

        auto* col = makeEffectRow("Compressor", mSwCompEnable,
            {mKnobCompThresh, mKnobCompRatio, mKnobCompAttack, mKnobCompRelease, mKnobCompMakeup});
        mGrComp = new GainReductionMeter(20.0f, Theme::accent("comp"));
        placeInStereoSlot(col, mGrComp);
        Theme::tagCard(col, "comp");

        mFxCols[&mEngine->comp()] = col;
    }

    // ---- Dynamic Wah ----
    {
        using namespace NAMConstants;
        mKnobWahSens = new KnobWidget("Sensitivity", kWahSensMin, kWahSensMax, kWahSensDefault, "%");
        mKnobWahRes  = new KnobWidget("Resonance", kWahResMin,  kWahResMax,  kWahResDefault,  "%");
        mKnobWahMix  = new KnobWidget("Mix",  kWahMixMin,  kWahMixMax,  kWahMixDefault,  "%");
        mKnobWahSens->setScrollStep(1.0);
        mKnobWahRes->setScrollStep(1.0);
        mKnobWahMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Dynamic Wah", mSwWahEnable,
            {mKnobWahMix, mKnobWahSens, mKnobWahRes});
        Theme::tagCard(col, "wah");

        mFxCols[&mEngine->wah()] = col;
    }

    // ---- Amp (NAM) ----
    {
        auto* col = makeEffectRow("Amplifier", mSwNAMEnable, {});
        mSwNAMEnable->setChecked(true);
        makeHeroControls(col, LibraryEntry::Kind::Model, mLblNAMStatus, mLblNAMMeta, mLblNAMOrigin);

        Theme::tagCard(col, "nam");
        mFxCols[&mEngine->nam()] = col;
    }

    // ---- EQ ----
    {
        using namespace NAMConstants;
        mKnobLow    = new KnobWidget("Bass",   kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobMid    = new KnobWidget("Middle", kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobTreble = new KnobWidget("Treble", kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobLow->setScrollStep(0.1);
        mKnobMid->setScrollStep(0.1);
        mKnobTreble->setScrollStep(0.1);

        auto* col = makeEffectRow("Equalizer", mSwEQEnable, {mKnobLow, mKnobMid, mKnobTreble});
        mSwEQEnable->setChecked(true);
        Theme::tagCard(col, "eq");
        mFxCols[&mEngine->eq()] = col;
    }

    // ---- Flanger ----
    {
        using namespace NAMConstants;
        mKnobFlRate     = new KnobWidget("Rate",     kFlangerRateMin,     kFlangerRateMax,     kFlangerRateDefault,     "Hz");
        mKnobFlDepth    = new KnobWidget("Depth",    kFlangerDepthMin,    kFlangerDepthMax,    kFlangerDepthDefault,    "%");
        mKnobFlFeedback = new KnobWidget("Feedback", kFlangerFeedbackMin, kFlangerFeedbackMax, kFlangerFeedbackDefault, "%");
        mKnobFlMix      = new KnobWidget("Mix",      kFlangerMixMin,      kFlangerMixMax,      kFlangerMixDefault,      "%");
        mKnobFlRate->setScrollStep(0.1);
        mKnobFlDepth->setScrollStep(1.0);
        mKnobFlFeedback->setScrollStep(1.0);
        mKnobFlMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Flanger", mSwFlangerEnable,
            {mKnobFlMix, mKnobFlRate, mKnobFlDepth, mKnobFlFeedback}, &mSwFlangerStereo);
        Theme::tagCard(col, "flanger");

        mFxCols[&mEngine->flanger()] = col;
    }

    // ---- Phaser ----
    {
        using namespace NAMConstants;
        mKnobPhRate     = new KnobWidget("Rate",     kPhaserRateMin,     kPhaserRateMax,     kPhaserRateDefault,     "Hz");
        mKnobPhDepth    = new KnobWidget("Depth",    kPhaserDepthMin,    kPhaserDepthMax,    kPhaserDepthDefault,    "%");
        mKnobPhFeedback = new KnobWidget("Feedback", kPhaserFeedbackMin, kPhaserFeedbackMax, kPhaserFeedbackDefault, "%");
        mKnobPhMix      = new KnobWidget("Mix",      kPhaserMixMin,      kPhaserMixMax,      kPhaserMixDefault,      "%");
        mKnobPhRate->setScrollStep(0.1);
        mKnobPhDepth->setScrollStep(1.0);
        mKnobPhFeedback->setScrollStep(1.0);
        mKnobPhMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Phaser", mSwPhaserEnable,
            {mKnobPhMix, mKnobPhRate, mKnobPhDepth, mKnobPhFeedback}, &mSwPhaserStereo);
        Theme::tagCard(col, "phaser");

        mFxCols[&mEngine->phaser()] = col;
    }

    // ---- Chorus ----
    {
        using namespace NAMConstants;
        mKnobChRate  = new KnobWidget("Rate",  kChorusRateMin,  kChorusRateMax,  kChorusRateDefault,  "Hz");
        mKnobChDepth = new KnobWidget("Depth", kChorusDepthMin, kChorusDepthMax, kChorusDepthDefault, "%");
        mKnobChMix   = new KnobWidget("Mix",   kChorusMixMin,   kChorusMixMax,   kChorusMixDefault,   "%");
        mKnobChRate->setScrollStep(0.1);
        mKnobChDepth->setScrollStep(1.0);
        mKnobChMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Chorus", mSwChorusEnable,
            {mKnobChMix, mKnobChRate, mKnobChDepth}, &mSwChorusStereo);
        Theme::tagCard(col, "chorus");

        mFxCols[&mEngine->chorus()] = col;
    }

    // ---- Tremolo ----
    {
        using namespace NAMConstants;
        mKnobTrRate  = new KnobWidget("Rate",  kTremoloRateMin,  kTremoloRateMax,  kTremoloRateDefault,  "Hz");
        mKnobTrDepth = new KnobWidget("Depth", kTremoloDepthMin, kTremoloDepthMax, kTremoloDepthDefault, "%");
        mKnobTrRate->setScrollStep(0.1);
        mKnobTrDepth->setScrollStep(1.0);

        auto* col = makeEffectRow("Tremolo", mSwTremoloEnable,
            {mKnobTrRate, mKnobTrDepth}, &mSwTremoloStereo);
        Theme::tagCard(col, "tremolo");

        mFxCols[&mEngine->tremolo()] = col;
    }

    // ---- Delay ----
    {
        using namespace NAMConstants;
        mKnobDelTime     = new KnobWidget("Time",     kDelayTimeMin,     kDelayTimeMax,     kDelayTimeDefault,     "ms");
        mKnobDelFeedback = new KnobWidget("Feedback", kDelayFeedbackMin, kDelayFeedbackMax, kDelayFeedbackDefault, "%");
        mKnobDelMix      = new KnobWidget("Mix",      kDelayMixMin,      kDelayMixMax,      kDelayMixDefault,      "%");
        mKnobDelTime->setScrollStep(1.0);
        mKnobDelFeedback->setScrollStep(1.0);
        mKnobDelMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Delay", mSwDelayEnable,
            {mKnobDelMix, mKnobDelTime, mKnobDelFeedback}, &mSwDelayStereo);
        mSwDelayStereo->setChecked(true);
        Theme::tagCard(col, "delay");

        mFxCols[&mEngine->delay()] = col;
    }

    // ---- Reverb ----
    {
        using namespace NAMConstants;
        mKnobRevRoom    = new KnobWidget("Room Size", kReverbRoomMin,    kReverbRoomMax,    kReverbRoomDefault,    "%");
        mKnobRevDamping = new KnobWidget("Damping", kReverbDampingMin, kReverbDampingMax, kReverbDampingDefault, "%");
        mKnobRevWidth   = new KnobWidget("Width",   kReverbWidthMin,   kReverbWidthMax,   kReverbWidthDefault,   "%");
        mKnobRevMix     = new KnobWidget("Mix",     kReverbMixMin,     kReverbMixMax,     kReverbMixDefault,     "%");
        mKnobRevRoom->setScrollStep(1.0);
        mKnobRevDamping->setScrollStep(1.0);
        mKnobRevWidth->setScrollStep(1.0);
        mKnobRevMix->setScrollStep(1.0);

        auto* col = makeEffectRow("Reverb", mSwReverbEnable,
            {mKnobRevMix, mKnobRevRoom, mKnobRevDamping, mKnobRevWidth});
        Theme::tagCard(col, "reverb");

        mFxCols[&mEngine->reverb()] = col;
    }

    // ---- IR toggle ----
    {
        auto* col = makeEffectRow("Cabinet", mSwIREnable, {});
        mSwIREnable->setChecked(true);
        makeHeroControls(col, LibraryEntry::Kind::IR, mLblIRStatus, mLblIRMeta, mLblIROrigin);

        Theme::tagCard(col, "ir");
        mFxCols[&mEngine->ir()] = col;
    }

    // Input/Output gain columns. Not draggable and not part of the chain, so
    // they flank the strip: input on the left, output on the right — signal
    // flows in → chain → out. The full-height meter reads bottom→top.
    auto makeGainColumn = [&](const QString& title, KnobWidget*& knob, VUMeter*& meter) -> QWidget* {
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
        knob = new KnobWidget("Gain", kGainMin, kGainMax, kGainDefault, "dB",
                              KnobWidget::Form::Stacked);
        knob->setScrollStep(0.1);
        meter = new VUMeter(Qt::Vertical);

        lay->addWidget(name);
        lay->addWidget(knob, 0, Qt::AlignHCenter);
        lay->addWidget(meter, 1, Qt::AlignHCenter);
        Theme::tagCard(col, "neutral"); // after children — pushes accents to them
        return col;
    };

    mInputCard  = makeGainColumn("Input",  mKnobInputGain,  mMeterIn);
    mOutputCard = makeGainColumn("Output", mKnobOutputGain, mMeterOut);

    // Keyboard shortcut targets per card; keys are (re)assigned left to right
    // on every strip rebuild so they always match the visual order.
    mFxToggles = {
        {&mEngine->tuner(),   {mSwTunerEnable,   nullptr}},
        {&mEngine->gate(),    {mSwGateEnable,    nullptr}},
        {&mEngine->comp(),    {mSwCompEnable,    nullptr}},
        {&mEngine->wah(),     {mSwWahEnable,     nullptr}},
        {&mEngine->flanger(), {mSwFlangerEnable, mSwFlangerStereo}},
        {&mEngine->phaser(),  {mSwPhaserEnable,  mSwPhaserStereo}},
        {&mEngine->nam(),     {mSwNAMEnable,     nullptr}},
        {&mEngine->eq(),      {mSwEQEnable,      nullptr}},
        {&mEngine->chorus(),  {mSwChorusEnable,  mSwChorusStereo}},
        {&mEngine->tremolo(), {mSwTremoloEnable, mSwTremoloStereo}},
        {&mEngine->delay(),   {mSwDelayEnable,   mSwDelayStereo}},
        {&mEngine->reverb(),  {mSwReverbEnable,  nullptr}},
        {&mEngine->ir(),      {mSwIREnable,      nullptr}},
    };

    for (const auto& [fx, toggles] : mFxToggles)
    {
        connect(toggles.enable, &ToggleSwitch::toggled, this,
                [this, fx](bool enabled) { updateCardBypassState(fx, enabled); });
        updateCardBypassState(fx, toggles.enable->isChecked());
    }

    // Drag sources + context menus: every card. Drop target: the strip itself.
    for (const auto& [fx, card] : mFxCols)
        card->installEventFilter(this);
    mInputCard->installEventFilter(this);   // context menu only — not draggable
    mOutputCard->installEventFilter(this);
    mLblNAMOrigin->installEventFilter(this); // T3K mark click
    mLblIROrigin->installEventFilter(this);
    mStrip->setAcceptDrops(true);
    mStrip->installEventFilter(this);

    rebuildEffectStrip();

    scroll->setWidget(mStrip);

    // Rack row: input column | effect strip | output column.
    auto* rackRow = new QHBoxLayout;
    rackRow->setSpacing(6);
    rackRow->addWidget(mInputCard);
    rackRow->addWidget(scroll, 1);
    rackRow->addWidget(mOutputCard);
    rootLayout->addLayout(rackRow, 1);

    // ---- Library dock (left of the rig; closable, movable, remembers state) ----
    mLibraryTabs = new LibraryDock(*mLibraryIndex, *mTone3000);
    mLibrary  = mLibraryTabs->local();
    mT3kPanel = mLibraryTabs->tone3000();
    connect(mLibrary,  &LibraryPanel::activated,      this, &MainWindow::loadLibraryEntry);
    connect(mT3kPanel, &Tone3000Panel::loadRequested, this, &MainWindow::loadLibraryEntry);
    connect(mTone3000, &Tone3000Client::authFinished, this, [this](bool ok, const QString&, int) {
        if (ok) { mLibraryDock->show(); mLibraryTabs->setTab(LibraryDock::Tone3000); }
    });

    mLibraryDock = new QDockWidget("Library", this);
    mLibraryDock->setObjectName("libraryDock"); // saveState() needs a name
    mLibraryDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    mLibraryDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    mLibraryDock->setWidget(mLibraryTabs);
    addDockWidget(Qt::LeftDockWidgetArea, mLibraryDock);
    resizeDocks({mLibraryDock}, {320}, Qt::Horizontal);

    {
        QAction* show = mLibraryDock->toggleViewAction();
        show->setText("Show Library");
        show->setShortcut(QKeySequence("Ctrl+L"));
        libraryMenu->addAction(show);

        auto* find = libraryMenu->addAction("Find in Library");
        find->setShortcut(QKeySequence::Find); // Ctrl+F
        connect(find, &QAction::triggered, this, [this] {
            mLibraryDock->show();
            mLibraryTabs->setTab(LibraryDock::Local);
            mLibrary->focusSearch();
        });
        auto* t3k = libraryMenu->addAction("Browse TONE3000…");
        t3k->setIcon(QIcon(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(14, Qt::SmoothTransformation)));
        connect(t3k, &QAction::triggered, this, [this] {
            mLibraryDock->show();
            mLibraryTabs->setTab(LibraryDock::Tone3000);
            if (mTone3000->isSignedIn()) mT3kPanel->browseCatalog();
        });
        libraryMenu->addSeparator();
        connect(libraryMenu->addAction("Folders…"), &QAction::triggered,
                mLibrary, &LibraryPanel::editFolders);
        connect(libraryMenu->addAction("Rescan"), &QAction::triggered,
                mLibraryIndex, &LibraryIndex::rescan);
        libraryMenu->addSeparator();
        connect(libraryMenu->addAction("Import Model…"), &QAction::triggered,
                this, &MainWindow::onImportNAM);
        connect(libraryMenu->addAction("Import Impulse Response…"), &QAction::triggered,
                this, &MainWindow::onImportIR);
    }
}

void MainWindow::makeHeroControls(QWidget* card, LibraryEntry::Kind kind,
                                  QLabel*& nameOut, QLabel*& metaOut, QLabel*& originOut)
{
    auto* lay = static_cast<QHBoxLayout*>(card->layout());
    const bool model = kind == LibraryEntry::Kind::Model;

    // ‹ › cycle the library section in its current visible order.
    auto arrow = [&](const QString& glyph, int step, const QString& tip) {
        auto* b = new QToolButton;
        b->setObjectName("heroArrow");
        b->setText(glyph);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, [this, kind, step] { stepLibrary(kind, step); });
        return b;
    };
    auto* prev = arrow("‹", -1, model ? "Previous model in the library" : "Previous IR in the library");
    auto* next = arrow("›", +1, model ? "Next model in the library"     : "Next IR in the library");

    auto* browse = new QPushButton("Browse");
    browse->setToolTip(model ? "Show the model list in the Library"
                             : "Show the IR list in the Library");
    browse->setFocusPolicy(Qt::NoFocus);
    connect(browse, &QPushButton::clicked, this, [this, kind] {
        mLibraryDock->show();
        mLibraryTabs->setTab(LibraryDock::Local);
        mLibrary->showSection(kind);
    });

    // TONE3000 origin marker (the compact T3K mark), shown only when the
    // loaded file carries a download sidecar.
    originOut = new QLabel;
    originOut->setObjectName("heroOrigin");
    originOut->setPixmap(QPixmap(":/tone3000/t3k-mark.png").scaledToHeight(13, Qt::SmoothTransformation));
    originOut->setCursor(Qt::PointingHandCursor);
    originOut->hide();

    nameOut = new QLabel(model ? "No model loaded" : "No impulse response loaded");
    nameOut->setObjectName("fileStatus");
    metaOut = new QLabel;
    metaOut->setObjectName("fileMeta");

    // Before the trailing stretch: grip, name, toggle, stereo slot = 4 items.
    int i = 4;
    lay->insertWidget(i++, prev,    0, Qt::AlignVCenter);
    lay->insertWidget(i++, next,    0, Qt::AlignVCenter);
    lay->insertWidget(i++, browse,  0, Qt::AlignVCenter);
    lay->insertWidget(i++, originOut, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, nameOut, 0, Qt::AlignVCenter);
    lay->insertWidget(i++, metaOut, 1, Qt::AlignVCenter);
}

void MainWindow::layoutStrip(const std::vector<EffectBase*>& order)
{
    // Empty the layout: the stretch is recreated below, cards are reused
    // (they stay children of the strip while out of the layout).
    while (QLayoutItem* item = mStripLay->takeAt(0))
        delete item;

    for (auto* fx : order)
    {
        auto it = mFxCols.find(fx);
        if (it != mFxCols.end())
            mStripLay->addWidget(it->second);
    }
    mStripLay->addStretch();
    mStripLay->activate(); // fresh geometry now — the drag preview reads card centres
}

void MainWindow::rebuildEffectStrip()
{
    layoutStrip(mEngine->chain());

    // Digit/QWE shortcuts always match the visual top-to-bottom order.
    mShortcuts.clear();
    for (auto* fx : mEngine->chain())
    {
        auto tg = mFxToggles.find(fx);
        if (tg != mFxToggles.end())
            mShortcuts.addEffect(tg->second.enable, tg->second.stereo);
    }
}

void MainWindow::updateCardBypassState(EffectBase* effect, bool enabled)
{
    const auto it = mFxCols.find(effect);
    if (it == mFxCols.end()) return;

    QWidget* card = it->second;
    if (enabled)
    {
        card->setGraphicsEffect(nullptr);
        return;
    }
    auto* opacity = new QGraphicsOpacityEffect(card);
    opacity->setOpacity(0.48);
    card->setGraphicsEffect(opacity);
}

void MainWindow::applyChainOrder(const QStringList& ids)
{
    std::vector<std::string> v;
    v.reserve(ids.size());
    for (const auto& s : ids)
        v.push_back(s.toStdString());
    mEngine->setChainOrder(v);
    rebuildEffectStrip();
}

// ---------------------------------------------------------------------------
// Card drag & drop
// ---------------------------------------------------------------------------

void MainWindow::startCardDrag(QWidget* card)
{
    EffectBase* fx = nullptr;
    for (const auto& [e, w] : mFxCols)
        if (w == card) { fx = e; break; }
    if (!fx) return;
    const std::string id = mEngine->idForEffect(fx);
    if (id.empty()) return;

    mDragId     = id;
    mPreviewIds = mEngine->chainIds();

    auto* mime = new QMimeData;
    mime->setData(kEffectMime, QByteArray::fromStdString(id));
    auto* drag = new QDrag(card);
    drag->setMimeData(mime);
    drag->setPixmap(card->grab()); // grab before dimming
    drag->setHotSpot(mDragStartPos);

    // Dim the in-strip card: it marks its landing slot in the live preview
    // while the full-opacity pixmap follows the cursor.
    auto* dim = new QGraphicsOpacityEffect(card);
    dim->setOpacity(0.35);
    card->setGraphicsEffect(dim);

    drag->exec(Qt::MoveAction);

    // Drop committed inside the event filter; a cancelled drag just falls
    // back to the engine order here.
    auto toggles = mFxToggles.find(fx);
    updateCardBypassState(fx, toggles != mFxToggles.end()
                                 && toggles->second.enable->isChecked());
    mDragId.clear();
    rebuildEffectStrip();
}

void MainWindow::updateDragPreview(const QPoint& pos)
{
    if (mDragId.empty()) return;

    std::vector<std::string> ids = mPreviewIds;
    ids.erase(std::remove(ids.begin(), ids.end(), mDragId), ids.end());
    if (ids.empty()) return;

    // Insert after every card whose centre lies above the cursor.
    int insert = 0;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto it = mFxCols.find(mEngine->effectById(ids[i]));
        if (it != mFxCols.end() && pos.y() > it->second->geometry().center().y())
            insert = static_cast<int>(i) + 1;
    }
    ids.insert(ids.begin() + insert, mDragId);

    if (ids == mPreviewIds) return;
    mPreviewIds = ids;

    std::vector<EffectBase*> order;
    order.reserve(ids.size());
    for (const auto& s : ids)
        if (EffectBase* e = mEngine->effectById(s))
            order.push_back(e);
    layoutStrip(order);
}

void MainWindow::commitDragPreview()
{
    if (mDragId.empty()) return;
    QStringList qids;
    for (const auto& s : mPreviewIds)
        qids << QString::fromStdString(s);
    applyChainOrder(qids);
    scheduleSessionSave();
}

void MainWindow::cancelDragPreview()
{
    if (mDragId.empty()) return;
    mPreviewIds = mEngine->chainIds();
    rebuildEffectStrip();
}

void MainWindow::resetCardToDefaults(QWidget* card)
{
    // The enable toggle keeps its state — resetting restores the effect's
    // sound, it doesn't kick it in or out of the chain.
    ToggleSwitch* enable = nullptr;
    for (const auto& [fx, w] : mFxCols)
        if (w == card)
        {
            auto tg = mFxToggles.find(fx);
            if (tg != mFxToggles.end()) enable = tg->second.enable;
            break;
        }

    // A parameter belongs to this card iff its widget lives inside it — the
    // registry needs no extra per-effect bookkeeping. Widget signals update
    // the engine and schedule the session save.
    for (const auto& p : mParams)
    {
        QWidget* w = p.knob ? static_cast<QWidget*>(p.knob)
                            : static_cast<QWidget*>(p.toggle);
        if (!card->isAncestorOf(w)) continue;
        if (p.toggle && p.toggle == enable) continue;
        if (p.knob) p.knob->setValue(p.knobDefault);
        else        p.toggle->setChecked(p.toggleDefault);
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev)
{
    switch (ev->type())
    {
    case QEvent::MouseButtonPress:
    {
        // Reorder drags start on the ≡ grip only. The grip label ignores
        // mouse events, so the press propagates to the card — where this
        // filter sees it with the position already in card coordinates.
        auto* me   = static_cast<QMouseEvent*>(ev);
        auto* card = qobject_cast<QWidget*>(obj);
        auto  it   = card ? mCardGrips.find(card) : mCardGrips.end();
        if (it != mCardGrips.end() && me->button() == Qt::LeftButton
            && it->second->geometry().contains(me->pos()))
        {
            mDragCard     = card;
            mDragStartPos = me->pos();
        }
        break;
    }
    case QEvent::MouseMove:
    {
        if (!mDragCard || obj != mDragCard)
            break;
        auto* me = static_cast<QMouseEvent*>(ev);
        if (!(me->buttons() & Qt::LeftButton))
        {
            mDragCard = nullptr;
            break;
        }
        if ((me->pos() - mDragStartPos).manhattanLength() < QApplication::startDragDistance())
            break;
        QWidget* card = mDragCard;
        mDragCard = nullptr;
        startCardDrag(card);
        return true;
    }
    case QEvent::MouseButtonRelease:
        if (obj == mDragCard)
            mDragCard = nullptr;
        if (obj == mLblNAMOrigin || obj == mLblIROrigin)
        {
            // T3K mark on a hero row: open that tone in the TONE3000 tab.
            const int toneId = static_cast<QWidget*>(obj)->property("toneId").toInt();
            if (toneId > 0)
            {
                mLibraryDock->show();
                mLibraryTabs->setTab(LibraryDock::Tone3000);
                mT3kPanel->showTone(toneId);
            }
            return true;
        }
        break;
    case QEvent::ContextMenu:
    {
        // Right-click on a card (knobs keep their own menu): per-effect reset.
        auto* card = qobject_cast<QWidget*>(obj);
        if (card && obj != mStrip && card->property("fxCard").toBool())
        {
            auto* ce = static_cast<QContextMenuEvent*>(ev);
            const QString name = card->property("displayName").toString();
            QMenu menu(card);

            // Hero rows: Import (file dialog) and Unload live here — the
            // library is the primary path, so they left the row itself.
            const bool isAmp = card == mFxCols[&mEngine->nam()];
            const bool isCab = card == mFxCols[&mEngine->ir()];
            if (isAmp || isCab)
            {
                connect(menu.addAction(isAmp ? "Import Model…" : "Import Impulse Response…"),
                        &QAction::triggered, this,
                        isAmp ? &MainWindow::onImportNAM : &MainWindow::onImportIR);
                QAction* unload = menu.addAction(isAmp ? "Unload Model" : "Unload Impulse Response");
                unload->setEnabled(isAmp ? mEngine->isModelLoaded() : mEngine->isIRLoaded());
                connect(unload, &QAction::triggered, this,
                        isAmp ? &MainWindow::onUnloadNAM : &MainWindow::onUnloadIR);
                menu.addSeparator();
            }

            QAction* act = menu.addAction("Reset " + name + " to defaults");
            if (menu.exec(ce->globalPos()) == act)
                resetCardToDefaults(card);
            return true;
        }
        break;
    }
    case QEvent::DragEnter:
        if (obj == mStrip)
        {
            auto* de = static_cast<QDragEnterEvent*>(ev);
            if (de->mimeData()->hasFormat(kEffectMime) || hasLoadableUrls(de->mimeData()))
            {
                de->acceptProposedAction(); // files: the strip is the drop
                return true;                // target Qt found, so accept here
            }
        }
        break;
    case QEvent::DragMove:
        if (obj == mStrip)
        {
            auto* de = static_cast<QDragMoveEvent*>(ev);
            if (de->mimeData()->hasFormat(kEffectMime))
            {
                de->acceptProposedAction();
                updateDragPreview(de->position().toPoint());
                return true;
            }
            if (hasLoadableUrls(de->mimeData()))
            {
                de->acceptProposedAction();
                return true;
            }
        }
        break;
    case QEvent::DragLeave:
        if (obj == mStrip)
            cancelDragPreview();
        break;
    case QEvent::Drop:
        if (obj == mStrip)
        {
            auto* de = static_cast<QDropEvent*>(ev);
            if (de->mimeData()->hasFormat(kEffectMime))
            {
                de->acceptProposedAction();
                commitDragPreview();
                return true;
            }
            if (de->mimeData()->hasUrls())
            {
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

void MainWindow::setupAudio()
{
    QSettings s;
    const QString savedIn  = s.value("audio/inputDeviceId").toString();
    const QString savedOut = s.value("audio/outputDeviceId").toString();

    bool started = false;
    if (!savedIn.isEmpty() || !savedOut.isEmpty())
        started = mEngine->startWithDevices(savedIn.toStdString(), savedOut.toStdString());
    if (!started)
        started = mEngine->start("NAMPlayer");

    mAudioOk = started;
    if (started) {
        mAudioBar->refresh();
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        mAudioBar->setError("Not connected");
    }
}

void MainWindow::restartAudioWithCurrentDevices()
{
    const QString inId  = mAudioBar->inputDeviceId();
    const QString outId = mAudioBar->outputDeviceId();

    mAudioOk = mEngine->startWithDevices(inId.toStdString(), outId.toStdString());
    if (mAudioOk) {
        mAudioBar->refresh();
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        mAudioBar->setError("Not connected");
    }
}

// ---------------------------------------------------------------------------
// File slots
// ---------------------------------------------------------------------------

void MainWindow::onImportNAM()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Import NAM Model", QSettings().value("files/lastNAM").toString(),
        "NAM Files (*.nam);;All Files (*)");
    if (!path.isEmpty()) loadModelFile(path);
}

void MainWindow::onUnloadNAM()
{
    mEngine->unloadModel();
    QSettings().remove("files/lastNAM");
    mLibrary->setLoadedPath(LibraryEntry::Kind::Model, {});
    updateHeroCards();
}

void MainWindow::onImportIR()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Import Impulse Response", QSettings().value("files/lastIR").toString(),
        "WAV Files (*.wav);;All Files (*)");
    if (!path.isEmpty()) loadIRFile(path);
}

void MainWindow::onUnloadIR()
{
    mEngine->unloadIR();
    QSettings().remove("files/lastIR");
    mLibrary->setLoadedPath(LibraryEntry::Kind::IR, {});
    updateHeroCards();
}

// ---------------------------------------------------------------------------
// Loading — shared by dialogs, the library, drops, session restore, presets
// ---------------------------------------------------------------------------

bool MainWindow::loadModelFile(const QString& path, bool quiet)
{
    const QString clean = QDir::cleanPath(path);
    // Same file again (preset reload, arrow wrap-around on a one-item list):
    // skip the synchronous rebuild + prewarm and the audible state swap.
    if (!(mEngine->isModelLoaded() && QString::fromStdString(mEngine->modelPath()) == clean))
    {
        if (!mEngine->loadModel(clean.toStdString()))
        {
            if (!quiet)
                QMessageBox::critical(this, "Model", "Failed to load NAM model:\n" + clean + "\n\n"
                                      + QString::fromStdString(mEngine->modelLoadError()));
            return false;
        }
    }
    QSettings().setValue("files/lastNAM", clean);
    mLibrary->noteUsed(clean);
    mLibrary->setLoadedPath(LibraryEntry::Kind::Model, clean);
    updateHeroCards();
    return true;
}

bool MainWindow::loadIRFile(const QString& path, bool quiet)
{
    const QString clean = QDir::cleanPath(path);
    if (!(mEngine->isIRLoaded() && QString::fromStdString(mEngine->irPath()) == clean))
    {
        if (!mEngine->loadIR(clean.toStdString()))
        {
            if (!quiet)
                QMessageBox::critical(this, "Impulse Response", "Failed to load IR:\n" + clean + "\n\n"
                                      + QString::fromStdString(mEngine->irLoadError()));
            return false;
        }
    }
    QSettings().setValue("files/lastIR", clean);
    mLibrary->noteUsed(clean);
    mLibrary->setLoadedPath(LibraryEntry::Kind::IR, clean);
    updateHeroCards();
    return true;
}

void MainWindow::loadLibraryEntry(LibraryEntry::Kind kind, const QString& path)
{
    switch (kind)
    {
    case LibraryEntry::Kind::Model:  loadModelFile(path);  break;
    case LibraryEntry::Kind::IR:     loadIRFile(path);     break;
    case LibraryEntry::Kind::Preset: loadPresetFile(path); break;
    }
}

void MainWindow::stepLibrary(LibraryEntry::Kind kind, int step)
{
    const QString current = kind == LibraryEntry::Kind::Model
                                ? QString::fromStdString(mEngine->modelPath())
                                : QString::fromStdString(mEngine->irPath());
    const QString next = mLibrary->neighbour(kind, current, step);
    if (!next.isEmpty() && next != QDir::cleanPath(current))
        loadLibraryEntry(kind, next);
}

bool MainWindow::hasLoadableUrls(const QMimeData* mime)
{
    if (!mime || !mime->hasUrls()) return false;
    for (const QUrl& u : mime->urls())
    {
        bool ok = false;
        LibraryIndex::kindForPath(u.toLocalFile(), &ok);
        if (u.isLocalFile() && ok) return true;
    }
    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* ev)
{
    if (hasLoadableUrls(ev->mimeData()))
        ev->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* ev)
{
    if (!ev->mimeData()->hasUrls()) return;
    QStringList paths;
    for (const QUrl& u : ev->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!hasLoadableUrls(ev->mimeData())) return;
    ev->acceptProposedAction();
    openPaths(paths);
}

void MainWindow::openPaths(const QStringList& paths)
{
    // One file per kind: the last model, the last IR, the last preset win.
    // The preset goes first so a model/IR given alongside overrides its own.
    QString model, ir, preset;
    for (const QString& p : paths)
    {
        bool ok = false;
        switch (LibraryIndex::kindForPath(p, &ok))
        {
        case LibraryEntry::Kind::Model:  if (ok) model  = p; break;
        case LibraryEntry::Kind::IR:     if (ok) ir     = p; break;
        case LibraryEntry::Kind::Preset: if (ok) preset = p; break;
        }
    }
    if (!preset.isEmpty()) loadPresetFile(preset);
    if (!model.isEmpty())  loadModelFile(model);
    if (!ir.isEmpty())     loadIRFile(ir);
}

// ---------------------------------------------------------------------------
// Parameter registry
// ---------------------------------------------------------------------------

void MainWindow::knobParam(const QString& group, const QString& key,
                           KnobWidget* knob, const std::function<void(double)>& apply)
{
    connect(knob, &KnobWidget::valueChanged, this,
            [this, apply](double v) { apply(v); scheduleSessionSave(); });
    mParams.push_back({group, key, knob, nullptr, knob->defaultValue(), false});
}

void MainWindow::toggleParam(const QString& group, const QString& key,
                             ToggleSwitch* sw, const std::function<void(bool)>& apply)
{
    connect(sw, &ToggleSwitch::toggled, this,
            [this, apply](bool b) { apply(b); scheduleSessionSave(); });
    mParams.push_back({group, key, nullptr, sw, 0.0, sw->isChecked()});
}

// ---------------------------------------------------------------------------
// Session state — the registry serialized to QSettings under "session/"
// ---------------------------------------------------------------------------

void MainWindow::scheduleSessionSave()
{
    if (mSessionSaveTimer) mSessionSaveTimer->start();
    if (mPresetStateTimer) mPresetStateTimer->start();
}

void MainWindow::saveSessionState()
{
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : mParams)
    {
        const QString path = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        s.setValue(path, p.knob ? QVariant(p.knob->value())
                                : QVariant(p.toggle->isChecked()));
    }
    QStringList order;
    for (const auto& id : mEngine->chainIds())
        order << QString::fromStdString(id);
    s.setValue("effectOrder", order);
    // The current preset and the state it was loaded with, so the name and
    // its `*` come back exactly after a restart.
    s.setValue("presetPath", mPresetPath);
    s.setValue("presetBaseline", mPresetPath.isEmpty()
                                     ? QString()
                                     : QString::fromUtf8(QJsonDocument(mPresetBaseline).toJson(QJsonDocument::Compact)));
    s.endGroup();
}

void MainWindow::restoreSessionState()
{
    QSettings s;
    s.beginGroup("session");
    for (const auto& p : mParams)
    {
        const QString path = p.group.isEmpty() ? p.key : p.group + "/" + p.key;
        if (!s.contains(path)) continue;  // first run: widgets keep their defaults
        if (p.knob)
            p.knob->setValue(s.value(path, p.knobDefault).toDouble());
        else
            p.toggle->setChecked(s.value(path, p.toggleDefault).toBool());
    }
    const QStringList order = s.value("effectOrder").toStringList();
    if (!order.isEmpty())
        applyChainOrder(order);
    s.endGroup();
}

// Every user-facing parameter, registered exactly once. Runs after setupUI so
// the captured widget states are the defaults (they mirror the engine's).
// Group/key names are the v1 preset schema; an empty group means the key sits
// at the JSON root.
void MainWindow::registerParameters()
{
    auto* e = mEngine.get();

    knobParam("", "inputGain",  mKnobInputGain,  [e](double v) { e->setInputGain (static_cast<float>(v)); });
    knobParam("", "outputGain", mKnobOutputGain, [e](double v) { e->setOutputGain(static_cast<float>(v)); });

    toggleParam("tuner", "enabled", mSwTunerEnable, [e](bool b) { e->tuner().setEnabled(b); });
    mParams.back().inPreset = false; // a practice tool, not part of the sound

    toggleParam("noiseGate", "enabled", mSwGateEnable,    [e](bool b)   { e->gate().setEnabled(b); });
    knobParam("noiseGate", "threshold", mKnobGateThresh,  [e](double v) { e->gate().setThreshold(static_cast<float>(v)); });
    knobParam("noiseGate", "attack",    mKnobGateAttack,  [e](double v) { e->gate().setAttack   (static_cast<float>(v)); });
    knobParam("noiseGate", "release",   mKnobGateRelease, [e](double v) { e->gate().setRelease  (static_cast<float>(v)); });

    toggleParam("compressor", "enabled", mSwCompEnable,   [e](bool b)   { e->comp().setEnabled(b); });
    knobParam("compressor", "threshold", mKnobCompThresh, [e](double v) { e->comp().setThreshold(static_cast<float>(v)); });
    knobParam("compressor", "ratio",     mKnobCompRatio,  [e](double v) { e->comp().setRatio    (static_cast<float>(v)); });
    knobParam("compressor", "attack",    mKnobCompAttack, [e](double v) { e->comp().setAttack   (static_cast<float>(v)); });
    knobParam("compressor", "release",   mKnobCompRelease,[e](double v) { e->comp().setRelease  (static_cast<float>(v)); });
    knobParam("compressor", "makeup",    mKnobCompMakeup, [e](double v) { e->comp().setMakeup   (static_cast<float>(v)); });

    toggleParam("dynamicWah", "enabled",   mSwWahEnable, [e](bool b)   { e->wah().setEnabled(b); });
    knobParam("dynamicWah", "sensitivity", mKnobWahSens, [e](double v) { e->wah().setSensitivity(static_cast<float>(v)); });
    knobParam("dynamicWah", "resonance",   mKnobWahRes,  [e](double v) { e->wah().setResonance  (static_cast<float>(v)); });
    knobParam("dynamicWah", "mix",         mKnobWahMix,  [e](double v) { e->wah().setMix        (static_cast<float>(v)); });

    toggleParam("flanger", "enabled", mSwFlangerEnable, [e](bool b)   { e->flanger().setEnabled   (b); });
    toggleParam("flanger", "stereo",  mSwFlangerStereo, [e](bool b)   { e->flanger().setStereoMode(b); });
    knobParam("flanger", "rate",      mKnobFlRate,      [e](double v) { e->flanger().setRate    (static_cast<float>(v)); });
    knobParam("flanger", "depth",     mKnobFlDepth,     [e](double v) { e->flanger().setDepth   (static_cast<float>(v)); });
    knobParam("flanger", "feedback",  mKnobFlFeedback,  [e](double v) { e->flanger().setFeedback(static_cast<float>(v)); });
    knobParam("flanger", "mix",       mKnobFlMix,       [e](double v) { e->flanger().setMix     (static_cast<float>(v)); });

    toggleParam("phaser", "enabled", mSwPhaserEnable, [e](bool b)   { e->phaser().setEnabled   (b); });
    toggleParam("phaser", "stereo",  mSwPhaserStereo, [e](bool b)   { e->phaser().setStereoMode(b); });
    knobParam("phaser", "rate",      mKnobPhRate,     [e](double v) { e->phaser().setRate    (static_cast<float>(v)); });
    knobParam("phaser", "depth",     mKnobPhDepth,    [e](double v) { e->phaser().setDepth   (static_cast<float>(v)); });
    knobParam("phaser", "feedback",  mKnobPhFeedback, [e](double v) { e->phaser().setFeedback(static_cast<float>(v)); });
    knobParam("phaser", "mix",       mKnobPhMix,      [e](double v) { e->phaser().setMix     (static_cast<float>(v)); });

    toggleParam("", "namEnabled", mSwNAMEnable, [e](bool b) { e->nam().setEnabled(b); });

    toggleParam("eq", "enabled", mSwEQEnable, [e](bool b)   { e->eq().setEnabled(b); });
    knobParam("eq", "low",       mKnobLow,    [e](double v) { e->eq().setLowGain   (v); });
    knobParam("eq", "mid",       mKnobMid,    [e](double v) { e->eq().setMidGain   (v); });
    knobParam("eq", "treble",    mKnobTreble, [e](double v) { e->eq().setTrebleGain(v); });

    toggleParam("chorus", "enabled", mSwChorusEnable, [e](bool b)   { e->chorus().setEnabled(b); });
    toggleParam("chorus", "stereo",  mSwChorusStereo, [e](bool b)   { e->chorus().setStereoMode(b); });
    knobParam("chorus", "rate",      mKnobChRate,     [e](double v) { e->chorus().setRate (static_cast<float>(v)); });
    knobParam("chorus", "depth",     mKnobChDepth,    [e](double v) { e->chorus().setDepth(static_cast<float>(v)); });
    knobParam("chorus", "mix",       mKnobChMix,      [e](double v) { e->chorus().setMix  (static_cast<float>(v)); });

    toggleParam("tremolo", "enabled", mSwTremoloEnable, [e](bool b)   { e->tremolo().setEnabled(b); });
    toggleParam("tremolo", "stereo",  mSwTremoloStereo, [e](bool b)   { e->tremolo().setStereoMode(b); });
    knobParam("tremolo", "rate",      mKnobTrRate,      [e](double v) { e->tremolo().setRate (static_cast<float>(v)); });
    knobParam("tremolo", "depth",     mKnobTrDepth,     [e](double v) { e->tremolo().setDepth(static_cast<float>(v)); });

    toggleParam("delay", "enabled", mSwDelayEnable,   [e](bool b)   { e->delay().setEnabled   (b); });
    toggleParam("delay", "stereo",  mSwDelayStereo,   [e](bool b)   { e->delay().setStereoMode(b); });
    knobParam("delay", "time",      mKnobDelTime,     [e](double v) { e->delay().setTime    (static_cast<float>(v)); });
    knobParam("delay", "feedback",  mKnobDelFeedback, [e](double v) { e->delay().setFeedback(static_cast<float>(v)); });
    knobParam("delay", "mix",       mKnobDelMix,      [e](double v) { e->delay().setMix     (static_cast<float>(v)); });

    toggleParam("reverb", "enabled", mSwReverbEnable, [e](bool b)   { e->reverb().setEnabled(b); });
    knobParam("reverb", "roomSize",  mKnobRevRoom,    [e](double v) { e->reverb().setRoomSize(static_cast<float>(v)); });
    knobParam("reverb", "damping",   mKnobRevDamping, [e](double v) { e->reverb().setDamping (static_cast<float>(v)); });
    knobParam("reverb", "width",     mKnobRevWidth,   [e](double v) { e->reverb().setWidth   (static_cast<float>(v)); });
    knobParam("reverb", "mix",       mKnobRevMix,     [e](double v) { e->reverb().setMix     (static_cast<float>(v)); });

    toggleParam("", "irEnabled", mSwIREnable, [e](bool b) { e->ir().setEnabled(b); });
}

// ---------------------------------------------------------------------------
// Meters & tuner
// ---------------------------------------------------------------------------

void MainWindow::updateMeters()
{
    mMeterIn ->setPeak(mEngine->takeInputPeak());
    mMeterOut->setPeak(mEngine->takeOutputPeak());
    mGrGate->setReduction(mEngine->gate().takeGainReductionDb());
    mGrComp->setReduction(mEngine->comp().takeGainReductionDb());
}

void MainWindow::updateMeterTimer()
{
    if (!mMeterTimer) return;
    const bool visible = isVisible() && !isMinimized();
    if (visible && !mMeterTimer->isActive()) mMeterTimer->start();
    else if (!visible)                       mMeterTimer->stop();
}

void MainWindow::changeEvent(QEvent* ev)
{
    QMainWindow::changeEvent(ev);
    if (ev->type() == QEvent::WindowStateChange) updateMeterTimer();
}

void MainWindow::showEvent(QShowEvent* ev)
{
    QMainWindow::showEvent(ev);
    updateMeterTimer();
}

void MainWindow::hideEvent(QHideEvent* ev)
{
    QMainWindow::hideEvent(ev);
    updateMeterTimer();
}

void MainWindow::updateStatus()
{
    mAudioBar->updateMetrics(); // PipeWire can change the quantum while running
    if (mAudioOk && !mEngine->isRunning())
    {
        mAudioOk = false;
        mAudioBar->setError("Disconnected");
    }
}

void MainWindow::updateTuner()
{
    const float freq = mEngine->tuner().isEnabled() ? mEngine->tuner().lastPitch()
                                                    : 0.0f;
    if (freq <= 0.0f)
    {
        mTunerDisplay->setReading("-", 0.0f, false);
        return;
    }

    const float ref   = static_cast<float>(mTunerRefHz);
    const int   midi  = TunerProcessor::freqToMidi(freq, ref);
    const float cents = TunerProcessor::centsOffset(freq, ref);
    mTunerDisplay->setReading(QString::fromStdString(TunerProcessor::midiToName(midi)),
                              cents, true, freq);
}

// ---------------------------------------------------------------------------
// Persisted file state
// ---------------------------------------------------------------------------

void MainWindow::restoreLastFiles()
{
    QSettings s;

    const QString nam = s.value("files/lastNAM").toString();
    if (!nam.isEmpty() && QFile::exists(nam))
        loadModelFile(nam, /*quiet=*/true);

    const QString ir = s.value("files/lastIR").toString();
    if (!ir.isEmpty() && QFile::exists(ir))
        loadIRFile(ir, /*quiet=*/true);

    updateHeroCards();
}

void MainWindow::restoreConnections()
{
    QSettings s;
    if (!s.contains("connections/saved"))
    {
        // First run: wire the default system ports so a new user hears sound
        // without opening a patchbay. Failed connects (no such port) are
        // harmless no-ops, and the user's own routing is saved on exit.
        AudioEngine::PortConnections c;
        c.inputSources = {"system:capture_1"};
        c.outputLDests = {"system:playback_1"};
        c.outputRDests = {"system:playback_2"};
        mEngine->applyConnections(c);
        return;
    }

    auto toVec = [](const QStringList& l) {
        std::vector<std::string> v;
        v.reserve(l.size());
        for (const auto& x : l) v.push_back(x.toStdString());
        return v;
    };

    AudioEngine::PortConnections c;
    c.inputSources = toVec(s.value("connections/inputSources").toStringList());
    c.outputLDests = toVec(s.value("connections/outputLDests").toStringList());
    c.outputRDests = toVec(s.value("connections/outputRDests").toStringList());
    mEngine->applyConnections(c);
}

void MainWindow::saveConnections()
{
    if (!mEngine->isRunning() || !mEngine->supportsPortRouting()) return;

    auto toList = [](const std::vector<std::string>& v) {
        QStringList l;
        l.reserve(static_cast<int>(v.size()));
        for (const auto& x : v) l << QString::fromStdString(x);
        return l;
    };

    auto c = mEngine->currentConnections();
    QSettings s;
    s.setValue("connections/inputSources", toList(c.inputSources));
    s.setValue("connections/outputLDests", toList(c.outputLDests));
    s.setValue("connections/outputRDests", toList(c.outputRDests));
    s.setValue("connections/saved", true);
}

// ---------------------------------------------------------------------------
// Hero rows (Amplifier / Cabinet)
// ---------------------------------------------------------------------------

void MainWindow::updateHeroCards()
{
    auto setStatus = [this](QLabel* name, QLabel* meta, QLabel* origin, bool loaded,
                            const QString& path, const QString& emptyText) {
        if (!loaded)
        {
            name->setText(emptyText);
            name->setObjectName("fileStatus");
            name->setToolTip({});
            meta->clear();
            meta->setToolTip({});
            origin->hide();
        }
        else
        {
            // Metadata comes from the index when the file is in the library,
            // otherwise it is read now (one-off imports, dropped files).
            const LibraryEntry* e = mLibraryIndex->find(path);
            const LibraryEntry  fresh = e ? *e : LibraryIndex::describe(path);
            name->setText(name->fontMetrics().elidedText(fresh.name, Qt::ElideMiddle, kHeroNameMaxWidth));
            name->setObjectName("fileStatusOk");
            name->setToolTip(QDir::toNativeSeparators(fresh.path));
            meta->setText(fresh.summary);
            meta->setToolTip(fresh.details);
            origin->setVisible(fresh.fromTone3000());
            origin->setProperty("toneId", fresh.t3kToneId);
            origin->setToolTip(fresh.fromTone3000()
                                   ? QString("From TONE3000: %1 by @%2 (click to open)")
                                         .arg(fresh.t3kTitle, fresh.t3kCreator)
                                   : QString());
        }
        name->style()->unpolish(name);
        name->style()->polish(name);
    };

    setStatus(mLblNAMStatus, mLblNAMMeta, mLblNAMOrigin, mEngine->isModelLoaded(),
              QString::fromStdString(mEngine->modelPath()), "No model loaded");
    setStatus(mLblIRStatus, mLblIRMeta, mLblIROrigin, mEngine->isIRLoaded(),
              QString::fromStdString(mEngine->irPath()), "No impulse response loaded");
    // Every model/IR change lands here — it counts toward the preset `*`.
    if (mPresetStateTimer) mPresetStateTimer->start();
    updateRigStatus();
}

void MainWindow::updateRigStatus()
{
    auto item = [](bool on, const QString& text) {
        return QString("<span style=\"color:%1\">●</span> %2")
            .arg((on ? Theme::meterLow() : Theme::track()).name(), text.toHtmlEscaped());
    };
    const bool model = mEngine->isModelLoaded();
    const bool ir    = mEngine->isIRLoaded();
    const bool t3k   = mTone3000->isSignedIn();
    QString t3kText = "TONE3000";
    if (t3k && mTone3000->user().isValid()) t3kText += " @" + mTone3000->user().username;

    mLblRigStatus->setText(item(model, "Model") + "&nbsp;&nbsp;&nbsp;" + item(ir, "IR")
                           + "&nbsp;&nbsp;&nbsp;" + item(t3k, t3kText));
    mLblRigStatus->setToolTip(
        QString("Model: %1\nIR: %2\nTONE3000: %3")
            .arg(model ? QFileInfo(QString::fromStdString(mEngine->modelPath())).completeBaseName() : "none",
                 ir    ? QFileInfo(QString::fromStdString(mEngine->irPath())).completeBaseName()    : "none",
                 t3k   ? "signed in" : "signed out"));
}

// ---------------------------------------------------------------------------
// Preset save / load
// ---------------------------------------------------------------------------

void MainWindow::seedStarterPresets()
{
    // Once per config: copy the bundled starters into <presets>/Starter so a
    // first run isn't empty. They carry no model/IR — loading one keeps the
    // current rig. Deleting them sticks: the flag stops a re-seed.
    QSettings s;
    if (s.value("presets/starterSeeded").toBool()) return;
    const QString dir = LibraryRoots::defaultPresetDir() + "/Starter";
    QDir().mkpath(dir);
    for (const QFileInfo& fi : QDir(":/presets").entryInfoList({"*.nampreset"}, QDir::Files))
    {
        const QString dest = dir + "/" + fi.fileName();
        if (QFile::exists(dest) || !QFile::copy(fi.filePath(), dest)) continue;
        // Resource files copy read-only.
        QFile::setPermissions(dest, QFile::permissions(dest) | QFile::WriteOwner);
    }
    s.setValue("presets/starterSeeded", true);
}

QJsonObject MainWindow::currentPresetJson() const
{
    QJsonObject obj = PresetSerializer::toJson(mParams, *mEngine);
    // TONE3000 linkage: lets the preset find its model on another machine.
    auto linkage = [this](const std::string& p) {
        QJsonObject o;
        if (const LibraryEntry* e = mLibraryIndex->find(QString::fromStdString(p)); e && e->fromTone3000())
        {
            o["toneId"]  = e->t3kToneId;
            o["modelId"] = e->t3kModelId;
            o["title"]   = e->t3kTitle;
            o["creator"] = e->t3kCreator;
        }
        return o;
    };
    QJsonObject t3k;
    if (mEngine->isModelLoaded()) { const QJsonObject m = linkage(mEngine->modelPath()); if (!m.isEmpty()) t3k["model"] = m; }
    if (mEngine->isIRLoaded())    { const QJsonObject m = linkage(mEngine->irPath());    if (!m.isEmpty()) t3k["ir"]    = m; }
    if (!t3k.isEmpty()) obj["tone3000"] = t3k;
    return obj;
}

bool MainWindow::writePresetFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "Preset", "Cannot write preset:\n" + path);
        return false;
    }
    f.write(QJsonDocument(currentPresetJson()).toJson());
    f.close();
    return true;
}

// Save-dialog path with the .nampreset suffix; empty when cancelled.
static QString askPresetSavePath(QWidget* parent, const QString& title, const QString& start)
{
    QString path = QFileDialog::getSaveFileName(
        parent, title, start, "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (!path.isEmpty() && QFileInfo(path).suffix().isEmpty())
        path += ".nampreset"; // portal dialogs don't append the filter's suffix
    return path;
}

void MainWindow::onSavePreset()
{
    if (mPresetPath.isEmpty() || !QFile::exists(mPresetPath))
    {
        onSavePresetAs();
        return;
    }
    if (writePresetFile(mPresetPath))
        setCurrentPreset(mPresetPath);
}

void MainWindow::onSavePresetAs()
{
    // Default to the library's presets folder so a saved preset shows up in
    // the list right away (the folder watcher picks it up).
    QString start = mPresetPath;
    if (start.isEmpty())
    {
        start = LibraryRoots::defaultPresetDir();
        QDir().mkpath(start);
    }
    QString path = askPresetSavePath(nullptr, "Save Preset As", start);
    if (path.isEmpty() || !writePresetFile(path)) return;

    path = QDir::cleanPath(path);
    QSettings().setValue("files/lastPreset", path);
    mLibrary->noteUsed(path);
    mLibrary->setLoadedPath(LibraryEntry::Kind::Preset, path);
    setCurrentPreset(path);
}

void MainWindow::onExportPreset()
{
    const QString name = mPresetPath.isEmpty() ? QString("Untitled.nampreset")
                                               : QFileInfo(mPresetPath).fileName();
    const QString path = askPresetSavePath(
        nullptr, "Export Preset",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + name);
    if (!path.isEmpty()) writePresetFile(path); // a copy — the current preset stays
}

void MainWindow::onImportPresets()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        nullptr, "Import Presets", QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (files.isEmpty()) return;

    const QString dir = LibraryRoots::defaultPresetDir();
    QDir().mkpath(dir);
    QString last;
    QStringList failed;
    for (const QString& src : files)
    {
        const QFileInfo fi(src);
        if (QDir::cleanPath(fi.absolutePath()) == QDir::cleanPath(dir)) { last = src; continue; }
        // Never overwrite: "Name.nampreset" → "Name (2).nampreset" …
        QString dest = dir + "/" + fi.fileName();
        for (int n = 2; QFile::exists(dest); ++n)
            dest = QString("%1/%2 (%3).%4").arg(dir, fi.completeBaseName()).arg(n).arg(fi.suffix());
        if (QFile::copy(src, dest)) last = dest;
        else                        failed << src;
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Import Presets", "Could not copy:\n" + failed.join('\n'));
    if (!last.isEmpty()) loadPresetFile(last);
}

void MainWindow::onDeletePreset()
{
    if (mPresetPath.isEmpty() || !QFile::exists(mPresetPath)) return;
    const QString name = QFileInfo(mPresetPath).completeBaseName();
    if (QMessageBox::question(this, "Delete Preset",
                              "Move the preset \"" + name + "\" to the trash?\n"
                              "The current sound is kept as an untitled state.")
        != QMessageBox::Yes)
        return;
    if (!QFile::moveToTrash(mPresetPath) && !QFile::remove(mPresetPath))
    {
        QMessageBox::critical(this, "Delete Preset", "Cannot delete:\n" + mPresetPath);
        return;
    }
    mLibrary->setLoadedPath(LibraryEntry::Kind::Preset, {});
    setCurrentPreset({});
}

void MainWindow::onLocatePreset()
{
    const QString dir = mPresetPath.isEmpty() ? LibraryRoots::defaultPresetDir()
                                              : QFileInfo(mPresetPath).absolutePath();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::setCurrentPreset(const QString& path)
{
    mPresetPath     = path;
    mPresetBaseline = PresetSerializer::toJson(mParams, *mEngine);
    mAb[0] = mAb[1] = QJsonObject();
    mAbActive = 0;
    mPresetBar->setAbState(0);
    updatePresetState();
    scheduleSessionSave();
}

void MainWindow::updatePresetState()
{
    const bool modified = !mPresetPath.isEmpty()
                          && PresetSerializer::toJson(mParams, *mEngine) != mPresetBaseline;
    mPresetBar->setPreset(mPresetPath, modified);
    setWindowTitle(mPresetPath.isEmpty()
                       ? QString("NAM Player")
                       : QString("%1%2 — NAM Player")
                             .arg(QFileInfo(mPresetPath).completeBaseName(), modified ? "*" : ""));
}

void MainWindow::restorePresetState()
{
    QSettings s;
    const QString path = s.value("session/presetPath").toString();
    if (path.isEmpty() || !QFile::exists(path)) { updatePresetState(); return; }
    mPresetPath     = path;
    mPresetBaseline = QJsonDocument::fromJson(s.value("session/presetBaseline").toString().toUtf8()).object();
    mLibrary->setLoadedPath(LibraryEntry::Kind::Preset, path);
    updatePresetState();
}

void MainWindow::stepPreset(int step)
{
    const QString next = mLibrary->neighbour(LibraryEntry::Kind::Preset, mPresetPath, step);
    if (!next.isEmpty() && next != mPresetPath)
        loadPresetFile(next);
}

void MainWindow::switchAb(int side)
{
    if (side == mAbActive) return;
    // The side being left keeps the live state; the other side starts as a
    // copy of it the first time it is visited.
    const QJsonObject live = PresetSerializer::toJson(mParams, *mEngine);
    mAb[mAbActive] = live;
    mAbActive = side;
    if (mAb[side].isEmpty())
    {
        mAb[side] = live;
    }
    else
    {
        const auto applied = applyPresetParams(mAb[side]);
        if (!applied.namPath.isEmpty() && QFile::exists(applied.namPath)) loadModelFile(applied.namPath, true);
        if (!applied.irPath.isEmpty()  && QFile::exists(applied.irPath))  loadIRFile(applied.irPath, true);
    }
    mPresetBar->setAbState(side);
    scheduleSessionSave();
}

void MainWindow::copyAb()
{
    mAb[1 - mAbActive] = PresetSerializer::toJson(mParams, *mEngine);
}

void MainWindow::onResetAll()
{
    if (QMessageBox::question(this, "Reset All",
            "Reset all effects, gains, and the chain order to their defaults?\n"
            "The loaded model and IR are kept.")
        != QMessageBox::Yes)
        return;

    // Registry defaults through the widgets — the engine follows via the
    // registry connections, exactly like a preset load.
    for (const auto& p : mParams)
    {
        if (p.knob) p.knob->setValue(p.knobDefault);
        else        p.toggle->setChecked(p.toggleDefault);
    }

    QStringList order;
    for (const auto& id : mEngine->defaultChainIds())
        order << QString::fromStdString(id);
    applyChainOrder(order);
    scheduleSessionSave();
}

void MainWindow::onLoadPreset()
{
    QString start = QSettings().value("files/lastPreset").toString();
    if (start.isEmpty()) start = LibraryRoots::defaultPresetDir();
    const QString path = QFileDialog::getOpenFileName(
        nullptr, "Load Preset", start,
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (!path.isEmpty()) loadPresetFile(path);
}

bool MainWindow::loadPresetFile(const QString& pathIn)
{
    const QString path = QDir::cleanPath(pathIn);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, "Preset", "Cannot read preset:\n" + path);
        return false;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (!doc.isObject())
    {
        QMessageBox::critical(this, "Preset", "Invalid preset:\n" + path + "\n\n"
                              + (doc.isNull() ? err.errorString() : QStringLiteral("not a JSON object")));
        return false;
    }
    QSettings settings;
    settings.setValue("files/lastPreset", path);
    mLibrary->noteUsed(path);
    mLibrary->setLoadedPath(LibraryEntry::Kind::Preset, path);

    const auto applied = applyPresetParams(doc.object());

    // v2 presets embed the model/IR paths. Only a non-empty path acts — a
    // preset saved without a model must not unload the current one — and when
    // the referenced file is missing or fails to load, the current one stays.
    // A referenced file that isn't there: the TONE3000 linkage can still find
    // it (same model id elsewhere in the library) or fetch it.
    const QJsonObject t3k = doc.object().value("tone3000").toObject();
    auto tryLoad = [this](const QString& p, LibraryEntry::Kind kind) {
        if (p.isEmpty() || !QFile::exists(p)) return false;
        return kind == LibraryEntry::Kind::Model ? loadModelFile(p, true) : loadIRFile(p, true);
    };
    // A TONE3000 fetch can land after this returns: the fetched model is
    // part of the preset, not an edit, so it moves the baseline.
    auto rebaseline = [this, path] { if (mPresetPath == path) setCurrentPreset(path); };

    QStringList failed;
    if (!applied.namPath.isEmpty() && !tryLoad(applied.namPath, LibraryEntry::Kind::Model))
    {
        const QJsonObject link = t3k.value("model").toObject();
        const int modelId = link.value("modelId").toInt();
        if (modelId > 0)
        {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.namPath).completeBaseName()
                                      : link.value("title").toString();
            resolveTone3000Model(modelId, title, [this, tryLoad, rebaseline](const QString& p) {
                if (!p.isEmpty() && tryLoad(p, LibraryEntry::Kind::Model)) rebaseline();
            });
        }
        else
        {
            failed << applied.namPath;
        }
    }
    if (!applied.irPath.isEmpty() && !tryLoad(applied.irPath, LibraryEntry::Kind::IR))
    {
        const QJsonObject link = t3k.value("ir").toObject();
        const int modelId = link.value("modelId").toInt();
        if (modelId > 0)
        {
            const QString title = link.value("title").toString().isEmpty()
                                      ? QFileInfo(applied.irPath).completeBaseName()
                                      : link.value("title").toString();
            resolveTone3000Model(modelId, title, [this, tryLoad, rebaseline](const QString& p) {
                if (!p.isEmpty() && tryLoad(p, LibraryEntry::Kind::IR)) rebaseline();
            });
        }
        else
        {
            failed << applied.irPath;
        }
    }
    updateHeroCards();
    setCurrentPreset(path);
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Preset",
                             "The preset references files that could not be loaded "
                             "(the current model/IR was kept):\n" + failed.join('\n'));
    return failed.isEmpty();
}

PresetSerializer::Applied MainWindow::applyPresetParams(const QJsonObject& obj)
{
    // Widgets first (engine follows via the registry connections), then the
    // chain order (engine-side sanitizer handles unknown/missing IDs).
    const auto applied = PresetSerializer::apply(obj, mParams);
    if (!applied.effectOrder.isEmpty())
        applyChainOrder(applied.effectOrder);
    return applied;
}

void MainWindow::resolveTone3000Model(int modelId, const QString& title,
                                      std::function<void(const QString&)> then)
{
    // 1. Already in the library under another path (downloaded on this machine).
    if (const LibraryEntry* e = mLibraryIndex->findTone3000Model(modelId))
    {
        then(e->path);
        return;
    }

    // 2. Offer to fetch it.
    if (!Tone3000Client::hasClientId())
    {
        QMessageBox::warning(this, "Preset",
                             "This preset uses \"" + title + "\" from TONE3000, which isn't on this computer, "
                             "and TONE3000 isn't set up in this build (Library → TONE3000).");
        then({});
        return;
    }
    if (!mTone3000->isSignedIn())
    {
        QMessageBox::information(this, "Preset",
                                 "This preset uses \"" + title + "\" from TONE3000, which isn't on this computer.\n\n"
                                 "Sign in on the Library's TONE3000 tab, then load the preset again to fetch it.");
        mLibraryDock->show();
        mLibraryTabs->setTab(LibraryDock::Tone3000);
        then({});
        return;
    }
    if (QMessageBox::question(this, "Preset",
                              "This preset uses \"" + title + "\" from TONE3000, which isn't on this computer.\n\n"
                              "Download it into the library now?")
        != QMessageBox::Yes)
    {
        then({});
        return;
    }

    mTone3000->fetchModel(modelId, [this, then, title](const Tone3000Client::Result& r) {
        if (!r.ok)
        {
            QMessageBox::warning(this, "TONE3000", "Could not look up \"" + title + "\": " + r.error);
            then({});
            return;
        }
        const T3kModel model = Tone3000Client::parseModel(r.object());
        mTone3000->fetchTone(model.toneId, [this, then, title, model](const Tone3000Client::Result& tr) {
            if (!tr.ok)
            {
                QMessageBox::warning(this, "TONE3000", "Could not look up the tone of \"" + title + "\": " + tr.error);
                then({});
                return;
            }
            const T3kTone tone = Tone3000Client::parseTone(tr.object());
            mTone3000->downloadModel(model, tone, [this, then, title](bool ok, QString path, QString err) {
                if (!ok)
                {
                    QMessageBox::warning(this, "TONE3000", "Download of \"" + title + "\" failed: " + err);
                    then({});
                    return;
                }
                mLibraryIndex->rescan();
                then(path);
            });
        });
    });
}

#include "MainWindow.h"
#include "AudioStatusBar.h"
#include "Constants.h"
#include "EffectShortcuts.h"
#include "PresetSerializer.h"
#include "Theme.h"
#include "TunerProcessor.h"
#include "VUMeter.h"

#include <QAction>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
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
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
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
static constexpr int kNameColumnWidth = 200; // fits "Cabinet (Impulse Response)"
static constexpr int kEnableSlotWidth = 74;  // toggle + "On"/"Off" caption
static constexpr int kStereoSlotWidth = 126; // toggle + "Stereo mode" caption

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
        auto* lbl = new QLabel("Stereo mode");
        lbl->setObjectName("toggleCaption");
        lay->addWidget(sw, 0, Qt::AlignVCenter);
        lay->addWidget(lbl, 0, Qt::AlignVCenter);
        lay->addStretch();
        *outToggle = sw;
    }

    return w;
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

    setupUI();
    registerParameters();

    // Debounced session save — every registry widget change restarts it, so
    // the session survives a crash without a write per knob tick. Created
    // before restoreSessionState so the restore's widget signals find it.
    mSessionSaveTimer = new QTimer(this);
    mSessionSaveTimer->setSingleShot(true);
    mSessionSaveTimer->setInterval(1000);
    connect(mSessionSaveTimer, &QTimer::timeout, this, &MainWindow::saveSessionState);

    restoreSessionState();
    {
        const QByteArray geo = QSettings().value("window/geometry").toByteArray();
        if (!geo.isEmpty()) restoreGeometry(geo);
    }

    setupAudio();
    restoreLastFiles();

    mMeterTimer = new QTimer(this);
    connect(mMeterTimer, &QTimer::timeout, this, &MainWindow::updateMeters);
    mMeterTimer->start(50);

    mTunerTimer = new QTimer(this);
    connect(mTunerTimer, &QTimer::timeout, this, &MainWindow::updateTuner);
    mTunerTimer->start(100);
}

MainWindow::~MainWindow()
{
    mMeterTimer->stop();
    mTunerTimer->stop();
    mSessionSaveTimer->stop();
    QSettings().setValue("window/geometry", saveGeometry());
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
    {
        auto* mb         = menuBar();
        auto* presetMenu = mb->addMenu("Preset");
        auto* actSave = presetMenu->addAction("Save Preset…");
        actSave->setShortcut(QKeySequence::Save); // Ctrl+S
        connect(actSave, &QAction::triggered, this, &MainWindow::onSavePreset);
        auto* actLoad = presetMenu->addAction("Load Preset…");
        actLoad->setShortcut(QKeySequence::Open); // Ctrl+O
        connect(actLoad, &QAction::triggered, this, &MainWindow::onLoadPreset);
        presetMenu->addSeparator();
        connect(presetMenu->addAction("Reset All to Defaults…"), &QAction::triggered, this, &MainWindow::onResetAll);

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

    // ---- Status bar (bottom right): backend, rate, buffer, latency, devices.
    // Preset save/load/reset live in the Preset menu — no top button row.
    mAudioBar = new AudioStatusBar(*mEngine);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addPermanentWidget(mAudioBar);
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

        mLblNote = new QLabel("--");
        mLblNote->setObjectName("tunerNote");
        {
            QFont f = mLblNote->font();
            f.setPointSize(f.pointSize() + 7);
            f.setBold(true);
            mLblNote->setFont(f);
        }
        mLblNote->setMinimumWidth(52);
        mLblNote->setAlignment(Qt::AlignCenter);

        mLblCents = new QLabel("±0¢");
        mLblCents->setObjectName("tunerCents");
        mLblCents->setMinimumWidth(52);
        mLblCents->setAlignment(Qt::AlignCenter);

        mTunerBar = new QProgressBar;
        mTunerBar->setObjectName("tunerBar"); // chunk color lives in the Theme stylesheet
        mTunerBar->setOrientation(Qt::Horizontal);
        mTunerBar->setRange(0, 100);
        mTunerBar->setValue(50);
        mTunerBar->setTextVisible(false);
        mTunerBar->setFixedHeight(14);
        mTunerBar->setMinimumWidth(180);

        auto* reading = new QWidget;
        auto* readingLay = new QVBoxLayout(reading);
        readingLay->setContentsMargins(0, 0, 0, 0);
        readingLay->setSpacing(2);
        readingLay->addWidget(mLblNote);
        readingLay->addWidget(mLblCents);
        lay->addWidget(reading, 0, Qt::AlignVCenter);
        lay->addWidget(mTunerBar, 1, Qt::AlignVCenter); // bar fills the row

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

        auto* lay = static_cast<QHBoxLayout*>(col->layout());
        mBtnLoadNAM   = new QPushButton("Load…");
        mBtnUnloadNAM = new QPushButton("Unload");
        mBtnUnloadNAM->setEnabled(false);
        mLblNAMStatus = new QLabel("No model loaded");
        mLblNAMStatus->setObjectName("fileStatus");
        connect(mBtnLoadNAM,   &QPushButton::clicked, this, &MainWindow::onLoadNAM);
        connect(mBtnUnloadNAM, &QPushButton::clicked, this, &MainWindow::onUnloadNAM);
        // Before the trailing stretch: grip, name, toggle, stereo slot = 4 items.
        lay->insertWidget(4, mBtnLoadNAM,   0, Qt::AlignVCenter);
        lay->insertWidget(5, mBtnUnloadNAM, 0, Qt::AlignVCenter);
        lay->insertWidget(6, mLblNAMStatus, 1, Qt::AlignVCenter);

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
            {mKnobChMix, mKnobChRate, mKnobChDepth});
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

        auto* col = makeEffectRow("Tremolo", mSwTremoloEnable, {mKnobTrRate, mKnobTrDepth});
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
        auto* col = makeEffectRow("Cabinet (Impulse Response)", mSwIREnable, {});
        mSwIREnable->setChecked(true);

        auto* lay = static_cast<QHBoxLayout*>(col->layout());
        mBtnLoadIR   = new QPushButton("Load…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mLblIRStatus = new QLabel("No impulse response loaded");
        mLblIRStatus->setObjectName("fileStatus");
        connect(mBtnLoadIR,   &QPushButton::clicked, this, &MainWindow::onLoadIR);
        connect(mBtnUnloadIR, &QPushButton::clicked, this, &MainWindow::onUnloadIR);
        lay->insertWidget(4, mBtnLoadIR,   0, Qt::AlignVCenter);
        lay->insertWidget(5, mBtnUnloadIR, 0, Qt::AlignVCenter);
        lay->insertWidget(6, mLblIRStatus, 1, Qt::AlignVCenter);

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
        {&mEngine->chorus(),  {mSwChorusEnable,  nullptr}},
        {&mEngine->tremolo(), {mSwTremoloEnable, nullptr}},
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
            if (de->mimeData()->hasFormat(kEffectMime))
            {
                de->acceptProposedAction();
                return true;
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
        mAudioBar->setError("Not connected — click to retry");
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
        mAudioBar->setError("Not connected — click to retry");
    }
}

// ---------------------------------------------------------------------------
// File slots
// ---------------------------------------------------------------------------

void MainWindow::onLoadNAM()
{
    QSettings s;
    QString path = QFileDialog::getOpenFileName(
        nullptr, "Load NAM Model", s.value("files/lastNAM").toString(),
        "NAM Files (*.nam);;All Files (*)");
    if (path.isEmpty()) return;
    if (!mEngine->loadModel(path.toStdString()))
    {
        QMessageBox::critical(this, "Error", "Failed to load NAM model:\n" + path);
        return;
    }
    s.setValue("files/lastNAM", path);
    updateStatusLabels();
    mBtnUnloadNAM->setEnabled(true);
}

void MainWindow::onUnloadNAM()
{
    mEngine->unloadModel();
    QSettings().remove("files/lastNAM");
    updateStatusLabels();
    mBtnUnloadNAM->setEnabled(false);
}

void MainWindow::onLoadIR()
{
    QSettings s;
    QString path = QFileDialog::getOpenFileName(
        nullptr, "Load Impulse Response", s.value("files/lastIR").toString(),
        "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty()) return;
    if (!mEngine->loadIR(path.toStdString()))
    {
        QMessageBox::critical(this, "Error",
                              "Failed to load IR:\n" + path + "\n\n"
                              + QString::fromStdString(mEngine->irLoadError()));
        return;
    }
    s.setValue("files/lastIR", path);
    updateStatusLabels();
    mBtnUnloadIR->setEnabled(true);
}

void MainWindow::onUnloadIR()
{
    mEngine->unloadIR();
    QSettings().remove("files/lastIR");
    updateStatusLabels();
    mBtnUnloadIR->setEnabled(false);
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
    knobParam("chorus", "rate",      mKnobChRate,     [e](double v) { e->chorus().setRate (static_cast<float>(v)); });
    knobParam("chorus", "depth",     mKnobChDepth,    [e](double v) { e->chorus().setDepth(static_cast<float>(v)); });
    knobParam("chorus", "mix",       mKnobChMix,      [e](double v) { e->chorus().setMix  (static_cast<float>(v)); });

    toggleParam("tremolo", "enabled", mSwTremoloEnable, [e](bool b)   { e->tremolo().setEnabled(b); });
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

    mAudioBar->updateMetrics(); // PipeWire can change the quantum while running
    if (mAudioOk && !mEngine->isRunning())
    {
        mAudioOk = false;
        mAudioBar->setError("Disconnected — click to retry");
    }
}

void MainWindow::updateTuner()
{
    if (!mEngine->tuner().isEnabled()) return;

    float freq = mEngine->tuner().lastPitch();
    if (freq <= 0.0f)
    {
        mLblNote ->setText("--");
        mLblCents->setText("±0¢");
        mTunerBar->setValue(50);
        return;
    }

    int   midi  = TunerProcessor::freqToMidi(freq);
    float cents = TunerProcessor::centsOffset(freq);

    mLblNote ->setText(QString::fromStdString(TunerProcessor::midiToName(midi)));
    mLblCents->setText(QString("%1¢").arg(static_cast<int>(cents)));
    mTunerBar->setValue(std::max(0, std::min(100, static_cast<int>(50.0f + cents))));
}

// ---------------------------------------------------------------------------
// Persisted file state
// ---------------------------------------------------------------------------

void MainWindow::restoreLastFiles()
{
    QSettings s;

    QString nam = s.value("files/lastNAM").toString();
    if (!nam.isEmpty() && QFile::exists(nam) && mEngine->loadModel(nam.toStdString()))
        mBtnUnloadNAM->setEnabled(true);

    QString ir = s.value("files/lastIR").toString();
    if (!ir.isEmpty() && QFile::exists(ir) && mEngine->loadIR(ir.toStdString()))
        mBtnUnloadIR->setEnabled(true);

    updateStatusLabels();
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
// Status labels
// ---------------------------------------------------------------------------

void MainWindow::updateStatusLabels()
{
    auto setStatus = [](QLabel* lbl, const QString& text, bool loaded) {
        lbl->setText(text);
        lbl->setObjectName(loaded ? "fileStatusOk" : "fileStatus");
        lbl->style()->unpolish(lbl);
        lbl->style()->polish(lbl);
    };

    if (mEngine->isModelLoaded())
        setStatus(mLblNAMStatus, QString::fromStdString(mEngine->modelPath()).section('/', -1), true);
    else
        setStatus(mLblNAMStatus, "No model loaded", false);

    if (mEngine->isIRLoaded())
        setStatus(mLblIRStatus, QString::fromStdString(mEngine->irPath()).section('/', -1), true);
    else
        setStatus(mLblIRStatus, "No impulse response loaded", false);
}

// ---------------------------------------------------------------------------
// Preset save / load
// ---------------------------------------------------------------------------

void MainWindow::onSavePreset()
{
    QSettings settings;
    QString path = QFileDialog::getSaveFileName(
        nullptr, "Save Preset", settings.value("files/lastPreset").toString(),
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    settings.setValue("files/lastPreset", path);

    const QJsonObject obj = PresetSerializer::toJson(mParams, *mEngine);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "Error", "Cannot write preset:\n" + path);
        return;
    }
    f.write(QJsonDocument(obj).toJson());
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
    QSettings settings;
    QString path = QFileDialog::getOpenFileName(
        nullptr, "Load Preset", settings.value("files/lastPreset").toString(),
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    settings.setValue("files/lastPreset", path);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, "Error", "Cannot read preset:\n" + path);
        return;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull())
    {
        QMessageBox::critical(this, "Error", "Invalid preset:\n" + err.errorString());
        return;
    }

    // Widgets first (engine follows via the registry connections), then the
    // chain order (engine-side sanitizer handles unknown/missing IDs).
    const auto applied = PresetSerializer::apply(doc.object(), mParams);
    if (!applied.effectOrder.isEmpty())
        applyChainOrder(applied.effectOrder);

    // v2 presets embed the model/IR paths. Only a non-empty path acts — a
    // preset saved without a model must not unload the current one — and when
    // the referenced file is missing or fails to load, the current one stays.
    QStringList failed;
    if (!applied.namPath.isEmpty())
    {
        if (QFile::exists(applied.namPath) && mEngine->loadModel(applied.namPath.toStdString())) {
            settings.setValue("files/lastNAM", applied.namPath);
            mBtnUnloadNAM->setEnabled(true);
        } else {
            failed << applied.namPath;
        }
    }
    if (!applied.irPath.isEmpty())
    {
        if (QFile::exists(applied.irPath) && mEngine->loadIR(applied.irPath.toStdString())) {
            settings.setValue("files/lastIR", applied.irPath);
            mBtnUnloadIR->setEnabled(true);
        } else {
            failed << applied.irPath;
        }
    }
    updateStatusLabels();
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Preset",
                             "The preset references files that could not be loaded "
                             "(the current model/IR was kept):\n" + failed.join('\n'));
}

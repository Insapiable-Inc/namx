#include "MainWindow.h"
#include "Constants.h"
#include "EffectShortcuts.h"
#include "Theme.h"
#include "TunerProcessor.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
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
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Mime type carrying the effect ID of a dragged strip card.
static const char kEffectMime[] = "application/x-nam-effect-id";

// Fixed height of the "stereo slot" row that sits between the enable toggle
// and the first knob. Reserved on every card with knobs (visible toggle on
// stereo-capable effects, otherwise an empty placeholder) so that knob row 1
// starts at the same Y across all columns.
static constexpr int kStereoSlotHeight = 38;

static QWidget* makeStereoSlot(ToggleSwitch** outToggle)
{
    auto* w = new QWidget;
    w->setFixedHeight(kStereoSlotHeight);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(1);

    if (outToggle) {
        auto* sw = new ToggleSwitch;
        sw->setCheckable(true);
        sw->setChecked(false);
        auto* lbl = new QLabel("Stereo");
        lbl->setObjectName("stereoLabel");
        lbl->setAlignment(Qt::AlignHCenter);
        lay->addWidget(sw, 0, Qt::AlignHCenter);
        lay->addWidget(lbl);
        lay->addStretch();
        *outToggle = sw;
    }

    return w;
}

// The title row (top margin band) of an effect card. Movable cards carry a
// "≡" grip glyph in the title and dragging is reserved for this band, so
// knob/toggle clicks can never start a reorder.
static QRect titleBandRect(const QGroupBox* box)
{
    QStyleOptionGroupBox opt;
    opt.initFrom(box);
    opt.text          = box->title();
    opt.textAlignment = box->alignment();
    opt.subControls   = QStyle::SC_GroupBoxLabel | QStyle::SC_GroupBoxFrame;
    opt.lineWidth     = 1;
    QRect r = box->style()->subControlRect(QStyle::CC_GroupBox, &opt,
                                           QStyle::SC_GroupBoxLabel, box);
    if (!r.isValid() || r.height() <= 0)
        r = QRect(0, 0, box->width(), QFontMetrics(box->font()).height() + 8);
    // Full card width at the title's vertical extent — the whole top band drags.
    return QRect(0, 0, box->width(), r.bottom() + 4);
}

static QProgressBar* makeVUBarVertical()
{
    auto* b = new QProgressBar;
    b->setObjectName("vuMeter"); // chunk gradient lives in the Theme stylesheet
    b->setOrientation(Qt::Vertical);
    b->setRange(0, 100);
    b->setValue(0);
    b->setTextVisible(false);
    b->setFixedWidth(14);
    b->setMinimumHeight(70);
    return b;
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

QWidget* MainWindow::makeEffectColumn(const QString& title,
                                      ToggleSwitch*& toggleOut,
                                      const std::initializer_list<KnobWidget*>& knobs,
                                      ToggleSwitch** stereoToggleOut)
{
    // The ≡ grip in the title marks a draggable card; dragging starts from
    // the title band only.
    auto* col = new QGroupBox("≡  " + title);
    col->setAlignment(Qt::AlignHCenter);
    col->setMinimumWidth(110);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(4, 6, 4, 4);
    lay->setSpacing(4);

    toggleOut = new ToggleSwitch;
    toggleOut->setCheckable(true);
    toggleOut->setChecked(false);
    lay->addWidget(toggleOut, 0, Qt::AlignHCenter);

    // Reserve the stereo-slot row even when this effect has no stereo toggle —
    // keeps knob row 1 at the same Y across every card with knobs.
    lay->addWidget(makeStereoSlot(stereoToggleOut));

    for (auto* k : knobs)
        lay->addWidget(k);

    lay->addStretch();
    return col;
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

    // ---- Top status row ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(10);

        auto* title = new QLabel("NAM Player");
        title->setObjectName("appTitle");

        mLblAudioStatus = new QLabel("Audio: connecting…");
        mLblAudioStatus->setObjectName("audioStatusErr");

        mBtnReconnect = new QPushButton("Reconnect");
        mBtnReconnect->setVisible(false);
        connect(mBtnReconnect, &QPushButton::clicked,
                this, &MainWindow::restartAudioWithCurrentDevices);

        auto* btnSave  = new QPushButton("Save Preset…");
        auto* btnLoad  = new QPushButton("Load Preset…");
        auto* btnReset = new QPushButton("Reset All");
        connect(btnSave,  &QPushButton::clicked, this, &MainWindow::onSavePreset);
        connect(btnLoad,  &QPushButton::clicked, this, &MainWindow::onLoadPreset);
        connect(btnReset, &QPushButton::clicked, this, &MainWindow::onResetAll);

        row->addWidget(title);
        row->addWidget(mLblAudioStatus);
        row->addWidget(mBtnReconnect);
        row->addStretch();
        row->addWidget(btnSave);
        row->addWidget(btnLoad);
        row->addWidget(btnReset);
        rootLayout->addLayout(row);
    }

    // ---- Device selection row ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 0, 4, 4);
        row->setSpacing(8);

        auto* inputLabel = new QLabel("Input:");
        inputLabel->setObjectName("deviceLabel");
        mCbxInputDevice = new QComboBox;
        mCbxInputDevice->setMinimumWidth(180);
        mCbxInputDevice->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        auto* outputLabel = new QLabel("Output:");
        outputLabel->setObjectName("deviceLabel");
        mCbxOutputDevice = new QComboBox;
        mCbxOutputDevice->setMinimumWidth(180);
        mCbxOutputDevice->setSizeAdjustPolicy(QComboBox::AdjustToContents);

        row->addWidget(inputLabel);
        row->addWidget(mCbxInputDevice, 1);
        row->addSpacing(16);
        row->addWidget(outputLabel);
        row->addWidget(mCbxOutputDevice, 1);

        rootLayout->addLayout(row);

        connect(mCbxInputDevice, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onInputDeviceChanged);
        connect(mCbxOutputDevice, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &MainWindow::onOutputDeviceChanged);
    }

    // ---- NAM + IR file row ----
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);

        auto* namBox = new QGroupBox("NAM Model");
        Theme::tagCard(namBox, "nam");
        auto* namLay = new QHBoxLayout(namBox);
        namLay->setContentsMargins(8, 16, 8, 8);
        namLay->setSpacing(6);
        mBtnLoadNAM   = new QPushButton("Load…");
        mBtnUnloadNAM = new QPushButton("Unload");
        mBtnUnloadNAM->setEnabled(false);
        mLblNAMStatus = new QLabel("No model loaded");
        mLblNAMStatus->setObjectName("fileStatus");
        connect(mBtnLoadNAM,   &QPushButton::clicked, this, &MainWindow::onLoadNAM);
        connect(mBtnUnloadNAM, &QPushButton::clicked, this, &MainWindow::onUnloadNAM);
        namLay->addWidget(mBtnLoadNAM);
        namLay->addWidget(mBtnUnloadNAM);
        namLay->addWidget(mLblNAMStatus, 1);

        auto* irBox = new QGroupBox("Impulse Response (Cab)");
        Theme::tagCard(irBox, "ir");
        auto* irLay = new QHBoxLayout(irBox);
        irLay->setContentsMargins(8, 16, 8, 8);
        irLay->setSpacing(6);
        mBtnLoadIR   = new QPushButton("Load…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mLblIRStatus = new QLabel("No IR loaded");
        mLblIRStatus->setObjectName("fileStatus");
        connect(mBtnLoadIR,   &QPushButton::clicked, this, &MainWindow::onLoadIR);
        connect(mBtnUnloadIR, &QPushButton::clicked, this, &MainWindow::onUnloadIR);
        irLay->addWidget(mBtnLoadIR);
        irLay->addWidget(mBtnUnloadIR);
        irLay->addWidget(mLblIRStatus, 1);

        row->addWidget(namBox, 1);
        row->addWidget(irBox, 1);
        rootLayout->addLayout(row);
    }

    // ---- Scrollable effect strip ----
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);

    mStrip    = new QWidget;
    mStripLay = new QHBoxLayout(mStrip);
    mStripLay->setContentsMargins(2, 2, 2, 2);
    mStripLay->setSpacing(4);
    mStripLay->setAlignment(Qt::AlignTop);

    // Effect columns are created once here, keyed by EffectBase*. They are
    // NOT added to the layout — rebuildEffectStrip lays them out in engine
    // chain order, so audio order and UI order stay in sync automatically.

    // ---- Tuner ----
    {
        auto* col = new QGroupBox("≡  Tuner");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(3);

        mSwTunerEnable = new ToggleSwitch;
        mSwTunerEnable->setCheckable(true);
        lay->addWidget(mSwTunerEnable, 0, Qt::AlignHCenter);

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
        mLblCents->setMinimumWidth(36);
        mLblCents->setAlignment(Qt::AlignCenter);

        mTunerBar = new QProgressBar;
        mTunerBar->setObjectName("tunerBar"); // chunk color lives in the Theme stylesheet
        mTunerBar->setOrientation(Qt::Vertical);
        mTunerBar->setRange(0, 100);
        mTunerBar->setValue(50);
        mTunerBar->setTextVisible(false);
        mTunerBar->setFixedWidth(14);
        mTunerBar->setMinimumHeight(60);

        mSwTunerMute = new ToggleSwitch;
        mSwTunerMute->setCheckable(true);
        auto* muteLabel = new QLabel("Mute");
        muteLabel->setAlignment(Qt::AlignHCenter);

        lay->addWidget(mLblNote);
        lay->addWidget(mLblCents);
        lay->addWidget(mTunerBar, 0, Qt::AlignHCenter);
        lay->addWidget(mSwTunerMute, 0, Qt::AlignHCenter);
        lay->addWidget(muteLabel);
        lay->addStretch();

        Theme::tagCard(col, "tuner"); // after children — pushes accents to them
        mFxCols[&mEngine->tuner()] = col;
    }

    // ---- Noise Gate ----
    {
        using namespace NAMConstants;
        mKnobGateThresh  = new KnobWidget("Thresh",  kGateThreshMin,  kGateThreshMax,  kGateThreshDefault,  "dB");
        mKnobGateAttack  = new KnobWidget("Attack",  kGateAttackMin,  kGateAttackMax,  kGateAttackDefault,  "ms");
        mKnobGateRelease = new KnobWidget("Release", kGateReleaseMin, kGateReleaseMax, kGateReleaseDefault, "ms");
        mKnobGateThresh->setScrollStep(1.0);
        mKnobGateAttack->setScrollStep(1.0);
        mKnobGateRelease->setScrollStep(10.0);

        auto* col = makeEffectColumn("Gate", mSwGateEnable,
            {mKnobGateThresh, mKnobGateAttack, mKnobGateRelease});
        Theme::tagCard(col, "gate");

        mFxCols[&mEngine->gate()] = col;
    }

    // ---- Compressor ----
    {
        using namespace NAMConstants;
        mKnobCompThresh  = new KnobWidget("Thresh",  kCompThreshMin,  kCompThreshMax,  kCompThreshDefault,  "dB");
        mKnobCompRatio   = new KnobWidget("Ratio",   kCompRatioMin,   kCompRatioMax,   kCompRatioDefault,   ":1");
        mKnobCompAttack  = new KnobWidget("Attack",  kCompAttackMin,  kCompAttackMax,  kCompAttackDefault,  "ms");
        mKnobCompRelease = new KnobWidget("Release", kCompReleaseMin, kCompReleaseMax, kCompReleaseDefault, "ms");
        mKnobCompMakeup  = new KnobWidget("Makeup",  kCompMakeupMin,  kCompMakeupMax,  kCompMakeupDefault,  "dB");
        mKnobCompThresh->setScrollStep(1.0);
        mKnobCompRatio->setScrollStep(0.5);
        mKnobCompAttack->setScrollStep(1.0);
        mKnobCompRelease->setScrollStep(10.0);
        mKnobCompMakeup->setScrollStep(0.5);

        auto* col = makeEffectColumn("Compressor", mSwCompEnable,
            {mKnobCompThresh, mKnobCompRatio, mKnobCompAttack, mKnobCompRelease, mKnobCompMakeup});
        Theme::tagCard(col, "comp");

        mFxCols[&mEngine->comp()] = col;
    }

    // ---- Dynamic Wah ----
    {
        using namespace NAMConstants;
        mKnobWahSens = new KnobWidget("Sens", kWahSensMin, kWahSensMax, kWahSensDefault, "%");
        mKnobWahRes  = new KnobWidget("Res",  kWahResMin,  kWahResMax,  kWahResDefault,  "%");
        mKnobWahMix  = new KnobWidget("Mix",  kWahMixMin,  kWahMixMax,  kWahMixDefault,  "%");
        mKnobWahSens->setScrollStep(1.0);
        mKnobWahRes->setScrollStep(1.0);
        mKnobWahMix->setScrollStep(1.0);

        auto* col = makeEffectColumn("Dyn.Wah", mSwWahEnable,
            {mKnobWahSens, mKnobWahRes, mKnobWahMix});
        Theme::tagCard(col, "wah");

        mFxCols[&mEngine->wah()] = col;
    }

    // ---- Amp (NAM) ----
    {
        auto* col = makeEffectColumn("Amp", mSwNAMEnable, {});
        mSwNAMEnable->setChecked(true);
        Theme::tagCard(col, "nam");
        mFxCols[&mEngine->nam()] = col;
    }

    // ---- EQ ----
    {
        using namespace NAMConstants;
        mKnobLow    = new KnobWidget("Low",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobMid    = new KnobWidget("Mid",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobTreble = new KnobWidget("Treble", kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobLow->setScrollStep(0.1);
        mKnobMid->setScrollStep(0.1);
        mKnobTreble->setScrollStep(0.1);

        auto* col = makeEffectColumn("EQ", mSwEQEnable, {mKnobLow, mKnobMid, mKnobTreble});
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

        auto* col = makeEffectColumn("Flanger", mSwFlangerEnable,
            {mKnobFlRate, mKnobFlDepth, mKnobFlFeedback, mKnobFlMix}, &mSwFlangerStereo);
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

        auto* col = makeEffectColumn("Phaser", mSwPhaserEnable,
            {mKnobPhRate, mKnobPhDepth, mKnobPhFeedback, mKnobPhMix}, &mSwPhaserStereo);
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

        auto* col = makeEffectColumn("Chorus", mSwChorusEnable,
            {mKnobChRate, mKnobChDepth, mKnobChMix});
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

        auto* col = makeEffectColumn("Tremolo", mSwTremoloEnable, {mKnobTrRate, mKnobTrDepth});
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

        auto* col = makeEffectColumn("Delay", mSwDelayEnable,
            {mKnobDelTime, mKnobDelFeedback, mKnobDelMix}, &mSwDelayStereo);
        mSwDelayStereo->setChecked(true);
        Theme::tagCard(col, "delay");

        mFxCols[&mEngine->delay()] = col;
    }

    // ---- Reverb ----
    {
        using namespace NAMConstants;
        mKnobRevRoom    = new KnobWidget("Room",    kReverbRoomMin,    kReverbRoomMax,    kReverbRoomDefault,    "%");
        mKnobRevDamping = new KnobWidget("Damping", kReverbDampingMin, kReverbDampingMax, kReverbDampingDefault, "%");
        mKnobRevWidth   = new KnobWidget("Width",   kReverbWidthMin,   kReverbWidthMax,   kReverbWidthDefault,   "%");
        mKnobRevMix     = new KnobWidget("Mix",     kReverbMixMin,     kReverbMixMax,     kReverbMixDefault,     "%");
        mKnobRevRoom->setScrollStep(1.0);
        mKnobRevDamping->setScrollStep(1.0);
        mKnobRevWidth->setScrollStep(1.0);
        mKnobRevMix->setScrollStep(1.0);

        auto* col = makeEffectColumn("Reverb", mSwReverbEnable,
            {mKnobRevRoom, mKnobRevDamping, mKnobRevWidth, mKnobRevMix});
        Theme::tagCard(col, "reverb");

        mFxCols[&mEngine->reverb()] = col;
    }

    // ---- IR toggle ----
    {
        auto* col = makeEffectColumn("IR", mSwIREnable, {});
        mSwIREnable->setChecked(true);
        Theme::tagCard(col, "ir");
        mFxCols[&mEngine->ir()] = col;
    }

    // Input/Output gain cards (not in the chain — pinned first and last)
    auto makeGainCard = [&](const QString& title, KnobWidget*& knob, QProgressBar*& meter) -> QWidget* {
        using namespace NAMConstants;
        auto* col = new QGroupBox(title);
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        knob = new KnobWidget("Gain", kGainMin, kGainMax, kGainDefault, "dB");
        knob->setScrollStep(0.1);
        meter = makeVUBarVertical();
        lay->addWidget(knob);
        lay->addWidget(meter, 0, Qt::AlignHCenter);
        lay->addStretch();
        Theme::tagCard(col, "neutral"); // after children — pushes accents to them
        return col;
    };

    mInputCard  = makeGainCard("Input",  mKnobInputGain,  mMeterIn);
    mOutputCard = makeGainCard("Output", mKnobOutputGain, mMeterOut);

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

    // Drag sources + context menus: every card. Drop target: the strip itself.
    for (const auto& [fx, card] : mFxCols)
        card->installEventFilter(this);
    mInputCard->installEventFilter(this);   // context menu only — not draggable
    mOutputCard->installEventFilter(this);
    mStrip->setAcceptDrops(true);
    mStrip->installEventFilter(this);

    rebuildEffectStrip();

    scroll->setWidget(mStrip);
    rootLayout->addWidget(scroll, 1);
}

void MainWindow::layoutStrip(const std::vector<EffectBase*>& order)
{
    // Empty the layout: dividers and the stretch are recreated below, cards
    // are reused (they stay children of the strip while out of the layout).
    while (QLayoutItem* item = mStripLay->takeAt(0))
    {
        if (QWidget* w = item->widget(); w && w->objectName() == "stripDivider")
            w->deleteLater();
        delete item;
    }

    auto addDivider = [this] {
        auto* f = new QFrame;
        f->setObjectName("stripDivider");
        f->setFrameShape(QFrame::VLine);
        f->setFrameShadow(QFrame::Sunken);
        mStripLay->addWidget(f);
    };

    // Card order drives the section dividers too — they follow NAM/EQ
    // wherever they currently sit (also during a drag preview).
    mStripLay->addWidget(mInputCard);
    addDivider();
    for (auto* fx : order)
    {
        if (fx == &mEngine->nam())
            addDivider();

        auto it = mFxCols.find(fx);
        if (it != mFxCols.end())
            mStripLay->addWidget(it->second);

        if (fx == &mEngine->eq())
            addDivider();
    }
    addDivider();
    mStripLay->addWidget(mOutputCard);
    mStripLay->addStretch();
    mStripLay->activate(); // fresh geometry now — the drag preview reads card centres
}

void MainWindow::rebuildEffectStrip()
{
    layoutStrip(mEngine->chain());

    // Digit/QWE shortcuts always match the visual left-to-right order.
    mShortcuts.clear();
    for (auto* fx : mEngine->chain())
    {
        auto tg = mFxToggles.find(fx);
        if (tg != mFxToggles.end())
            mShortcuts.addEffect(tg->second.enable, tg->second.stereo);
    }
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
    card->setGraphicsEffect(nullptr);
    mDragId.clear();
    rebuildEffectStrip();
}

void MainWindow::updateDragPreview(const QPoint& pos)
{
    if (mDragId.empty()) return;

    std::vector<std::string> ids = mPreviewIds;
    ids.erase(std::remove(ids.begin(), ids.end(), mDragId), ids.end());
    if (ids.empty()) return;

    // Insert after every card whose centre lies left of the cursor.
    int insert = 0;
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto it = mFxCols.find(mEngine->effectById(ids[i]));
        if (it != mFxCols.end() && pos.x() > it->second->geometry().center().x())
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
        auto* me  = static_cast<QMouseEvent*>(ev);
        auto* box = qobject_cast<QGroupBox*>(obj);
        if (box && obj != mStrip && me->button() == Qt::LeftButton
            && titleBandRect(box).contains(me->pos()))
        {
            mDragCard     = box;
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
        auto* box = qobject_cast<QGroupBox*>(obj);
        if (box && obj != mStrip)
        {
            auto* ce = static_cast<QContextMenuEvent*>(ev);
            QString name = box->title();
            name.remove("≡  ");
            QMenu menu(box);
            QAction* act = menu.addAction("Reset " + name + " to defaults");
            if (menu.exec(ce->globalPos()) == act)
                resetCardToDefaults(box);
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

void MainWindow::setAudioStatus(const QString& text, bool ok)
{
    mLblAudioStatus->setText(text);
    mLblAudioStatus->setObjectName(ok ? "audioStatusOk" : "audioStatusErr");
    mLblAudioStatus->style()->unpolish(mLblAudioStatus);
    mLblAudioStatus->style()->polish(mLblAudioStatus);
    mBtnReconnect->setVisible(!ok);
}

void MainWindow::setupAudio()
{
    populateDeviceDropdowns();

    QSettings s;
    const QString savedIn  = s.value("audio/inputDeviceId").toString();
    const QString savedOut = s.value("audio/outputDeviceId").toString();

    bool started = false;
    if (!savedIn.isEmpty() || !savedOut.isEmpty())
        started = mEngine->startWithDevices(savedIn.toStdString(), savedOut.toStdString());
    if (!started)
        started = mEngine->start("NAMPlayer");

    if (started) {
        setAudioStatus(QString::fromStdString(mEngine->backendStatus()), true);
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        setAudioStatus("Audio backend  \xe2\x97\x8f  not connected", false);
    }
}

void MainWindow::populateDeviceDropdowns()
{
    mCbxInputDevice->blockSignals(true);
    mCbxOutputDevice->blockSignals(true);

    mCbxInputDevice->clear();
    mCbxOutputDevice->clear();

    const auto inputs  = mEngine->enumerateInputDevices();
    const auto outputs = mEngine->enumerateOutputDevices();

    // If only "Default" is available (JACK or single entry), disable combos.
    const bool onlyDefault = (inputs.size() <= 1 && outputs.size() <= 1);

    for (const auto& d : inputs)
        mCbxInputDevice->addItem(QString::fromStdString(d.name),
                                  QString::fromStdString(d.id));
    for (const auto& d : outputs)
        mCbxOutputDevice->addItem(QString::fromStdString(d.name),
                                   QString::fromStdString(d.id));

    mCbxInputDevice->setEnabled(!onlyDefault);
    mCbxOutputDevice->setEnabled(!onlyDefault);

    // Restore saved selections
    QSettings s;
    const int idxIn = mCbxInputDevice->findData(s.value("audio/inputDeviceId").toString());
    if (idxIn >= 0) mCbxInputDevice->setCurrentIndex(idxIn);

    const int idxOut = mCbxOutputDevice->findData(s.value("audio/outputDeviceId").toString());
    if (idxOut >= 0) mCbxOutputDevice->setCurrentIndex(idxOut);

    mCbxInputDevice->blockSignals(false);
    mCbxOutputDevice->blockSignals(false);
}

void MainWindow::onInputDeviceChanged(int)
{
    if (mDeviceChanging) return;
    mDeviceChanging = true;
    QSettings s;
    s.setValue("audio/inputDeviceId", mCbxInputDevice->currentData().toString());
    restartAudioWithCurrentDevices();
    mDeviceChanging = false;
}

void MainWindow::onOutputDeviceChanged(int)
{
    if (mDeviceChanging) return;
    mDeviceChanging = true;
    QSettings s;
    s.setValue("audio/outputDeviceId", mCbxOutputDevice->currentData().toString());
    restartAudioWithCurrentDevices();
    mDeviceChanging = false;
}

void MainWindow::restartAudioWithCurrentDevices()
{
    setAudioStatus("Audio backend  \xe2\x97\x8f  switching devices…", false);

    const QString inId  = mCbxInputDevice->currentData().toString();
    const QString outId = mCbxOutputDevice->currentData().toString();

    if (mEngine->startWithDevices(inId.toStdString(), outId.toStdString())) {
        setAudioStatus(QString::fromStdString(mEngine->backendStatus()), true);
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        setAudioStatus("Audio backend  \xe2\x97\x8f  not connected", false);
    }
}

// ---------------------------------------------------------------------------
// File slots
// ---------------------------------------------------------------------------

void MainWindow::onLoadNAM()
{
    QSettings s;
    QString path = QFileDialog::getOpenFileName(
        this, "Load NAM Model", s.value("files/lastNAM").toString(),
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
        this, "Load Impulse Response", s.value("files/lastIR").toString(),
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
    toggleParam("tuner", "mute",    mSwTunerMute,   [e](bool b) { e->tuner().setMute(b);    });

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
    auto toMeter = [](float peak) -> int {
        if (peak <= 0.0f) return 0;
        float dB = 20.0f * std::log10(peak);
        return std::max(0, std::min(100, static_cast<int>((dB + 60.0f) / 60.0f * 100.0f)));
    };
    mMeterIn ->setValue(toMeter(mEngine->takeInputPeak()));
    mMeterOut->setValue(toMeter(mEngine->takeOutputPeak()));

    if (!mEngine->isRunning() && mLblAudioStatus->objectName() == "audioStatusOk")
        setAudioStatus("Audio backend  ●  disconnected", false);
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
        setStatus(mLblIRStatus, "No IR loaded", false);
}

// ---------------------------------------------------------------------------
// Preset save / load
// ---------------------------------------------------------------------------

void MainWindow::onSavePreset()
{
    QSettings settings;
    QString path = QFileDialog::getSaveFileName(
        this, "Save Preset", settings.value("files/lastPreset").toString(),
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    settings.setValue("files/lastPreset", path);

    // Widget values mirror the engine (every change flows through the
    // registry connections), so the widgets are the single source here.
    QJsonObject obj;
    obj["version"] = 2;
    obj["namPath"] = QString::fromStdString(mEngine->modelPath());
    obj["irPath"]  = QString::fromStdString(mEngine->irPath());
    QJsonArray orderArr;
    for (const auto& id : mEngine->chainIds())
        orderArr.append(QString::fromStdString(id));
    obj["effectOrder"] = orderArr;
    for (const auto& p : mParams)
    {
        const QJsonValue v = p.knob ? QJsonValue(p.knob->value())
                                    : QJsonValue(p.toggle->isChecked());
        if (p.group.isEmpty()) {
            obj[p.key] = v;
        } else {
            QJsonObject g = obj[p.group].toObject();
            g[p.key] = v;
            obj[p.group] = g;
        }
    }

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
        this, "Load Preset", settings.value("files/lastPreset").toString(),
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

    const QJsonObject obj = doc.object();

    // Apply through the widgets so UI and engine stay in sync (widget signals
    // reach the engine via the registry connections). A group absent from the
    // file leaves its effect untouched; inside a present group — and at the
    // root — missing keys fall back to the registered defaults.
    for (const auto& p : mParams)
    {
        QJsonObject src = obj;
        if (!p.group.isEmpty())
        {
            if (!obj.contains(p.group)) continue;
            src = obj[p.group].toObject();
        }
        if (p.knob)
            p.knob->setValue(src.contains(p.key) ? src[p.key].toDouble(p.knobDefault)
                                                 : p.knobDefault);
        else
            p.toggle->setChecked(src.contains(p.key) ? src[p.key].toBool(p.toggleDefault)
                                                     : p.toggleDefault);
    }

    // v2 presets carry the chain order; unknown/missing IDs are handled by
    // the engine-side sanitizer (missing effects keep their default slot).
    if (obj.contains("effectOrder"))
    {
        QStringList order;
        for (const auto& v : obj["effectOrder"].toArray())
            order << v.toString();
        applyChainOrder(order);
    }

    // v2 presets embed the model/IR paths. Only a non-empty path acts — a
    // preset saved without a model must not unload the current one — and when
    // the referenced file is missing or fails to load, the current one stays.
    QStringList failed;
    const QString namPath = obj["namPath"].toString();
    if (!namPath.isEmpty())
    {
        if (QFile::exists(namPath) && mEngine->loadModel(namPath.toStdString())) {
            settings.setValue("files/lastNAM", namPath);
            mBtnUnloadNAM->setEnabled(true);
        } else {
            failed << namPath;
        }
    }
    const QString irPath = obj["irPath"].toString();
    if (!irPath.isEmpty())
    {
        if (QFile::exists(irPath) && mEngine->loadIR(irPath.toStdString())) {
            settings.setValue("files/lastIR", irPath);
            mBtnUnloadIR->setEnabled(true);
        } else {
            failed << irPath;
        }
    }
    updateStatusLabels();
    if (!failed.isEmpty())
        QMessageBox::warning(this, "Preset",
                             "The preset references files that could not be loaded "
                             "(the current model/IR was kept):\n" + failed.join('\n'));
}

#include "MainWindowAlt.h"
#include "Constants.h"
#include "TunerProcessor.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QProgressBar* makeVUBarVertical()
{
    auto* b = new QProgressBar;
    b->setOrientation(Qt::Vertical);
    b->setRange(0, 100);
    b->setValue(0);
    b->setTextVisible(false);
    b->setFixedWidth(14);
    b->setMinimumHeight(70);
    b->setStyleSheet(
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:1,x2:0,y2:0,"
        "stop:0 #2a9d2a, stop:0.7 #d4a017, stop:1 #cc2222); }");
    return b;
}

// ---------------------------------------------------------------------------

MainWindowAlt::MainWindowAlt(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player – Alt Layout");
    setMinimumSize(1400, 500);

    setupUI();
    setupAudio();

    mMeterTimer = new QTimer(this);
    connect(mMeterTimer, &QTimer::timeout, this, &MainWindowAlt::updateMeters);
    mMeterTimer->start(50);

    mTunerTimer = new QTimer(this);
    connect(mTunerTimer, &QTimer::timeout, this, &MainWindowAlt::updateTuner);
    mTunerTimer->start(100);
}

MainWindowAlt::~MainWindowAlt()
{
    mMeterTimer->stop();
    mTunerTimer->stop();
    mEngine->stop();
}

// ---------------------------------------------------------------------------
// Build a vertical effect column: title in group box, toggle, knobs as rows
// ---------------------------------------------------------------------------

QWidget* MainWindowAlt::makeEffectColumn(const QString& title,
                                          ToggleSwitch*& toggleOut,
                                          const std::initializer_list<KnobWidget*>& knobs,
                                          ToggleSwitch** stereoToggleOut)
{
    auto* col = new QGroupBox(title);
    col->setAlignment(Qt::AlignHCenter);
    col->setMinimumWidth(90);
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(4, 6, 4, 4);
    lay->setSpacing(2);

    toggleOut = new ToggleSwitch;
    toggleOut->setCheckable(true);
    toggleOut->setChecked(false);
    lay->addWidget(toggleOut, 0, Qt::AlignHCenter);

    if (stereoToggleOut) {
        auto* stereoSwitch = new ToggleSwitch;
        stereoSwitch->setCheckable(true);
        stereoSwitch->setChecked(false);
        auto* stereoLabel = new QLabel("Stereo");
        stereoLabel->setAlignment(Qt::AlignHCenter);
        lay->addWidget(stereoSwitch, 0, Qt::AlignHCenter);
        lay->addWidget(stereoLabel);
        *stereoToggleOut = stereoSwitch;
    }

    for (auto* k : knobs)
        lay->addWidget(k);

    lay->addStretch();
    return col;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void MainWindowAlt::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 4, 8, 6);
    rootLayout->setSpacing(4);

    // ---- Menu bar ----
    {
        auto* mb        = menuBar();
        auto* presetMenu = mb->addMenu("Preset");
        auto* saveAction = presetMenu->addAction("Save Preset…");
        auto* loadAction = presetMenu->addAction("Load Preset…");
        connect(saveAction, &QAction::triggered, this, &MainWindowAlt::onSavePreset);
        connect(loadAction, &QAction::triggered, this, &MainWindowAlt::onLoadPreset);
    }

    // ---- Top status row ----
    {
        auto* row = new QHBoxLayout;

        auto* title = new QLabel("NAM Player");
        QFont tf = title->font();
        tf.setPointSize(tf.pointSize() + 4);
        tf.setBold(true);
        title->setFont(tf);

        mLblJACKStatus = new QLabel("JACK: connecting…");

        auto* btnSavePreset = new QPushButton("Save Preset…");
        auto* btnLoadPreset = new QPushButton("Load Preset…");
        connect(btnSavePreset, &QPushButton::clicked, this, &MainWindowAlt::onSavePreset);
        connect(btnLoadPreset, &QPushButton::clicked, this, &MainWindowAlt::onLoadPreset);

        row->addWidget(title);
        row->addWidget(mLblJACKStatus);
        row->addStretch();
        row->addWidget(btnSavePreset);
        row->addWidget(btnLoadPreset);
        rootLayout->addLayout(row);
    }

    // ---- NAM + IR file row (above the strip) ----
    {
        auto* row = new QHBoxLayout;

        // NAM sub-box
        auto* namBox = new QGroupBox("NAM");
        auto* namLay = new QHBoxLayout(namBox);
        namLay->setContentsMargins(4, 4, 4, 4);
        mBtnLoadNAM   = new QPushButton("Load…");
        mBtnUnloadNAM = new QPushButton("Unload");
        mBtnUnloadNAM->setEnabled(false);
        mLblNAMStatus = new QLabel("No model loaded");
        connect(mBtnLoadNAM,   &QPushButton::clicked, this, &MainWindowAlt::onLoadNAM);
        connect(mBtnUnloadNAM, &QPushButton::clicked, this, &MainWindowAlt::onUnloadNAM);
        namLay->addWidget(mBtnLoadNAM);
        namLay->addWidget(mBtnUnloadNAM);
        namLay->addWidget(mLblNAMStatus, 1);

        // IR sub-box
        auto* irBox  = new QGroupBox("IR (Cab)");
        auto* irLay  = new QHBoxLayout(irBox);
        irLay->setContentsMargins(4, 4, 4, 4);
        mBtnLoadIR   = new QPushButton("Load…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mLblIRStatus = new QLabel("No IR loaded");
        connect(mBtnLoadIR,   &QPushButton::clicked, this, &MainWindowAlt::onLoadIR);
        connect(mBtnUnloadIR, &QPushButton::clicked, this, &MainWindowAlt::onUnloadIR);
        irLay->addWidget(mBtnLoadIR);
        irLay->addWidget(mBtnUnloadIR);
        irLay->addWidget(mLblIRStatus, 1);

        row->addWidget(namBox, 1);
        row->addWidget(irBox, 1);
        rootLayout->addLayout(row);
    }

    // ---- Horizontally scrollable effect strip ----
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* strip    = new QWidget;
    auto* stripLay = new QHBoxLayout(strip);
    stripLay->setContentsMargins(2, 2, 2, 2);
    stripLay->setSpacing(4);
    stripLay->setAlignment(Qt::AlignTop);

    auto addDivider = [&] {
        auto* f = new QFrame;
        f->setFrameShape(QFrame::VLine);
        f->setFrameShadow(QFrame::Sunken);
        stripLay->addWidget(f);
    };

    // ---- Input gain column ----
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("Input");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        mKnobInputGain = new KnobWidget("Gain", kGainMin, kGainMax, kGainDefault, "dB");
        connect(mKnobInputGain, &KnobWidget::valueChanged, this, &MainWindowAlt::onInputGainChanged);
        mMeterIn = makeVUBarVertical();
        lay->addWidget(mKnobInputGain);
        lay->addWidget(mMeterIn, 0, Qt::AlignHCenter);
        lay->addStretch();
        stripLay->addWidget(col);
    }

    addDivider();

    // ---- Tuner column ----
    {
        auto* col = new QGroupBox("Tuner");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(3);

        mSwTunerEnable = new ToggleSwitch;
        mSwTunerEnable->setCheckable(true);
        lay->addWidget(mSwTunerEnable, 0, Qt::AlignHCenter);

        mLblNote = new QLabel("--");
        {
            QFont f = mLblNote->font();
            f.setPointSize(f.pointSize() + 7);
            f.setBold(true);
            mLblNote->setFont(f);
        }
        mLblNote->setMinimumWidth(52);
        mLblNote->setAlignment(Qt::AlignCenter);

        mLblCents = new QLabel("±0¢");
        mLblCents->setMinimumWidth(36);
        mLblCents->setAlignment(Qt::AlignCenter);

        mTunerBar = new QProgressBar;
        mTunerBar->setOrientation(Qt::Vertical);
        mTunerBar->setRange(0, 100);
        mTunerBar->setValue(50);
        mTunerBar->setTextVisible(false);
        mTunerBar->setFixedWidth(14);
        mTunerBar->setMinimumHeight(60);
        mTunerBar->setStyleSheet("QProgressBar::chunk { background: #4caf50; }");

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

        connect(mSwTunerEnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onTunerEnabledToggled);
        connect(mSwTunerMute,   &ToggleSwitch::toggled, this, &MainWindowAlt::onTunerMuteToggled);

        stripLay->addWidget(col);
    }

    // ---- Noise Gate column ----
    {
        using namespace NAMConstants;
        mKnobGateThresh  = new KnobWidget("Thresh",  kGateThreshMin,   kGateThreshMax,   kGateThreshDefault,   "dB");
        mKnobGateAttack  = new KnobWidget("Attack",  kGateAttackMin,   kGateAttackMax,   kGateAttackDefault,   "ms");
        mKnobGateRelease = new KnobWidget("Release", kGateReleaseMin,  kGateReleaseMax,  kGateReleaseDefault,  "ms");

        auto* col = makeEffectColumn("Gate", mSwGateEnable,
            {mKnobGateThresh, mKnobGateAttack, mKnobGateRelease});

        connect(mSwGateEnable,    &ToggleSwitch::toggled,    this, &MainWindowAlt::onGateEnabledToggled);
        connect(mKnobGateThresh,  &KnobWidget::valueChanged, this, &MainWindowAlt::onGateThresholdChanged);
        connect(mKnobGateAttack,  &KnobWidget::valueChanged, this, &MainWindowAlt::onGateAttackChanged);
        connect(mKnobGateRelease, &KnobWidget::valueChanged, this, &MainWindowAlt::onGateReleaseChanged);
        stripLay->addWidget(col);
    }

    // ---- Compressor column ----
    {
        using namespace NAMConstants;
        mKnobCompThresh  = new KnobWidget("Thresh",  kCompThreshMin,   kCompThreshMax,   kCompThreshDefault,   "dB");
        mKnobCompRatio   = new KnobWidget("Ratio",   kCompRatioMin,    kCompRatioMax,    kCompRatioDefault,    ":1");
        mKnobCompAttack  = new KnobWidget("Attack",  kCompAttackMin,   kCompAttackMax,   kCompAttackDefault,   "ms");
        mKnobCompRelease = new KnobWidget("Release", kCompReleaseMin,  kCompReleaseMax,  kCompReleaseDefault,  "ms");
        mKnobCompMakeup  = new KnobWidget("Makeup",  kCompMakeupMin,   kCompMakeupMax,   kCompMakeupDefault,   "dB");

        auto* col = makeEffectColumn("Compressor", mSwCompEnable,
            {mKnobCompThresh, mKnobCompRatio, mKnobCompAttack, mKnobCompRelease, mKnobCompMakeup});

        connect(mSwCompEnable,    &ToggleSwitch::toggled,    this, &MainWindowAlt::onCompEnabledToggled);
        connect(mKnobCompThresh,  &KnobWidget::valueChanged, this, &MainWindowAlt::onCompThresholdChanged);
        connect(mKnobCompRatio,   &KnobWidget::valueChanged, this, &MainWindowAlt::onCompRatioChanged);
        connect(mKnobCompAttack,  &KnobWidget::valueChanged, this, &MainWindowAlt::onCompAttackChanged);
        connect(mKnobCompRelease, &KnobWidget::valueChanged, this, &MainWindowAlt::onCompReleaseChanged);
        connect(mKnobCompMakeup,  &KnobWidget::valueChanged, this, &MainWindowAlt::onCompMakeupChanged);
        stripLay->addWidget(col);
    }

    // ---- Pitch Shift column ----
    {
        using namespace NAMConstants;
        mKnobPitchSemi = new KnobWidget("Semitones", kPitchSemiMin, kPitchSemiMax, kPitchSemiDefault, "st");
        mKnobPitchSemi->setIntegerDisplay(true);
        mKnobPitchMix  = new KnobWidget("Mix", kPitchMixMin, kPitchMixMax, kPitchMixDefault, "%");

        auto* col = makeEffectColumn("Pitch", mSwPitchEnable, {mKnobPitchSemi, mKnobPitchMix});

        connect(mSwPitchEnable, &ToggleSwitch::toggled,    this, &MainWindowAlt::onPitchEnabledToggled);
        connect(mKnobPitchSemi, &KnobWidget::valueChanged, this, &MainWindowAlt::onPitchSemitonesChanged);
        connect(mKnobPitchMix,  &KnobWidget::valueChanged, this, &MainWindowAlt::onPitchMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Chorus 1 column ----
    {
        using namespace NAMConstants;
        mKnobCh1Rate  = new KnobWidget("Rate",  kChorusRateMin,  kChorusRateMax,  kChorusRateDefault,  "Hz");
        mKnobCh1Depth = new KnobWidget("Depth", kChorusDepthMin, kChorusDepthMax, kChorusDepthDefault, "%");
        mKnobCh1Mix   = new KnobWidget("Mix",   kChorusMixMin,   kChorusMixMax,   kChorusMixDefault,   "%");

        auto* col = makeEffectColumn("Chorus 1", mSwChorus1Enable,
            {mKnobCh1Rate, mKnobCh1Depth, mKnobCh1Mix}, &mSwChorus1Stereo);

        connect(mSwChorus1Enable,  &ToggleSwitch::toggled, this, &MainWindowAlt::onChorus1EnabledToggled);
        connect(mSwChorus1Stereo,  &ToggleSwitch::toggled, this, &MainWindowAlt::onChorus1StereoToggled);
        connect(mKnobCh1Rate,     &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus1RateChanged);
        connect(mKnobCh1Depth,    &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus1DepthChanged);
        connect(mKnobCh1Mix,      &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus1MixChanged);
        stripLay->addWidget(col);
    }

    // ---- Flanger column ----
    {
        using namespace NAMConstants;
        mKnobFlRate     = new KnobWidget("Rate",     kFlangerRateMin,     kFlangerRateMax,     kFlangerRateDefault,     "Hz");
        mKnobFlDepth    = new KnobWidget("Depth",    kFlangerDepthMin,    kFlangerDepthMax,    kFlangerDepthDefault,    "%");
        mKnobFlFeedback = new KnobWidget("Feedback", kFlangerFeedbackMin, kFlangerFeedbackMax, kFlangerFeedbackDefault, "%");
        mKnobFlMix      = new KnobWidget("Mix",      kFlangerMixMin,      kFlangerMixMax,      kFlangerMixDefault,      "%");

        auto* col = makeEffectColumn("Flanger", mSwFlangerEnable,
            {mKnobFlRate, mKnobFlDepth, mKnobFlFeedback, mKnobFlMix}, &mSwFlangerStereo);

        connect(mSwFlangerEnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onFlangerEnabledToggled);
        connect(mSwFlangerStereo, &ToggleSwitch::toggled, this, &MainWindowAlt::onFlangerStereoToggled);
        connect(mKnobFlRate,      &KnobWidget::valueChanged, this, &MainWindowAlt::onFlangerRateChanged);
        connect(mKnobFlDepth,     &KnobWidget::valueChanged, this, &MainWindowAlt::onFlangerDepthChanged);
        connect(mKnobFlFeedback,  &KnobWidget::valueChanged, this, &MainWindowAlt::onFlangerFeedbackChanged);
        connect(mKnobFlMix,       &KnobWidget::valueChanged, this, &MainWindowAlt::onFlangerMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Phaser column ----
    {
        using namespace NAMConstants;
        mKnobPhRate     = new KnobWidget("Rate",     kPhaserRateMin,     kPhaserRateMax,     kPhaserRateDefault,     "Hz");
        mKnobPhDepth    = new KnobWidget("Depth",    kPhaserDepthMin,    kPhaserDepthMax,    kPhaserDepthDefault,    "%");
        mKnobPhFeedback = new KnobWidget("Feedback", kPhaserFeedbackMin, kPhaserFeedbackMax, kPhaserFeedbackDefault, "%");
        mKnobPhMix      = new KnobWidget("Mix",      kPhaserMixMin,      kPhaserMixMax,      kPhaserMixDefault,      "%");

        auto* col = makeEffectColumn("Phaser", mSwPhaserEnable,
            {mKnobPhRate, mKnobPhDepth, mKnobPhFeedback, mKnobPhMix}, &mSwPhaserStereo);

        connect(mSwPhaserEnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onPhaserEnabledToggled);
        connect(mSwPhaserStereo, &ToggleSwitch::toggled, this, &MainWindowAlt::onPhaserStereoToggled);
        connect(mKnobPhRate,     &KnobWidget::valueChanged, this, &MainWindowAlt::onPhaserRateChanged);
        connect(mKnobPhDepth,    &KnobWidget::valueChanged, this, &MainWindowAlt::onPhaserDepthChanged);
        connect(mKnobPhFeedback, &KnobWidget::valueChanged, this, &MainWindowAlt::onPhaserFeedbackChanged);
        connect(mKnobPhMix,      &KnobWidget::valueChanged, this, &MainWindowAlt::onPhaserMixChanged);
        stripLay->addWidget(col);
    }

    addDivider();

    // ---- Amp toggle column ----
    {
        auto* col = new QGroupBox("Amp");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(4);

        mSwNAMEnable = new ToggleSwitch;
        mSwNAMEnable->setCheckable(true);
        mSwNAMEnable->setChecked(true);
        lay->addWidget(mSwNAMEnable, 0, Qt::AlignHCenter);
        lay->addStretch();

        connect(mSwNAMEnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onNAMEnabledToggled);
        stripLay->addWidget(col);
    }

    // ---- EQ column ----
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("EQ");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(2);

        mSwEQEnable = new ToggleSwitch;
        mSwEQEnable->setCheckable(true);
        mSwEQEnable->setChecked(true);
        lay->addWidget(mSwEQEnable, 0, Qt::AlignHCenter);
        connect(mSwEQEnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onEQEnabledToggled);

        mKnobLow    = new KnobWidget("Low",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobMid    = new KnobWidget("Mid",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobTreble = new KnobWidget("Treble", kEQBandMin, kEQBandMax, kEQBandDefault, "dB");

        connect(mKnobLow,    &KnobWidget::valueChanged, this, &MainWindowAlt::onLowChanged);
        connect(mKnobMid,    &KnobWidget::valueChanged, this, &MainWindowAlt::onMidChanged);
        connect(mKnobTreble, &KnobWidget::valueChanged, this, &MainWindowAlt::onTrebleChanged);

        lay->addWidget(mKnobLow);
        lay->addWidget(mKnobMid);
        lay->addWidget(mKnobTreble);
        lay->addStretch();
        stripLay->addWidget(col);
    }

    addDivider();

    // ---- Chorus 2 column ----
    {
        using namespace NAMConstants;
        mKnobCh2Rate  = new KnobWidget("Rate",  kChorusRateMin,  kChorusRateMax,  kChorusRateDefault,  "Hz");
        mKnobCh2Depth = new KnobWidget("Depth", kChorusDepthMin, kChorusDepthMax, kChorusDepthDefault, "%");
        mKnobCh2Mix   = new KnobWidget("Mix",   kChorusMixMin,   kChorusMixMax,   kChorusMixDefault,   "%");

        auto* col = makeEffectColumn("Chorus 2", mSwChorus2Enable,
            {mKnobCh2Rate, mKnobCh2Depth, mKnobCh2Mix});

        connect(mSwChorus2Enable, &ToggleSwitch::toggled,    this, &MainWindowAlt::onChorus2EnabledToggled);
        connect(mKnobCh2Rate,     &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus2RateChanged);
        connect(mKnobCh2Depth,    &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus2DepthChanged);
        connect(mKnobCh2Mix,      &KnobWidget::valueChanged, this, &MainWindowAlt::onChorus2MixChanged);
        stripLay->addWidget(col);
    }

    // ---- Tremolo column ----
    {
        using namespace NAMConstants;
        mKnobTrRate  = new KnobWidget("Rate",  kTremoloRateMin,  kTremoloRateMax,  kTremoloRateDefault,  "Hz");
        mKnobTrDepth = new KnobWidget("Depth", kTremoloDepthMin, kTremoloDepthMax, kTremoloDepthDefault, "%");

        auto* col = makeEffectColumn("Tremolo", mSwTremoloEnable, {mKnobTrRate, mKnobTrDepth});

        connect(mSwTremoloEnable, &ToggleSwitch::toggled,    this, &MainWindowAlt::onTremoloEnabledToggled);
        connect(mKnobTrRate,      &KnobWidget::valueChanged, this, &MainWindowAlt::onTremoloRateChanged);
        connect(mKnobTrDepth,     &KnobWidget::valueChanged, this, &MainWindowAlt::onTremoloDepthChanged);
        stripLay->addWidget(col);
    }

    // ---- Delay column ----
    {
        using namespace NAMConstants;
        mKnobDelTime     = new KnobWidget("Time",     kDelayTimeMin,     kDelayTimeMax,     kDelayTimeDefault,     "ms");
        mKnobDelFeedback = new KnobWidget("Feedback", kDelayFeedbackMin, kDelayFeedbackMax, kDelayFeedbackDefault, "%");
        mKnobDelMix      = new KnobWidget("Mix",      kDelayMixMin,      kDelayMixMax,      kDelayMixDefault,      "%");

        auto* col = makeEffectColumn("Delay", mSwDelayEnable,
            {mKnobDelTime, mKnobDelFeedback, mKnobDelMix}, &mSwDelayStereo);
        mSwDelayStereo->setChecked(true);  // ping-pong on by default

        connect(mSwDelayEnable,  &ToggleSwitch::toggled, this, &MainWindowAlt::onDelayEnabledToggled);
        connect(mSwDelayStereo,  &ToggleSwitch::toggled, this, &MainWindowAlt::onDelayStereoToggled);
        connect(mKnobDelTime,     &KnobWidget::valueChanged, this, &MainWindowAlt::onDelayTimeChanged);
        connect(mKnobDelFeedback, &KnobWidget::valueChanged, this, &MainWindowAlt::onDelayFeedbackChanged);
        connect(mKnobDelMix,      &KnobWidget::valueChanged, this, &MainWindowAlt::onDelayMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Reverb column ----
    {
        using namespace NAMConstants;
        mKnobRevRoom    = new KnobWidget("Room",    kReverbRoomMin,    kReverbRoomMax,    kReverbRoomDefault,    "%");
        mKnobRevDamping = new KnobWidget("Damping", kReverbDampingMin, kReverbDampingMax, kReverbDampingDefault, "%");
        mKnobRevWidth   = new KnobWidget("Width",   kReverbWidthMin,   kReverbWidthMax,   kReverbWidthDefault,   "%");
        mKnobRevMix     = new KnobWidget("Mix",     kReverbMixMin,     kReverbMixMax,     kReverbMixDefault,     "%");

        auto* col = makeEffectColumn("Reverb", mSwReverbEnable,
            {mKnobRevRoom, mKnobRevDamping, mKnobRevWidth, mKnobRevMix});

        connect(mSwReverbEnable,  &ToggleSwitch::toggled,    this, &MainWindowAlt::onReverbEnabledToggled);
        connect(mKnobRevRoom,     &KnobWidget::valueChanged, this, &MainWindowAlt::onReverbRoomChanged);
        connect(mKnobRevDamping,  &KnobWidget::valueChanged, this, &MainWindowAlt::onReverbDampingChanged);
        connect(mKnobRevWidth,    &KnobWidget::valueChanged, this, &MainWindowAlt::onReverbWidthChanged);
        connect(mKnobRevMix,      &KnobWidget::valueChanged, this, &MainWindowAlt::onReverbMixChanged);
        stripLay->addWidget(col);
    }

    // ---- IR toggle column (in strip; load/unload is in the top file row) ----
    {
        auto* col = new QGroupBox("IR");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(4);

        mSwIREnable = new ToggleSwitch;
        mSwIREnable->setCheckable(true);
        mSwIREnable->setChecked(true);
        lay->addWidget(mSwIREnable, 0, Qt::AlignHCenter);
        lay->addStretch();

        connect(mSwIREnable, &ToggleSwitch::toggled, this, &MainWindowAlt::onIREnabledToggled);
        stripLay->addWidget(col);
    }

    addDivider();

    // ---- Output gain column ----
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("Output");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(90);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        mKnobOutputGain = new KnobWidget("Gain", kGainMin, kGainMax, kGainDefault, "dB");
        connect(mKnobOutputGain, &KnobWidget::valueChanged, this, &MainWindowAlt::onOutputGainChanged);
        mMeterOut = makeVUBarVertical();
        lay->addWidget(mKnobOutputGain);
        lay->addWidget(mMeterOut, 0, Qt::AlignHCenter);
        lay->addStretch();
        stripLay->addWidget(col);
    }

    stripLay->addStretch();
    scroll->setWidget(strip);
    rootLayout->addWidget(scroll, 1);
}

void MainWindowAlt::setupAudio()
{
    if (mEngine->start("NAMPlayer-Alt"))
    {
        mLblJACKStatus->setText(
            QString("JACK: connected  |  %1 Hz  |  stereo out")
                .arg(static_cast<int>(mEngine->sampleRate())));
        mLblJACKStatus->setStyleSheet("color: green;");
    }
    else
    {
        mLblJACKStatus->setText("JACK: not connected (check jackd)");
        mLblJACKStatus->setStyleSheet("color: red;");
    }
}

// ---------------------------------------------------------------------------
// File slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onLoadNAM()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load NAM Model", {}, "NAM Files (*.nam);;All Files (*)");
    if (path.isEmpty()) return;
    if (!mEngine->loadModel(path.toStdString()))
    {
        QMessageBox::critical(this, "Error", "Failed to load NAM model:\n" + path);
        return;
    }
    updateStatusLabels();
    mBtnUnloadNAM->setEnabled(true);
}

void MainWindowAlt::onUnloadNAM()
{
    mEngine->unloadModel();
    updateStatusLabels();
    mBtnUnloadNAM->setEnabled(false);
}

void MainWindowAlt::onLoadIR()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load Impulse Response", {}, "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty()) return;
    if (!mEngine->loadIR(path.toStdString()))
    {
        QMessageBox::critical(this, "Error", "Failed to load IR:\n" + path);
        return;
    }
    updateStatusLabels();
    mBtnUnloadIR->setEnabled(true);
}

void MainWindowAlt::onUnloadIR()
{
    mEngine->unloadIR();
    updateStatusLabels();
    mBtnUnloadIR->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Gain / EQ / IR slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onInputGainChanged(double dB)  { mEngine->setInputGain (static_cast<float>(dB)); }
void MainWindowAlt::onOutputGainChanged(double dB) { mEngine->setOutputGain(static_cast<float>(dB)); }
void MainWindowAlt::onLowChanged(double dB)        { mEngine->setLowGain   (static_cast<float>(dB)); }
void MainWindowAlt::onMidChanged(double dB)        { mEngine->setMidGain   (static_cast<float>(dB)); }
void MainWindowAlt::onTrebleChanged(double dB)     { mEngine->setTrebleGain(static_cast<float>(dB)); }
void MainWindowAlt::onNAMEnabledToggled(bool en)   { mEngine->setNAMEnabled(en); }
void MainWindowAlt::onEQEnabledToggled(bool en)    { mEngine->setEQEnabled(en);  }
void MainWindowAlt::onIREnabledToggled(bool en)    { mEngine->setIREnabled(en);  }

// ---------------------------------------------------------------------------
// Tuner slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onTunerEnabledToggled(bool en) { mEngine->setTunerEnabled(en); }
void MainWindowAlt::onTunerMuteToggled(bool en)    { mEngine->setTunerMute(en);    }

// ---------------------------------------------------------------------------
// Noise gate slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onGateEnabledToggled(bool en)    { mEngine->setGateEnabled(en); }
void MainWindowAlt::onGateThresholdChanged(double v) { mEngine->gate().setThreshold(static_cast<float>(v)); }
void MainWindowAlt::onGateAttackChanged(double v)    { mEngine->gate().setAttack   (static_cast<float>(v)); }
void MainWindowAlt::onGateReleaseChanged(double v)   { mEngine->gate().setRelease  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Compressor slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onCompEnabledToggled(bool en)    { mEngine->setCompEnabled(en); }
void MainWindowAlt::onCompThresholdChanged(double v) { mEngine->comp().setThreshold(static_cast<float>(v)); }
void MainWindowAlt::onCompRatioChanged(double v)     { mEngine->comp().setRatio    (static_cast<float>(v)); }
void MainWindowAlt::onCompAttackChanged(double v)    { mEngine->comp().setAttack   (static_cast<float>(v)); }
void MainWindowAlt::onCompReleaseChanged(double v)   { mEngine->comp().setRelease  (static_cast<float>(v)); }
void MainWindowAlt::onCompMakeupChanged(double v)    { mEngine->comp().setMakeup   (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Pitch Shift slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onPitchEnabledToggled(bool en)   { mEngine->setPitchEnabled(en); }
void MainWindowAlt::onPitchSemitonesChanged(double v){ mEngine->pitch().setSemitones(static_cast<float>(v)); }
void MainWindowAlt::onPitchMixChanged(double v)      { mEngine->pitch().setMix      (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 1 slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onChorus1EnabledToggled(bool en) { mEngine->setChorus1Enabled(en); }
void MainWindowAlt::onChorus1StereoToggled(bool en)  { mEngine->setChorus1Stereo(en);  }
void MainWindowAlt::onChorus1RateChanged(double v)   { mEngine->chorus1().setRate (static_cast<float>(v)); }
void MainWindowAlt::onChorus1DepthChanged(double v)  { mEngine->chorus1().setDepth(static_cast<float>(v)); }
void MainWindowAlt::onChorus1MixChanged(double v)    { mEngine->chorus1().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Flanger slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onFlangerEnabledToggled(bool en)   { mEngine->setFlangerEnabled(en); }
void MainWindowAlt::onFlangerStereoToggled(bool en)    { mEngine->setFlangerStereo(en);  }
void MainWindowAlt::onFlangerRateChanged(double v)     { mEngine->flanger().setRate    (static_cast<float>(v)); }
void MainWindowAlt::onFlangerDepthChanged(double v)    { mEngine->flanger().setDepth   (static_cast<float>(v)); }
void MainWindowAlt::onFlangerFeedbackChanged(double v) { mEngine->flanger().setFeedback(static_cast<float>(v)); }
void MainWindowAlt::onFlangerMixChanged(double v)      { mEngine->flanger().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Phaser slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onPhaserEnabledToggled(bool en)   { mEngine->setPhaserEnabled(en); }
void MainWindowAlt::onPhaserStereoToggled(bool en)    { mEngine->setPhaserStereo(en);  }
void MainWindowAlt::onPhaserRateChanged(double v)     { mEngine->phaser().setRate    (static_cast<float>(v)); }
void MainWindowAlt::onPhaserDepthChanged(double v)    { mEngine->phaser().setDepth   (static_cast<float>(v)); }
void MainWindowAlt::onPhaserFeedbackChanged(double v) { mEngine->phaser().setFeedback(static_cast<float>(v)); }
void MainWindowAlt::onPhaserMixChanged(double v)      { mEngine->phaser().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 2 slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onChorus2EnabledToggled(bool en) { mEngine->setChorus2Enabled(en); }
void MainWindowAlt::onChorus2RateChanged(double v)   { mEngine->chorus2().setRate (static_cast<float>(v)); }
void MainWindowAlt::onChorus2DepthChanged(double v)  { mEngine->chorus2().setDepth(static_cast<float>(v)); }
void MainWindowAlt::onChorus2MixChanged(double v)    { mEngine->chorus2().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Tremolo slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onTremoloEnabledToggled(bool en) { mEngine->setTremoloEnabled(en); }
void MainWindowAlt::onTremoloRateChanged(double v)   { mEngine->tremolo().setRate (static_cast<float>(v)); }
void MainWindowAlt::onTremoloDepthChanged(double v)  { mEngine->tremolo().setDepth(static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Delay slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onDelayEnabledToggled(bool en)   { mEngine->setDelayEnabled(en); }
void MainWindowAlt::onDelayStereoToggled(bool en)    { mEngine->setDelayStereo(en);  }
void MainWindowAlt::onDelayTimeChanged(double v)     { mEngine->delay().setTime    (static_cast<float>(v)); }
void MainWindowAlt::onDelayFeedbackChanged(double v) { mEngine->delay().setFeedback(static_cast<float>(v)); }
void MainWindowAlt::onDelayMixChanged(double v)      { mEngine->delay().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Reverb slots
// ---------------------------------------------------------------------------

void MainWindowAlt::onReverbEnabledToggled(bool en)  { mEngine->setReverbEnabled(en); }
void MainWindowAlt::onReverbRoomChanged(double v)    { mEngine->reverb().setRoomSize(static_cast<float>(v)); }
void MainWindowAlt::onReverbDampingChanged(double v) { mEngine->reverb().setDamping (static_cast<float>(v)); }
void MainWindowAlt::onReverbWidthChanged(double v)   { mEngine->reverb().setWidth   (static_cast<float>(v)); }
void MainWindowAlt::onReverbMixChanged(double v)     { mEngine->reverb().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Meters & tuner
// ---------------------------------------------------------------------------

void MainWindowAlt::updateMeters()
{
    auto toMeter = [](float peak) -> int {
        if (peak <= 0.0f) return 0;
        float dB = 20.0f * std::log10(peak);
        return std::max(0, std::min(100, static_cast<int>((dB + 60.0f) / 60.0f * 100.0f)));
    };
    mMeterIn ->setValue(toMeter(mEngine->inputPeak()));
    mMeterOut->setValue(toMeter(mEngine->outputPeak()));

    if (!mEngine->isRunning() && mLblJACKStatus->text().startsWith("JACK: connected"))
    {
        mLblJACKStatus->setText("JACK: disconnected");
        mLblJACKStatus->setStyleSheet("color: red;");
    }
}

void MainWindowAlt::updateTuner()
{
    if (!mEngine->tunerEnabled()) return;

    float freq = mEngine->tuner().detectPitch();
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

    int barVal = static_cast<int>(50.0f + cents);
    mTunerBar->setValue(std::max(0, std::min(100, barVal)));
}

// ---------------------------------------------------------------------------
// Status labels
// ---------------------------------------------------------------------------

void MainWindowAlt::updateStatusLabels()
{
    if (mEngine->isModelLoaded())
    {
        QString p = QString::fromStdString(mEngine->modelPath());
        mLblNAMStatus->setText(p.section('/', -1));
        mLblNAMStatus->setStyleSheet("color: green;");
    }
    else
    {
        mLblNAMStatus->setText("No model loaded");
        mLblNAMStatus->setStyleSheet("");
    }

    if (mEngine->isIRLoaded())
    {
        QString p = QString::fromStdString(mEngine->irPath());
        mLblIRStatus->setText(p.section('/', -1));
        mLblIRStatus->setStyleSheet("color: green;");
    }
    else
    {
        mLblIRStatus->setText("No IR loaded");
        mLblIRStatus->setStyleSheet("");
    }
}

// ---------------------------------------------------------------------------
// Preset save / load  (same JSON schema as MainWindow)
// ---------------------------------------------------------------------------

void MainWindowAlt::onSavePreset()
{
    QString path = QFileDialog::getSaveFileName(
        this, "Save Preset", {},
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;

    QJsonObject obj;
    obj["namModelPath"] = QString::fromStdString(mEngine->modelPath());
    obj["irPath"]       = QString::fromStdString(mEngine->irPath());
    obj["inputGain"]    = mEngine->inputGain();
    obj["outputGain"]   = mEngine->outputGain();

    QJsonObject eq;
    eq["enabled"] = mEngine->eqEnabled();
    eq["low"]     = mEngine->lowGain();
    eq["mid"]     = mEngine->midGain();
    eq["treble"]  = mEngine->trebleGain();
    obj["eq"] = eq;

    QJsonObject tuner;
    tuner["enabled"] = mEngine->tunerEnabled();
    tuner["mute"]    = mEngine->tunerMute();
    obj["tuner"] = tuner;

    QJsonObject gate;
    gate["enabled"]   = mEngine->gateEnabled();
    gate["threshold"] = static_cast<double>(mEngine->gate().threshold());
    gate["attack"]    = static_cast<double>(mEngine->gate().attack());
    gate["release"]   = static_cast<double>(mEngine->gate().release());
    obj["noiseGate"] = gate;

    QJsonObject comp;
    comp["enabled"]   = mEngine->compEnabled();
    comp["threshold"] = static_cast<double>(mEngine->comp().threshold());
    comp["ratio"]     = static_cast<double>(mEngine->comp().ratio());
    comp["attack"]    = static_cast<double>(mEngine->comp().attack());
    comp["release"]   = static_cast<double>(mEngine->comp().release());
    comp["makeup"]    = static_cast<double>(mEngine->comp().makeup());
    obj["compressor"] = comp;

    QJsonObject pitch;
    pitch["enabled"]   = mEngine->pitchEnabled();
    pitch["semitones"] = static_cast<double>(mEngine->pitch().semitones());
    pitch["mix"]       = static_cast<double>(mEngine->pitch().mix());
    obj["pitchShift"] = pitch;

    QJsonObject ch1;
    ch1["enabled"] = mEngine->chorus1Enabled();
    ch1["stereo"]  = mEngine->chorus1Stereo();
    ch1["rate"]    = static_cast<double>(mEngine->chorus1().rate());
    ch1["depth"]   = static_cast<double>(mEngine->chorus1().depth());
    ch1["mix"]     = static_cast<double>(mEngine->chorus1().mix());
    obj["chorus1"] = ch1;

    QJsonObject fl;
    fl["enabled"]  = mEngine->flangerEnabled();
    fl["stereo"]   = mEngine->flangerStereo();
    fl["rate"]     = static_cast<double>(mEngine->flanger().rate());
    fl["depth"]    = static_cast<double>(mEngine->flanger().depth());
    fl["feedback"] = static_cast<double>(mEngine->flanger().feedback());
    fl["mix"]      = static_cast<double>(mEngine->flanger().mix());
    obj["flanger"] = fl;

    QJsonObject ph;
    ph["enabled"]  = mEngine->phaserEnabled();
    ph["stereo"]   = mEngine->phaserStereo();
    ph["rate"]     = static_cast<double>(mEngine->phaser().rate());
    ph["depth"]    = static_cast<double>(mEngine->phaser().depth());
    ph["feedback"] = static_cast<double>(mEngine->phaser().feedback());
    ph["mix"]      = static_cast<double>(mEngine->phaser().mix());
    obj["phaser"] = ph;

    QJsonObject ch2;
    ch2["enabled"] = mEngine->chorus2Enabled();
    ch2["rate"]    = static_cast<double>(mEngine->chorus2().rate());
    ch2["depth"]   = static_cast<double>(mEngine->chorus2().depth());
    ch2["mix"]     = static_cast<double>(mEngine->chorus2().mix());
    obj["chorus2"] = ch2;

    QJsonObject tr;
    tr["enabled"] = mEngine->tremoloEnabled();
    tr["rate"]    = static_cast<double>(mEngine->tremolo().rate());
    tr["depth"]   = static_cast<double>(mEngine->tremolo().depth());
    obj["tremolo"] = tr;

    QJsonObject del;
    del["enabled"]  = mEngine->delayEnabled();
    del["stereo"]   = mEngine->delayStereo();
    del["time"]     = static_cast<double>(mEngine->delay().time());
    del["feedback"] = static_cast<double>(mEngine->delay().feedback());
    del["mix"]      = static_cast<double>(mEngine->delay().mix());
    obj["delay"] = del;

    QJsonObject rev;
    rev["enabled"]  = mEngine->reverbEnabled();
    rev["roomSize"] = static_cast<double>(mEngine->reverb().roomSize());
    rev["damping"]  = static_cast<double>(mEngine->reverb().damping());
    rev["width"]    = static_cast<double>(mEngine->reverb().width());
    rev["mix"]      = static_cast<double>(mEngine->reverb().mix());
    obj["reverb"] = rev;

    obj["irEnabled"] = mEngine->irEnabled();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "Error", "Cannot write preset:\n" + path);
        return;
    }
    f.write(QJsonDocument(obj).toJson());
}

void MainWindowAlt::onLoadPreset()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load Preset", {},
        "NAM Preset (*.nampreset);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;

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

    QJsonObject obj = doc.object();

    auto getD = [](const QJsonObject& o, const QString& k, double def) -> double {
        return o.contains(k) ? o[k].toDouble(def) : def;
    };
    auto getB = [](const QJsonObject& o, const QString& k, bool def) -> bool {
        return o.contains(k) ? o[k].toBool(def) : def;
    };

    if (obj.contains("namModelPath"))
    {
        QString p = obj["namModelPath"].toString();
        if (!p.isEmpty() && QFile::exists(p))
        {
            if (mEngine->loadModel(p.toStdString()))
            {
                updateStatusLabels();
                mBtnUnloadNAM->setEnabled(true);
            }
        }
    }
    if (obj.contains("irPath"))
    {
        QString p = obj["irPath"].toString();
        if (!p.isEmpty() && QFile::exists(p))
        {
            if (mEngine->loadIR(p.toStdString()))
            {
                updateStatusLabels();
                mBtnUnloadIR->setEnabled(true);
            }
        }
    }

    mKnobInputGain ->setValue(getD(obj, "inputGain",  0.0));
    mKnobOutputGain->setValue(getD(obj, "outputGain", 0.0));

    if (obj.contains("eq")) {
        auto eq = obj["eq"].toObject();
        mSwEQEnable->setChecked(getB(eq, "enabled", true));
        mKnobLow   ->setValue(getD(eq, "low",    0.0));
        mKnobMid   ->setValue(getD(eq, "mid",    0.0));
        mKnobTreble->setValue(getD(eq, "treble", 0.0));
    }
    if (obj.contains("tuner")) {
        auto t = obj["tuner"].toObject();
        mSwTunerEnable->setChecked(getB(t, "enabled", false));
        mSwTunerMute  ->setChecked(getB(t, "mute",    false));
    }
    if (obj.contains("noiseGate")) {
        auto g = obj["noiseGate"].toObject();
        mSwGateEnable   ->setChecked(getB(g, "enabled",   false));
        mKnobGateThresh ->setValue(getD(g, "threshold", NAMConstants::kGateThreshDefault));
        mKnobGateAttack ->setValue(getD(g, "attack",    NAMConstants::kGateAttackDefault));
        mKnobGateRelease->setValue(getD(g, "release",   NAMConstants::kGateReleaseDefault));
    }
    if (obj.contains("compressor")) {
        auto c = obj["compressor"].toObject();
        mSwCompEnable   ->setChecked(getB(c, "enabled",   false));
        mKnobCompThresh ->setValue(getD(c, "threshold", NAMConstants::kCompThreshDefault));
        mKnobCompRatio  ->setValue(getD(c, "ratio",     NAMConstants::kCompRatioDefault));
        mKnobCompAttack ->setValue(getD(c, "attack",    NAMConstants::kCompAttackDefault));
        mKnobCompRelease->setValue(getD(c, "release",   NAMConstants::kCompReleaseDefault));
        mKnobCompMakeup ->setValue(getD(c, "makeup",    NAMConstants::kCompMakeupDefault));
    }
    if (obj.contains("pitchShift")) {
        auto p = obj["pitchShift"].toObject();
        mSwPitchEnable ->setChecked(getB(p, "enabled", false));
        mKnobPitchSemi ->setValue(getD(p, "semitones", NAMConstants::kPitchSemiDefault));
        mKnobPitchMix  ->setValue(getD(p, "mix",       NAMConstants::kPitchMixDefault));
    }
    if (obj.contains("chorus1")) {
        auto c = obj["chorus1"].toObject();
        mSwChorus1Enable->setChecked(getB(c, "enabled", false));
        mSwChorus1Stereo->setChecked(getB(c, "stereo",  false));
        mKnobCh1Rate ->setValue(getD(c, "rate",  NAMConstants::kChorusRateDefault));
        mKnobCh1Depth->setValue(getD(c, "depth", NAMConstants::kChorusDepthDefault));
        mKnobCh1Mix  ->setValue(getD(c, "mix",   NAMConstants::kChorusMixDefault));
    }
    if (obj.contains("flanger")) {
        auto fl = obj["flanger"].toObject();
        mSwFlangerEnable->setChecked(getB(fl, "enabled", false));
        mSwFlangerStereo->setChecked(getB(fl, "stereo",  false));
        mKnobFlRate     ->setValue(getD(fl, "rate",     NAMConstants::kFlangerRateDefault));
        mKnobFlDepth    ->setValue(getD(fl, "depth",    NAMConstants::kFlangerDepthDefault));
        mKnobFlFeedback ->setValue(getD(fl, "feedback", NAMConstants::kFlangerFeedbackDefault));
        mKnobFlMix      ->setValue(getD(fl, "mix",      NAMConstants::kFlangerMixDefault));
    }
    if (obj.contains("phaser")) {
        auto p = obj["phaser"].toObject();
        mSwPhaserEnable ->setChecked(getB(p, "enabled", false));
        mSwPhaserStereo ->setChecked(getB(p, "stereo",  false));
        mKnobPhRate     ->setValue(getD(p, "rate",     NAMConstants::kPhaserRateDefault));
        mKnobPhDepth    ->setValue(getD(p, "depth",    NAMConstants::kPhaserDepthDefault));
        mKnobPhFeedback ->setValue(getD(p, "feedback", NAMConstants::kPhaserFeedbackDefault));
        mKnobPhMix      ->setValue(getD(p, "mix",      NAMConstants::kPhaserMixDefault));
    }
    if (obj.contains("chorus2")) {
        auto c = obj["chorus2"].toObject();
        mSwChorus2Enable->setChecked(getB(c, "enabled", false));
        mKnobCh2Rate ->setValue(getD(c, "rate",  NAMConstants::kChorusRateDefault));
        mKnobCh2Depth->setValue(getD(c, "depth", NAMConstants::kChorusDepthDefault));
        mKnobCh2Mix  ->setValue(getD(c, "mix",   NAMConstants::kChorusMixDefault));
    }
    if (obj.contains("tremolo")) {
        auto t = obj["tremolo"].toObject();
        mSwTremoloEnable->setChecked(getB(t, "enabled", false));
        mKnobTrRate ->setValue(getD(t, "rate",  NAMConstants::kTremoloRateDefault));
        mKnobTrDepth->setValue(getD(t, "depth", NAMConstants::kTremoloDepthDefault));
    }
    if (obj.contains("delay")) {
        auto d = obj["delay"].toObject();
        mSwDelayEnable  ->setChecked(getB(d, "enabled",  false));
        mSwDelayStereo  ->setChecked(getB(d, "stereo",   true));
        mKnobDelTime    ->setValue(getD(d, "time",     NAMConstants::kDelayTimeDefault));
        mKnobDelFeedback->setValue(getD(d, "feedback", NAMConstants::kDelayFeedbackDefault));
        mKnobDelMix     ->setValue(getD(d, "mix",      NAMConstants::kDelayMixDefault));
    }
    if (obj.contains("reverb")) {
        auto rv = obj["reverb"].toObject();
        mSwReverbEnable ->setChecked(getB(rv, "enabled",  false));
        mKnobRevRoom    ->setValue(getD(rv, "roomSize", NAMConstants::kReverbRoomDefault));
        mKnobRevDamping ->setValue(getD(rv, "damping",  NAMConstants::kReverbDampingDefault));
        mKnobRevWidth   ->setValue(getD(rv, "width",    NAMConstants::kReverbWidthDefault));
        mKnobRevMix     ->setValue(getD(rv, "mix",      NAMConstants::kReverbMixDefault));
    }
    if (obj.contains("irEnabled"))
        mSwIREnable->setChecked(obj["irEnabled"].toBool(true));
}

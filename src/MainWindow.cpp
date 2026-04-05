#include "MainWindow.h"
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player");
    setMinimumSize(1400, 500);

    setupUI();
    setupAudio();

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
    mEngine->stop();
}

// ---------------------------------------------------------------------------
// Build a vertical effect column: title in group box, toggle, knobs as rows
// ---------------------------------------------------------------------------

QWidget* MainWindow::makeEffectColumn(const QString& title,
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
        auto* saveAction = presetMenu->addAction("Save Preset…");
        auto* loadAction = presetMenu->addAction("Load Preset…");
        connect(saveAction, &QAction::triggered, this, &MainWindow::onSavePreset);
        connect(loadAction, &QAction::triggered, this, &MainWindow::onLoadPreset);
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
        connect(btnSavePreset, &QPushButton::clicked, this, &MainWindow::onSavePreset);
        connect(btnLoadPreset, &QPushButton::clicked, this, &MainWindow::onLoadPreset);

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
        connect(mBtnLoadNAM,   &QPushButton::clicked, this, &MainWindow::onLoadNAM);
        connect(mBtnUnloadNAM, &QPushButton::clicked, this, &MainWindow::onUnloadNAM);
        namLay->addWidget(mBtnLoadNAM);
        namLay->addWidget(mBtnUnloadNAM);
        namLay->addWidget(mLblNAMStatus, 1);

        // IR sub-box
        auto* irBox = new QGroupBox("IR (Cab)");
        auto* irLay = new QHBoxLayout(irBox);
        irLay->setContentsMargins(4, 4, 4, 4);
        mBtnLoadIR   = new QPushButton("Load…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mLblIRStatus = new QLabel("No IR loaded");
        connect(mBtnLoadIR,   &QPushButton::clicked, this, &MainWindow::onLoadIR);
        connect(mBtnUnloadIR, &QPushButton::clicked, this, &MainWindow::onUnloadIR);
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
        mKnobInputGain->setScrollStep(0.1);
        connect(mKnobInputGain, &KnobWidget::valueChanged, this, &MainWindow::onInputGainChanged);
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

        connect(mSwTunerEnable, &ToggleSwitch::toggled, this, &MainWindow::onTunerEnabledToggled);
        connect(mSwTunerMute,   &ToggleSwitch::toggled, this, &MainWindow::onTunerMuteToggled);

        stripLay->addWidget(col);
    }

    // ---- Noise Gate column ----
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

        connect(mSwGateEnable,    &ToggleSwitch::toggled,    this, &MainWindow::onGateEnabledToggled);
        connect(mKnobGateThresh,  &KnobWidget::valueChanged, this, &MainWindow::onGateThresholdChanged);
        connect(mKnobGateAttack,  &KnobWidget::valueChanged, this, &MainWindow::onGateAttackChanged);
        connect(mKnobGateRelease, &KnobWidget::valueChanged, this, &MainWindow::onGateReleaseChanged);
        stripLay->addWidget(col);
    }

    // ---- Compressor column ----
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

        connect(mSwCompEnable,    &ToggleSwitch::toggled,    this, &MainWindow::onCompEnabledToggled);
        connect(mKnobCompThresh,  &KnobWidget::valueChanged, this, &MainWindow::onCompThresholdChanged);
        connect(mKnobCompRatio,   &KnobWidget::valueChanged, this, &MainWindow::onCompRatioChanged);
        connect(mKnobCompAttack,  &KnobWidget::valueChanged, this, &MainWindow::onCompAttackChanged);
        connect(mKnobCompRelease, &KnobWidget::valueChanged, this, &MainWindow::onCompReleaseChanged);
        connect(mKnobCompMakeup,  &KnobWidget::valueChanged, this, &MainWindow::onCompMakeupChanged);
        stripLay->addWidget(col);
    }

    // ---- Octave Pedal column ----
    {
        using namespace NAMConstants;
        mKnobOctUp   = new KnobWidget("Oct Up",   kOctaveUpMin,   kOctaveUpMax,   kOctaveUpDefault,   "%");
        mKnobOctDown = new KnobWidget("Oct Down", kOctaveDownMin, kOctaveDownMax, kOctaveDownDefault, "%");
        mKnobOctUp->setScrollStep(1.0);
        mKnobOctDown->setScrollStep(1.0);

        auto* col = makeEffectColumn("Octave", mSwOctaveEnable, {mKnobOctUp, mKnobOctDown});

        connect(mSwOctaveEnable, &ToggleSwitch::toggled,    this, &MainWindow::onOctaveEnabledToggled);
        connect(mKnobOctUp,      &KnobWidget::valueChanged, this, &MainWindow::onOctaveUpChanged);
        connect(mKnobOctDown,    &KnobWidget::valueChanged, this, &MainWindow::onOctaveDownChanged);
        stripLay->addWidget(col);
    }

    // ---- Chorus 1 column ----
    {
        using namespace NAMConstants;
        mKnobCh1Rate  = new KnobWidget("Rate",  kChorusRateMin,  kChorusRateMax,  kChorusRateDefault,  "Hz");
        mKnobCh1Depth = new KnobWidget("Depth", kChorusDepthMin, kChorusDepthMax, kChorusDepthDefault, "%");
        mKnobCh1Mix   = new KnobWidget("Mix",   kChorusMixMin,   kChorusMixMax,   kChorusMixDefault,   "%");
        mKnobCh1Rate->setScrollStep(0.1);
        mKnobCh1Depth->setScrollStep(1.0);
        mKnobCh1Mix->setScrollStep(1.0);

        auto* col = makeEffectColumn("Chorus 1", mSwChorus1Enable,
            {mKnobCh1Rate, mKnobCh1Depth, mKnobCh1Mix}, &mSwChorus1Stereo);

        connect(mSwChorus1Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus1EnabledToggled);
        connect(mSwChorus1Stereo, &ToggleSwitch::toggled,    this, &MainWindow::onChorus1StereoToggled);
        connect(mKnobCh1Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus1RateChanged);
        connect(mKnobCh1Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus1DepthChanged);
        connect(mKnobCh1Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus1MixChanged);
        stripLay->addWidget(col);
    }

    // ---- Flanger column ----
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

        connect(mSwFlangerEnable, &ToggleSwitch::toggled,    this, &MainWindow::onFlangerEnabledToggled);
        connect(mSwFlangerStereo, &ToggleSwitch::toggled,    this, &MainWindow::onFlangerStereoToggled);
        connect(mKnobFlRate,      &KnobWidget::valueChanged, this, &MainWindow::onFlangerRateChanged);
        connect(mKnobFlDepth,     &KnobWidget::valueChanged, this, &MainWindow::onFlangerDepthChanged);
        connect(mKnobFlFeedback,  &KnobWidget::valueChanged, this, &MainWindow::onFlangerFeedbackChanged);
        connect(mKnobFlMix,       &KnobWidget::valueChanged, this, &MainWindow::onFlangerMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Phaser column ----
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

        connect(mSwPhaserEnable, &ToggleSwitch::toggled,    this, &MainWindow::onPhaserEnabledToggled);
        connect(mSwPhaserStereo, &ToggleSwitch::toggled,    this, &MainWindow::onPhaserStereoToggled);
        connect(mKnobPhRate,     &KnobWidget::valueChanged, this, &MainWindow::onPhaserRateChanged);
        connect(mKnobPhDepth,    &KnobWidget::valueChanged, this, &MainWindow::onPhaserDepthChanged);
        connect(mKnobPhFeedback, &KnobWidget::valueChanged, this, &MainWindow::onPhaserFeedbackChanged);
        connect(mKnobPhMix,      &KnobWidget::valueChanged, this, &MainWindow::onPhaserMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Volume Sweep column ----
    {
        using namespace NAMConstants;
        mKnobSweepAtk  = new KnobWidget("Attack", kSweepAttackMin, kSweepAttackMax, kSweepAttackDefault, "ms");
        mKnobSweepSens = new KnobWidget("Sens",   kSweepSensMin,   kSweepSensMax,   kSweepSensDefault,   "%");
        mKnobSweepMix  = new KnobWidget("Mix",    kSweepMixMin,    kSweepMixMax,    kSweepMixDefault,    "%");
        mKnobSweepAtk->setScrollStep(10.0);
        mKnobSweepSens->setScrollStep(1.0);
        mKnobSweepMix->setScrollStep(1.0);

        auto* col = makeEffectColumn("V.Sweep", mSwSweepEnable,
            {mKnobSweepAtk, mKnobSweepSens, mKnobSweepMix});

        connect(mSwSweepEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onVolumeSweepEnabledToggled);
        connect(mKnobSweepAtk,   &KnobWidget::valueChanged, this, &MainWindow::onVolumeSweepAttackChanged);
        connect(mKnobSweepSens,  &KnobWidget::valueChanged, this, &MainWindow::onVolumeSweepSensChanged);
        connect(mKnobSweepMix,   &KnobWidget::valueChanged, this, &MainWindow::onVolumeSweepMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Dynamic Wah column ----
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

        connect(mSwWahEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onDynamicWahEnabledToggled);
        connect(mKnobWahSens,  &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahSensChanged);
        connect(mKnobWahRes,   &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahResChanged);
        connect(mKnobWahMix,   &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahMixChanged);
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

        connect(mSwNAMEnable, &ToggleSwitch::toggled, this, &MainWindow::onNAMEnabledToggled);
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
        connect(mSwEQEnable, &ToggleSwitch::toggled, this, &MainWindow::onEQEnabledToggled);

        mKnobLow    = new KnobWidget("Low",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobMid    = new KnobWidget("Mid",    kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobTreble = new KnobWidget("Treble", kEQBandMin, kEQBandMax, kEQBandDefault, "dB");
        mKnobLow->setScrollStep(0.1);
        mKnobMid->setScrollStep(0.1);
        mKnobTreble->setScrollStep(0.1);

        connect(mKnobLow,    &KnobWidget::valueChanged, this, &MainWindow::onLowChanged);
        connect(mKnobMid,    &KnobWidget::valueChanged, this, &MainWindow::onMidChanged);
        connect(mKnobTreble, &KnobWidget::valueChanged, this, &MainWindow::onTrebleChanged);

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
        mKnobCh2Rate->setScrollStep(0.1);
        mKnobCh2Depth->setScrollStep(1.0);
        mKnobCh2Mix->setScrollStep(1.0);

        auto* col = makeEffectColumn("Chorus 2", mSwChorus2Enable,
            {mKnobCh2Rate, mKnobCh2Depth, mKnobCh2Mix});

        connect(mSwChorus2Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus2EnabledToggled);
        connect(mKnobCh2Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus2RateChanged);
        connect(mKnobCh2Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus2DepthChanged);
        connect(mKnobCh2Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus2MixChanged);
        stripLay->addWidget(col);
    }

    // ---- Tremolo column ----
    {
        using namespace NAMConstants;
        mKnobTrRate  = new KnobWidget("Rate",  kTremoloRateMin,  kTremoloRateMax,  kTremoloRateDefault,  "Hz");
        mKnobTrDepth = new KnobWidget("Depth", kTremoloDepthMin, kTremoloDepthMax, kTremoloDepthDefault, "%");
        mKnobTrRate->setScrollStep(0.1);
        mKnobTrDepth->setScrollStep(1.0);

        auto* col = makeEffectColumn("Tremolo", mSwTremoloEnable, {mKnobTrRate, mKnobTrDepth});

        connect(mSwTremoloEnable, &ToggleSwitch::toggled,    this, &MainWindow::onTremoloEnabledToggled);
        connect(mKnobTrRate,      &KnobWidget::valueChanged, this, &MainWindow::onTremoloRateChanged);
        connect(mKnobTrDepth,     &KnobWidget::valueChanged, this, &MainWindow::onTremoloDepthChanged);
        stripLay->addWidget(col);
    }

    // ---- Delay column ----
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
        mSwDelayStereo->setChecked(true);  // ping-pong on by default

        connect(mSwDelayEnable,   &ToggleSwitch::toggled,    this, &MainWindow::onDelayEnabledToggled);
        connect(mSwDelayStereo,   &ToggleSwitch::toggled,    this, &MainWindow::onDelayStereoToggled);
        connect(mKnobDelTime,     &KnobWidget::valueChanged, this, &MainWindow::onDelayTimeChanged);
        connect(mKnobDelFeedback, &KnobWidget::valueChanged, this, &MainWindow::onDelayFeedbackChanged);
        connect(mKnobDelMix,      &KnobWidget::valueChanged, this, &MainWindow::onDelayMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Reverse column ----
    {
        using namespace NAMConstants;
        mKnobRevLen  = new KnobWidget("Length", kReverseLenMin, kReverseLenMax, kReverseLenDefault, "ms");
        mKnobRevMixR = new KnobWidget("Mix",    kReverseMixMin, kReverseMixMax, kReverseMixDefault, "%");
        mKnobRevLen->setScrollStep(10.0);
        mKnobRevMixR->setScrollStep(1.0);

        auto* col = makeEffectColumn("Reverse", mSwReverseEnable,
            {mKnobRevLen, mKnobRevMixR});

        connect(mSwReverseEnable, &ToggleSwitch::toggled,    this, &MainWindow::onReverseEnabledToggled);
        connect(mKnobRevLen,      &KnobWidget::valueChanged, this, &MainWindow::onReverseLengthChanged);
        connect(mKnobRevMixR,     &KnobWidget::valueChanged, this, &MainWindow::onReverseMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Reverb column ----
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

        connect(mSwReverbEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onReverbEnabledToggled);
        connect(mKnobRevRoom,     &KnobWidget::valueChanged, this, &MainWindow::onReverbRoomChanged);
        connect(mKnobRevDamping,  &KnobWidget::valueChanged, this, &MainWindow::onReverbDampingChanged);
        connect(mKnobRevWidth,    &KnobWidget::valueChanged, this, &MainWindow::onReverbWidthChanged);
        connect(mKnobRevMix,      &KnobWidget::valueChanged, this, &MainWindow::onReverbMixChanged);
        stripLay->addWidget(col);
    }

    // ---- Bit Crusher column ----
    {
        using namespace NAMConstants;
        mKnobBitBits = new KnobWidget("Bits", kBitCrushBitsMin, kBitCrushBitsMax, kBitCrushBitsDefault, "");
        mKnobBitMix  = new KnobWidget("Mix",  kBitCrushMixMin,  kBitCrushMixMax,  kBitCrushMixDefault,  "%");
        mKnobBitBits->setScrollStep(1.0);
        mKnobBitMix->setScrollStep(1.0);

        auto* col = makeEffectColumn("Bit Crush", mSwBitEnable,
            {mKnobBitBits, mKnobBitMix});

        connect(mSwBitEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onBitCrusherEnabledToggled);
        connect(mKnobBitBits,  &KnobWidget::valueChanged, this, &MainWindow::onBitCrusherBitsChanged);
        connect(mKnobBitMix,   &KnobWidget::valueChanged, this, &MainWindow::onBitCrusherMixChanged);
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

        connect(mSwIREnable, &ToggleSwitch::toggled, this, &MainWindow::onIREnabledToggled);
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
        mKnobOutputGain->setScrollStep(0.1);
        connect(mKnobOutputGain, &KnobWidget::valueChanged, this, &MainWindow::onOutputGainChanged);
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

void MainWindow::setupAudio()
{
    if (mEngine->start("NAMPlayer"))
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

void MainWindow::onLoadNAM()
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

void MainWindow::onUnloadNAM()
{
    mEngine->unloadModel();
    updateStatusLabels();
    mBtnUnloadNAM->setEnabled(false);
}

void MainWindow::onLoadIR()
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

void MainWindow::onUnloadIR()
{
    mEngine->unloadIR();
    updateStatusLabels();
    mBtnUnloadIR->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Gain / EQ / IR slots
// ---------------------------------------------------------------------------

void MainWindow::onInputGainChanged(double dB)  { mEngine->setInputGain (static_cast<float>(dB)); }
void MainWindow::onOutputGainChanged(double dB) { mEngine->setOutputGain(static_cast<float>(dB)); }
void MainWindow::onLowChanged(double dB)        { mEngine->setLowGain   (static_cast<float>(dB)); }
void MainWindow::onMidChanged(double dB)        { mEngine->setMidGain   (static_cast<float>(dB)); }
void MainWindow::onTrebleChanged(double dB)     { mEngine->setTrebleGain(static_cast<float>(dB)); }
void MainWindow::onNAMEnabledToggled(bool en)   { mEngine->setNAMEnabled(en); }
void MainWindow::onEQEnabledToggled(bool en)    { mEngine->setEQEnabled(en);  }
void MainWindow::onIREnabledToggled(bool en)    { mEngine->setIREnabled(en);  }

// ---------------------------------------------------------------------------
// Tuner slots
// ---------------------------------------------------------------------------

void MainWindow::onTunerEnabledToggled(bool en) { mEngine->setTunerEnabled(en); }
void MainWindow::onTunerMuteToggled(bool en)    { mEngine->setTunerMute(en);    }

// ---------------------------------------------------------------------------
// Noise gate slots
// ---------------------------------------------------------------------------

void MainWindow::onGateEnabledToggled(bool en)    { mEngine->setGateEnabled(en); }
void MainWindow::onGateThresholdChanged(double v) { mEngine->gate().setThreshold(static_cast<float>(v)); }
void MainWindow::onGateAttackChanged(double v)    { mEngine->gate().setAttack   (static_cast<float>(v)); }
void MainWindow::onGateReleaseChanged(double v)   { mEngine->gate().setRelease  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Compressor slots
// ---------------------------------------------------------------------------

void MainWindow::onCompEnabledToggled(bool en)    { mEngine->setCompEnabled(en); }
void MainWindow::onCompThresholdChanged(double v) { mEngine->comp().setThreshold(static_cast<float>(v)); }
void MainWindow::onCompRatioChanged(double v)     { mEngine->comp().setRatio    (static_cast<float>(v)); }
void MainWindow::onCompAttackChanged(double v)    { mEngine->comp().setAttack   (static_cast<float>(v)); }
void MainWindow::onCompReleaseChanged(double v)   { mEngine->comp().setRelease  (static_cast<float>(v)); }
void MainWindow::onCompMakeupChanged(double v)    { mEngine->comp().setMakeup   (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Reverse slots
// ---------------------------------------------------------------------------

void MainWindow::onReverseEnabledToggled(bool en) { mEngine->setReverseEnabled(en); }
void MainWindow::onReverseLengthChanged(double v) { mEngine->reverse().setLength(static_cast<float>(v)); }
void MainWindow::onReverseMixChanged(double v)    { mEngine->reverse().setMix   (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Volume Sweep slots
// ---------------------------------------------------------------------------

void MainWindow::onVolumeSweepEnabledToggled(bool en) { mEngine->setVolumeSweepEnabled(en); }
void MainWindow::onVolumeSweepAttackChanged(double v) { mEngine->volumeSweep().setAttack     (static_cast<float>(v)); }
void MainWindow::onVolumeSweepSensChanged(double v)   { mEngine->volumeSweep().setSensitivity(static_cast<float>(v)); }
void MainWindow::onVolumeSweepMixChanged(double v)    { mEngine->volumeSweep().setMix        (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Dynamic Wah slots
// ---------------------------------------------------------------------------

void MainWindow::onDynamicWahEnabledToggled(bool en) { mEngine->setDynamicWahEnabled(en); }
void MainWindow::onDynamicWahSensChanged(double v)   { mEngine->dynamicWah().setSensitivity(static_cast<float>(v)); }
void MainWindow::onDynamicWahResChanged(double v)    { mEngine->dynamicWah().setResonance  (static_cast<float>(v)); }
void MainWindow::onDynamicWahMixChanged(double v)    { mEngine->dynamicWah().setMix        (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Bit Crusher slots
// ---------------------------------------------------------------------------

void MainWindow::onBitCrusherEnabledToggled(bool en) { mEngine->setBitCrusherEnabled(en); }
void MainWindow::onBitCrusherBitsChanged(double v)   { mEngine->bitCrusher().setBits(static_cast<float>(v)); }
void MainWindow::onBitCrusherMixChanged(double v)    { mEngine->bitCrusher().setMix (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Octave Pedal slots
// ---------------------------------------------------------------------------

void MainWindow::onOctaveEnabledToggled(bool en) { mEngine->setOctaveEnabled(en); }
void MainWindow::onOctaveUpChanged(double v)     { mEngine->octave().setUpLevel  (static_cast<float>(v)); }
void MainWindow::onOctaveDownChanged(double v)   { mEngine->octave().setDownLevel(static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 1 slots
// ---------------------------------------------------------------------------

void MainWindow::onChorus1EnabledToggled(bool en) { mEngine->setChorus1Enabled(en); }
void MainWindow::onChorus1StereoToggled(bool en)  { mEngine->setChorus1Stereo(en);  }
void MainWindow::onChorus1RateChanged(double v)   { mEngine->chorus1().setRate (static_cast<float>(v)); }
void MainWindow::onChorus1DepthChanged(double v)  { mEngine->chorus1().setDepth(static_cast<float>(v)); }
void MainWindow::onChorus1MixChanged(double v)    { mEngine->chorus1().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Flanger slots
// ---------------------------------------------------------------------------

void MainWindow::onFlangerEnabledToggled(bool en)   { mEngine->setFlangerEnabled(en); }
void MainWindow::onFlangerStereoToggled(bool en)    { mEngine->setFlangerStereo(en);  }
void MainWindow::onFlangerRateChanged(double v)     { mEngine->flanger().setRate    (static_cast<float>(v)); }
void MainWindow::onFlangerDepthChanged(double v)    { mEngine->flanger().setDepth   (static_cast<float>(v)); }
void MainWindow::onFlangerFeedbackChanged(double v) { mEngine->flanger().setFeedback(static_cast<float>(v)); }
void MainWindow::onFlangerMixChanged(double v)      { mEngine->flanger().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Phaser slots
// ---------------------------------------------------------------------------

void MainWindow::onPhaserEnabledToggled(bool en)   { mEngine->setPhaserEnabled(en); }
void MainWindow::onPhaserStereoToggled(bool en)    { mEngine->setPhaserStereo(en);  }
void MainWindow::onPhaserRateChanged(double v)     { mEngine->phaser().setRate    (static_cast<float>(v)); }
void MainWindow::onPhaserDepthChanged(double v)    { mEngine->phaser().setDepth   (static_cast<float>(v)); }
void MainWindow::onPhaserFeedbackChanged(double v) { mEngine->phaser().setFeedback(static_cast<float>(v)); }
void MainWindow::onPhaserMixChanged(double v)      { mEngine->phaser().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 2 slots
// ---------------------------------------------------------------------------

void MainWindow::onChorus2EnabledToggled(bool en) { mEngine->setChorus2Enabled(en); }
void MainWindow::onChorus2RateChanged(double v)   { mEngine->chorus2().setRate (static_cast<float>(v)); }
void MainWindow::onChorus2DepthChanged(double v)  { mEngine->chorus2().setDepth(static_cast<float>(v)); }
void MainWindow::onChorus2MixChanged(double v)    { mEngine->chorus2().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Tremolo slots
// ---------------------------------------------------------------------------

void MainWindow::onTremoloEnabledToggled(bool en) { mEngine->setTremoloEnabled(en); }
void MainWindow::onTremoloRateChanged(double v)   { mEngine->tremolo().setRate (static_cast<float>(v)); }
void MainWindow::onTremoloDepthChanged(double v)  { mEngine->tremolo().setDepth(static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Delay slots
// ---------------------------------------------------------------------------

void MainWindow::onDelayEnabledToggled(bool en)   { mEngine->setDelayEnabled(en); }
void MainWindow::onDelayStereoToggled(bool en)    { mEngine->setDelayStereo(en);  }
void MainWindow::onDelayTimeChanged(double v)     { mEngine->delay().setTime    (static_cast<float>(v)); }
void MainWindow::onDelayFeedbackChanged(double v) { mEngine->delay().setFeedback(static_cast<float>(v)); }
void MainWindow::onDelayMixChanged(double v)      { mEngine->delay().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Reverb slots
// ---------------------------------------------------------------------------

void MainWindow::onReverbEnabledToggled(bool en)  { mEngine->setReverbEnabled(en); }
void MainWindow::onReverbRoomChanged(double v)    { mEngine->reverb().setRoomSize(static_cast<float>(v)); }
void MainWindow::onReverbDampingChanged(double v) { mEngine->reverb().setDamping (static_cast<float>(v)); }
void MainWindow::onReverbWidthChanged(double v)   { mEngine->reverb().setWidth   (static_cast<float>(v)); }
void MainWindow::onReverbMixChanged(double v)     { mEngine->reverb().setMix     (static_cast<float>(v)); }


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
    mMeterIn ->setValue(toMeter(mEngine->inputPeak()));
    mMeterOut->setValue(toMeter(mEngine->outputPeak()));

    if (!mEngine->isRunning() && mLblJACKStatus->text().startsWith("JACK: connected"))
    {
        mLblJACKStatus->setText("JACK: disconnected");
        mLblJACKStatus->setStyleSheet("color: red;");
    }
}

void MainWindow::updateTuner()
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

void MainWindow::updateStatusLabels()
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
// Preset save / load
// ---------------------------------------------------------------------------

void MainWindow::onSavePreset()
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

    QJsonObject octave;
    octave["enabled"]  = mEngine->octaveEnabled();
    octave["upLevel"]  = static_cast<double>(mEngine->octave().upLevel());
    octave["downLevel"]= static_cast<double>(mEngine->octave().downLevel());
    obj["octave"] = octave;

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

    QJsonObject rvs;
    rvs["enabled"] = mEngine->reverseEnabled();
    rvs["length"]  = static_cast<double>(mEngine->reverse().length());
    rvs["mix"]     = static_cast<double>(mEngine->reverse().mix());
    obj["reverse"] = rvs;

    QJsonObject sweep;
    sweep["enabled"]     = mEngine->volumeSweepEnabled();
    sweep["attack"]      = static_cast<double>(mEngine->volumeSweep().attack());
    sweep["sensitivity"] = static_cast<double>(mEngine->volumeSweep().sensitivity());
    sweep["mix"]         = static_cast<double>(mEngine->volumeSweep().mix());
    obj["volumeSweep"] = sweep;

    QJsonObject wah;
    wah["enabled"]     = mEngine->dynamicWahEnabled();
    wah["sensitivity"] = static_cast<double>(mEngine->dynamicWah().sensitivity());
    wah["resonance"]   = static_cast<double>(mEngine->dynamicWah().resonance());
    wah["mix"]         = static_cast<double>(mEngine->dynamicWah().mix());
    obj["dynamicWah"] = wah;

    QJsonObject bit;
    bit["enabled"] = mEngine->bitCrusherEnabled();
    bit["bits"]    = static_cast<double>(mEngine->bitCrusher().bits());
    bit["mix"]     = static_cast<double>(mEngine->bitCrusher().mix());
    obj["bitCrusher"] = bit;

    obj["irEnabled"] = mEngine->irEnabled();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, "Error", "Cannot write preset:\n" + path);
        return;
    }
    f.write(QJsonDocument(obj).toJson());
}

void MainWindow::onLoadPreset()
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
    if (obj.contains("octave")) {
        auto o = obj["octave"].toObject();
        mSwOctaveEnable->setChecked(getB(o, "enabled",   false));
        mKnobOctUp     ->setValue(getD(o, "upLevel",   NAMConstants::kOctaveUpDefault));
        mKnobOctDown   ->setValue(getD(o, "downLevel", NAMConstants::kOctaveDownDefault));
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
    if (obj.contains("reverse")) {
        auto r = obj["reverse"].toObject();
        mSwReverseEnable->setChecked(getB(r, "enabled", false));
        mKnobRevLen ->setValue(getD(r, "length", NAMConstants::kReverseLenDefault));
        mKnobRevMixR->setValue(getD(r, "mix",    NAMConstants::kReverseMixDefault));
    }
    if (obj.contains("volumeSweep")) {
        auto s = obj["volumeSweep"].toObject();
        mSwSweepEnable ->setChecked(getB(s, "enabled",     false));
        mKnobSweepAtk  ->setValue(getD(s, "attack",      NAMConstants::kSweepAttackDefault));
        mKnobSweepSens ->setValue(getD(s, "sensitivity", NAMConstants::kSweepSensDefault));
        mKnobSweepMix  ->setValue(getD(s, "mix",         NAMConstants::kSweepMixDefault));
    }
    if (obj.contains("dynamicWah")) {
        auto w = obj["dynamicWah"].toObject();
        mSwWahEnable ->setChecked(getB(w, "enabled",     false));
        mKnobWahSens ->setValue(getD(w, "sensitivity", NAMConstants::kWahSensDefault));
        mKnobWahRes  ->setValue(getD(w, "resonance",   NAMConstants::kWahResDefault));
        mKnobWahMix  ->setValue(getD(w, "mix",         NAMConstants::kWahMixDefault));
    }
    if (obj.contains("bitCrusher")) {
        auto b = obj["bitCrusher"].toObject();
        mSwBitEnable ->setChecked(getB(b, "enabled", false));
        mKnobBitBits ->setValue(getD(b, "bits", NAMConstants::kBitCrushBitsDefault));
        mKnobBitMix  ->setValue(getD(b, "mix",  NAMConstants::kBitCrushMixDefault));
    }
    if (obj.contains("irEnabled"))
        mSwIREnable->setChecked(obj["irEnabled"].toBool(true));
}

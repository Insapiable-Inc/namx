#include "MainWindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFile>
#include <QFrame>

#include <cmath>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QProgressBar* makeVUBar()
{
    auto* b = new QProgressBar;
    b->setRange(0, 100);
    b->setValue(0);
    b->setTextVisible(false);
    b->setFixedHeight(10);
    b->setStyleSheet(
        "QProgressBar { border:1px solid #333; background:#222; border-radius:3px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #2a9d2a, stop:0.7 #d4a017, stop:1 #cc2222); border-radius:3px; }");
    return b;
}

static QFrame* makeSectionDivider(const QString& text)
{
    auto* w = new QFrame;
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 4, 0, 2);
    lay->setSpacing(6);
    auto* lbl = new QLabel(text);
    lbl->setStyleSheet("font-size:7pt; font-weight:bold; color:#666; text-transform:uppercase;");
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color:#333;");
    lay->addWidget(lbl);
    lay->addWidget(line, 1);
    return w;
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player");
    setMinimumSize(820, 600);

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
// Helper: build an effect row with ToggleSwitch, label, knobs, up/down buttons
// ---------------------------------------------------------------------------

QWidget* MainWindow::makeEffectRow(int /*unusedIndex*/,
                                   const QString& title,
                                   ToggleSwitch*& toggleOut,
                                   const std::initializer_list<KnobWidget*>& knobs,
                                   QWidget* extraWidget)
{
    auto* row = new QWidget;
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    // Up / Down reorder buttons — capture the stable row pointer, not the index
    auto* btnUp   = new QPushButton("▲");
    auto* btnDown = new QPushButton("▼");
    btnUp  ->setFixedSize(18, 18);
    btnDown->setFixedSize(18, 18);
    btnUp  ->setStyleSheet("font-size:7pt; padding:0;");
    btnDown->setStyleSheet("font-size:7pt; padding:0;");

    auto* reorderCol = new QWidget;
    auto* reorderLay = new QVBoxLayout(reorderCol);
    reorderLay->setContentsMargins(0, 0, 0, 0);
    reorderLay->setSpacing(1);
    reorderLay->addWidget(btnUp);
    reorderLay->addWidget(btnDown);
    lay->addWidget(reorderCol);

    connect(btnUp,   &QPushButton::clicked, this, [this, row]{ onMoveUp(row);   });
    connect(btnDown, &QPushButton::clicked, this, [this, row]{ onMoveDown(row); });

    // Toggle switch + label
    toggleOut = new ToggleSwitch;
    toggleOut->setCheckable(true);
    toggleOut->setChecked(false);
    auto* lbl = new QLabel(title);
    lbl->setStyleSheet("font-size:8pt; font-weight:bold; color:#ccc;");
    lbl->setFixedWidth(90);

    lay->addWidget(toggleOut);
    lay->addWidget(lbl);

    if (extraWidget)
        lay->addWidget(extraWidget);

    for (auto* k : knobs)
        lay->addWidget(k);

    lay->addStretch();
    return row;
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void MainWindow::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(10, 8, 10, 8);
    rootLayout->setSpacing(5);

    // ---- Title / preset buttons ----
    {
        auto* row = new QHBoxLayout;
        auto* title = new QLabel(
            "<b><span style='font-size:15pt;'>NAM</span>"
            "<span style='font-size:15pt; font-weight:normal;'> Player</span></b>");
        title->setTextFormat(Qt::RichText);

        mLblJACKStatus = new QLabel("JACK: connecting…");
        mLblJACKStatus->setStyleSheet("color:#aaa; font-size:8pt;");

        auto* btnSave = new QPushButton("Save Preset…");
        auto* btnLoad = new QPushButton("Load Preset…");
        btnSave->setFixedHeight(24);
        btnLoad->setFixedHeight(24);
        connect(btnSave, &QPushButton::clicked, this, &MainWindow::onSavePreset);
        connect(btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadPreset);

        row->addWidget(title);
        row->addWidget(mLblJACKStatus);
        row->addStretch();
        row->addWidget(btnSave);
        row->addWidget(btnLoad);
        rootLayout->addLayout(row);
    }

    // ---- Model loading ----
    {
        auto* grp = new QGroupBox("Model");
        auto* r   = new QHBoxLayout(grp);
        r->setContentsMargins(6, 4, 6, 4);

        mBtnLoadNAM   = new QPushButton("Load NAM…");
        mBtnUnloadNAM = new QPushButton("Unload");
        mBtnUnloadNAM->setEnabled(false);
        mLblNAMStatus = new QLabel("No model loaded");
        mLblNAMStatus->setStyleSheet("color:#888; font-style:italic;");

        r->addWidget(mBtnLoadNAM);
        r->addWidget(mBtnUnloadNAM);
        r->addWidget(mLblNAMStatus, 1);

        connect(mBtnLoadNAM,   &QPushButton::clicked, this, &MainWindow::onLoadNAM);
        connect(mBtnUnloadNAM, &QPushButton::clicked, this, &MainWindow::onUnloadNAM);

        rootLayout->addWidget(grp);
    }

    // ---- VU meters ----
    {
        auto* r = new QHBoxLayout;
        r->addWidget(new QLabel("In:"));
        mMeterIn = makeVUBar();
        r->addWidget(mMeterIn, 1);
        r->addSpacing(12);
        r->addWidget(new QLabel("Out:"));
        mMeterOut = makeVUBar();
        r->addWidget(mMeterOut, 1);
        rootLayout->addLayout(r);
    }

    // ---- Scrollable signal chain ----
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* chain       = new QWidget;
    auto* chainLayout = new QVBoxLayout(chain);
    chainLayout->setSpacing(2);
    chainLayout->setContentsMargins(0, 0, 0, 0);

    // --- Input gain ---
    {
        auto* grp = new QGroupBox("Input");
        auto* r   = new QHBoxLayout(grp);
        r->setContentsMargins(6, 3, 6, 3);
        mKnobInputGain = new KnobWidget("Gain", -20.0, 20.0, 0.0, "dB");
        connect(mKnobInputGain, &KnobWidget::valueChanged, this, &MainWindow::onInputGainChanged);
        r->addWidget(mKnobInputGain);
        r->addStretch();
        chainLayout->addWidget(grp);
    }

    // --- Pre-NAM section ---
    chainLayout->addWidget(makeSectionDivider("Pre-NAM  (mono)"));

    auto* preGrp = new QGroupBox;
    mPreChainLayout = new QVBoxLayout(preGrp);
    mPreChainLayout->setContentsMargins(4, 4, 4, 4);
    mPreChainLayout->setSpacing(2);

    // Tuner (special: no reorder row — it's always at the top for monitoring)
    {
        auto* row = new QWidget;
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(8);

        // placeholder reorder buttons (disabled for tuner — it's a monitor, not a tone effect)
        auto* btnUp   = new QPushButton("▲");
        auto* btnDown = new QPushButton("▼");
        btnUp  ->setFixedSize(18, 18);
        btnDown->setFixedSize(18, 18);
        btnUp  ->setStyleSheet("font-size:7pt; padding:0;");
        btnDown->setStyleSheet("font-size:7pt; padding:0;");
        auto* reorderCol = new QWidget;
        auto* reorderLay = new QVBoxLayout(reorderCol);
        reorderLay->setContentsMargins(0, 0, 0, 0);
        reorderLay->setSpacing(1);
        reorderLay->addWidget(btnUp);
        reorderLay->addWidget(btnDown);
        lay->addWidget(reorderCol);

        connect(btnUp,   &QPushButton::clicked, this, [this, row]{ onMoveUp(row);   });
        connect(btnDown, &QPushButton::clicked, this, [this, row]{ onMoveDown(row); });

        mSwTunerEnable = new ToggleSwitch;
        mSwTunerEnable->setCheckable(true);
        auto* lbl = new QLabel("Tuner");
        lbl->setStyleSheet("font-size:8pt; font-weight:bold; color:#ccc;");
        lbl->setFixedWidth(90);

        mSwTunerMute = new ToggleSwitch;
        mSwTunerMute->setCheckable(true);
        auto* muteLabel = new QLabel("Mute");
        muteLabel->setStyleSheet("font-size:7pt; color:#aaa;");

        mLblNote = new QLabel("--");
        mLblNote->setStyleSheet("font-size:16pt; font-weight:bold; min-width:52px;");
        mLblNote->setAlignment(Qt::AlignCenter);

        mLblCents = new QLabel("±0¢");
        mLblCents->setStyleSheet("font-size:9pt; color:#aaa; min-width:36px;");
        mLblCents->setAlignment(Qt::AlignCenter);

        mTunerBar = new QProgressBar;
        mTunerBar->setRange(0, 100);
        mTunerBar->setValue(50);
        mTunerBar->setTextVisible(false);
        mTunerBar->setFixedHeight(10);
        mTunerBar->setStyleSheet(
            "QProgressBar { border:1px solid #333; background:#222; border-radius:3px; }"
            "QProgressBar::chunk { background:#4caf50; border-radius:3px; }");

        lay->addWidget(mSwTunerEnable);
        lay->addWidget(lbl);
        lay->addWidget(mSwTunerMute);
        lay->addWidget(muteLabel);
        lay->addWidget(mLblNote);
        lay->addWidget(mLblCents);
        lay->addWidget(mTunerBar, 1);

        connect(mSwTunerEnable, &ToggleSwitch::toggled, this, &MainWindow::onTunerEnabledToggled);
        connect(mSwTunerMute,   &ToggleSwitch::toggled, this, &MainWindow::onTunerMuteToggled);

        mEffectRows.push_back({row, EffectId::Tuner, false});
        mPreChainLayout->addWidget(row);
    }

    // Noise Gate
    {
        mKnobGateThresh  = new KnobWidget("Thresh",  -80.0,  -10.0,  -40.0, "dB");
        mKnobGateAttack  = new KnobWidget("Attack",    0.1,  100.0,   10.0, "ms");
        mKnobGateRelease = new KnobWidget("Release",  10.0, 2000.0,  100.0, "ms");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Noise Gate", mSwGateEnable,
            {mKnobGateThresh, mKnobGateAttack, mKnobGateRelease});

        connect(mSwGateEnable,    &ToggleSwitch::toggled,      this, &MainWindow::onGateEnabledToggled);
        connect(mKnobGateThresh,  &KnobWidget::valueChanged,   this, &MainWindow::onGateThresholdChanged);
        connect(mKnobGateAttack,  &KnobWidget::valueChanged,   this, &MainWindow::onGateAttackChanged);
        connect(mKnobGateRelease, &KnobWidget::valueChanged,   this, &MainWindow::onGateReleaseChanged);

        mEffectRows.push_back({row, EffectId::Gate, false});
        mPreChainLayout->addWidget(row);
    }

    // Compressor
    {
        mKnobCompThresh  = new KnobWidget("Thresh",  -60.0,    0.0, -20.0, "dB");
        mKnobCompRatio   = new KnobWidget("Ratio",     1.0,   20.0,   4.0, ":1");
        mKnobCompAttack  = new KnobWidget("Attack",    0.1,  100.0,  10.0, "ms");
        mKnobCompRelease = new KnobWidget("Release",  10.0, 2000.0, 100.0, "ms");
        mKnobCompMakeup  = new KnobWidget("Makeup",    0.0,   30.0,   0.0, "dB");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Compressor", mSwCompEnable,
            {mKnobCompThresh, mKnobCompRatio, mKnobCompAttack, mKnobCompRelease, mKnobCompMakeup});

        connect(mSwCompEnable,    &ToggleSwitch::toggled,    this, &MainWindow::onCompEnabledToggled);
        connect(mKnobCompThresh,  &KnobWidget::valueChanged, this, &MainWindow::onCompThresholdChanged);
        connect(mKnobCompRatio,   &KnobWidget::valueChanged, this, &MainWindow::onCompRatioChanged);
        connect(mKnobCompAttack,  &KnobWidget::valueChanged, this, &MainWindow::onCompAttackChanged);
        connect(mKnobCompRelease, &KnobWidget::valueChanged, this, &MainWindow::onCompReleaseChanged);
        connect(mKnobCompMakeup,  &KnobWidget::valueChanged, this, &MainWindow::onCompMakeupChanged);

        mEffectRows.push_back({row, EffectId::Comp, false});
        mPreChainLayout->addWidget(row);
    }

    // Pitch Shift
    {
        mKnobPitchSemi = new KnobWidget("Semitones", -12.0, 12.0, 0.0, "st");
        mKnobPitchSemi->setIntegerDisplay(true);
        mKnobPitchMix  = new KnobWidget("Mix", 0.0, 100.0, 100.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Pitch Shift", mSwPitchEnable,
            {mKnobPitchSemi, mKnobPitchMix});

        connect(mSwPitchEnable,   &ToggleSwitch::toggled,    this, &MainWindow::onPitchEnabledToggled);
        connect(mKnobPitchSemi,   &KnobWidget::valueChanged, this, &MainWindow::onPitchSemitonesChanged);
        connect(mKnobPitchMix,    &KnobWidget::valueChanged, this, &MainWindow::onPitchMixChanged);

        mEffectRows.push_back({row, EffectId::PitchShift, false});
        mPreChainLayout->addWidget(row);
    }

    // Chorus 1
    {
        mKnobCh1Rate  = new KnobWidget("Rate",  0.1, 5.0, 1.0, "Hz");
        mKnobCh1Depth = new KnobWidget("Depth", 0.0, 100.0, 50.0, "%");
        mKnobCh1Mix   = new KnobWidget("Mix",   0.0, 100.0, 50.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Chorus", mSwChorus1Enable,
            {mKnobCh1Rate, mKnobCh1Depth, mKnobCh1Mix});

        connect(mSwChorus1Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus1EnabledToggled);
        connect(mKnobCh1Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus1RateChanged);
        connect(mKnobCh1Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus1DepthChanged);
        connect(mKnobCh1Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus1MixChanged);

        mEffectRows.push_back({row, EffectId::Chorus1, false});
        mPreChainLayout->addWidget(row);
    }

    // Flanger
    {
        mKnobFlRate     = new KnobWidget("Rate",     0.1,  5.0,  0.5, "Hz");
        mKnobFlDepth    = new KnobWidget("Depth",    0.0, 100.0, 50.0, "%");
        mKnobFlFeedback = new KnobWidget("Feedback", -99.0, 99.0, 0.0, "%");
        mKnobFlMix      = new KnobWidget("Mix",      0.0, 100.0, 50.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Flanger", mSwFlangerEnable,
            {mKnobFlRate, mKnobFlDepth, mKnobFlFeedback, mKnobFlMix});

        connect(mSwFlangerEnable, &ToggleSwitch::toggled,    this, &MainWindow::onFlangerEnabledToggled);
        connect(mKnobFlRate,      &KnobWidget::valueChanged, this, &MainWindow::onFlangerRateChanged);
        connect(mKnobFlDepth,     &KnobWidget::valueChanged, this, &MainWindow::onFlangerDepthChanged);
        connect(mKnobFlFeedback,  &KnobWidget::valueChanged, this, &MainWindow::onFlangerFeedbackChanged);
        connect(mKnobFlMix,       &KnobWidget::valueChanged, this, &MainWindow::onFlangerMixChanged);

        mEffectRows.push_back({row, EffectId::Flanger, false});
        mPreChainLayout->addWidget(row);
    }

    // Phaser
    {
        mKnobPhRate     = new KnobWidget("Rate",     0.1,  5.0, 1.0, "Hz");
        mKnobPhDepth    = new KnobWidget("Depth",    0.0, 100.0, 50.0, "%");
        mKnobPhFeedback = new KnobWidget("Feedback", 0.0,  99.0, 50.0, "%");
        mKnobPhMix      = new KnobWidget("Mix",      0.0, 100.0, 50.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Phaser", mSwPhaserEnable,
            {mKnobPhRate, mKnobPhDepth, mKnobPhFeedback, mKnobPhMix});

        connect(mSwPhaserEnable, &ToggleSwitch::toggled,    this, &MainWindow::onPhaserEnabledToggled);
        connect(mKnobPhRate,     &KnobWidget::valueChanged, this, &MainWindow::onPhaserRateChanged);
        connect(mKnobPhDepth,    &KnobWidget::valueChanged, this, &MainWindow::onPhaserDepthChanged);
        connect(mKnobPhFeedback, &KnobWidget::valueChanged, this, &MainWindow::onPhaserFeedbackChanged);
        connect(mKnobPhMix,      &KnobWidget::valueChanged, this, &MainWindow::onPhaserMixChanged);

        mEffectRows.push_back({row, EffectId::Phaser, false});
        mPreChainLayout->addWidget(row);
    }

    chainLayout->addWidget(preGrp);

    // --- NAM DSP (fixed, not reorderable) ---
    {
        auto* grp = new QGroupBox("NAM DSP");
        auto* r   = new QHBoxLayout(grp);
        r->setContentsMargins(6, 3, 6, 3);
        auto* info = new QLabel("Model:");
        info->setStyleSheet("color:#aaa; font-size:8pt;");
        r->addWidget(info);
        r->addWidget(mLblNAMStatus, 1);
        chainLayout->addWidget(grp);
    }

    // --- 3-band EQ (fixed after NAM) ---
    {
        mKnobLow    = new KnobWidget("Low",    -12.0, 12.0, 0.0, "dB");
        mKnobMid    = new KnobWidget("Mid",    -12.0, 12.0, 0.0, "dB");
        mKnobTreble = new KnobWidget("Treble", -12.0, 12.0, 0.0, "dB");

        auto* grp = new QGroupBox("EQ");
        auto* r   = new QHBoxLayout(grp);
        r->setContentsMargins(6, 3, 6, 3);
        r->addWidget(mKnobLow);
        r->addWidget(mKnobMid);
        r->addWidget(mKnobTreble);
        r->addStretch();

        connect(mKnobLow,    &KnobWidget::valueChanged, this, &MainWindow::onLowChanged);
        connect(mKnobMid,    &KnobWidget::valueChanged, this, &MainWindow::onMidChanged);
        connect(mKnobTreble, &KnobWidget::valueChanged, this, &MainWindow::onTrebleChanged);

        chainLayout->addWidget(grp);
    }

    // --- Post-NAM section ---
    chainLayout->addWidget(makeSectionDivider("Post-NAM  (stereo)"));

    auto* postGrp = new QGroupBox;
    mPostChainLayout = new QVBoxLayout(postGrp);
    mPostChainLayout->setContentsMargins(4, 4, 4, 4);
    mPostChainLayout->setSpacing(2);

    // Chorus 2
    {
        mKnobCh2Rate  = new KnobWidget("Rate",  0.1, 5.0, 1.0, "Hz");
        mKnobCh2Depth = new KnobWidget("Depth", 0.0, 100.0, 50.0, "%");
        mKnobCh2Mix   = new KnobWidget("Mix",   0.0, 100.0, 50.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Chorus", mSwChorus2Enable,
            {mKnobCh2Rate, mKnobCh2Depth, mKnobCh2Mix});

        connect(mSwChorus2Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus2EnabledToggled);
        connect(mKnobCh2Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus2RateChanged);
        connect(mKnobCh2Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus2DepthChanged);
        connect(mKnobCh2Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus2MixChanged);

        mEffectRows.push_back({row, EffectId::Chorus2, true});
        mPostChainLayout->addWidget(row);
    }

    // Tremolo
    {
        mKnobTrRate  = new KnobWidget("Rate",  0.1, 20.0,  5.0, "Hz");
        mKnobTrDepth = new KnobWidget("Depth", 0.0, 100.0, 50.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Tremolo", mSwTremoloEnable,
            {mKnobTrRate, mKnobTrDepth});

        connect(mSwTremoloEnable, &ToggleSwitch::toggled,    this, &MainWindow::onTremoloEnabledToggled);
        connect(mKnobTrRate,      &KnobWidget::valueChanged, this, &MainWindow::onTremoloRateChanged);
        connect(mKnobTrDepth,     &KnobWidget::valueChanged, this, &MainWindow::onTremoloDepthChanged);

        mEffectRows.push_back({row, EffectId::Tremolo, true});
        mPostChainLayout->addWidget(row);
    }

    // Delay
    {
        mKnobDelTime     = new KnobWidget("Time",     0.0, 2000.0, 250.0, "ms");
        mKnobDelFeedback = new KnobWidget("Feedback", 0.0,   99.0,  40.0, "%");
        mKnobDelMix      = new KnobWidget("Mix",      0.0,  100.0,  30.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Delay", mSwDelayEnable,
            {mKnobDelTime, mKnobDelFeedback, mKnobDelMix});

        connect(mSwDelayEnable,   &ToggleSwitch::toggled,    this, &MainWindow::onDelayEnabledToggled);
        connect(mKnobDelTime,     &KnobWidget::valueChanged, this, &MainWindow::onDelayTimeChanged);
        connect(mKnobDelFeedback, &KnobWidget::valueChanged, this, &MainWindow::onDelayFeedbackChanged);
        connect(mKnobDelMix,      &KnobWidget::valueChanged, this, &MainWindow::onDelayMixChanged);

        mEffectRows.push_back({row, EffectId::Delay, true});
        mPostChainLayout->addWidget(row);
    }

    // Reverb
    {
        mKnobRevRoom    = new KnobWidget("Room",    0.0, 100.0,  50.0, "%");
        mKnobRevDamping = new KnobWidget("Damping", 0.0, 100.0,  50.0, "%");
        mKnobRevWidth   = new KnobWidget("Width",   0.0, 100.0, 100.0, "%");
        mKnobRevMix     = new KnobWidget("Mix",     0.0, 100.0,  30.0, "%");

        int gi = static_cast<int>(mEffectRows.size());
        auto* row = makeEffectRow(gi, "Reverb", mSwReverbEnable,
            {mKnobRevRoom, mKnobRevDamping, mKnobRevWidth, mKnobRevMix});

        connect(mSwReverbEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onReverbEnabledToggled);
        connect(mKnobRevRoom,     &KnobWidget::valueChanged, this, &MainWindow::onReverbRoomChanged);
        connect(mKnobRevDamping,  &KnobWidget::valueChanged, this, &MainWindow::onReverbDampingChanged);
        connect(mKnobRevWidth,    &KnobWidget::valueChanged, this, &MainWindow::onReverbWidthChanged);
        connect(mKnobRevMix,      &KnobWidget::valueChanged, this, &MainWindow::onReverbMixChanged);

        mEffectRows.push_back({row, EffectId::Reverb, true});
        mPostChainLayout->addWidget(row);
    }

    // IR
    {
        auto* row = new QWidget;
        auto* lay = new QHBoxLayout(row);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(4);

        // Placeholder reorder col (same structure)
        auto* btnUp   = new QPushButton("▲");
        auto* btnDown = new QPushButton("▼");
        btnUp  ->setFixedSize(18, 18);
        btnDown->setFixedSize(18, 18);
        btnUp  ->setStyleSheet("font-size:7pt; padding:0;");
        btnDown->setStyleSheet("font-size:7pt; padding:0;");
        auto* reorderCol = new QWidget;
        auto* reorderLay = new QVBoxLayout(reorderCol);
        reorderLay->setContentsMargins(0, 0, 0, 0);
        reorderLay->setSpacing(1);
        reorderLay->addWidget(btnUp);
        reorderLay->addWidget(btnDown);
        lay->addWidget(reorderCol);

        connect(btnUp,   &QPushButton::clicked, this, [this, row]{ onMoveUp(row);   });
        connect(btnDown, &QPushButton::clicked, this, [this, row]{ onMoveDown(row); });

        mSwIREnable = new ToggleSwitch;
        mSwIREnable->setCheckable(true);
        mSwIREnable->setChecked(true);
        auto* lbl = new QLabel("IR (Cab Sim)");
        lbl->setStyleSheet("font-size:8pt; font-weight:bold; color:#ccc;");
        lbl->setFixedWidth(90);

        mBtnLoadIR   = new QPushButton("Load IR…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mLblIRStatus = new QLabel("No IR loaded");
        mLblIRStatus->setStyleSheet("color:#888; font-style:italic;");

        lay->addWidget(mSwIREnable);
        lay->addWidget(lbl);
        lay->addWidget(mBtnLoadIR);
        lay->addWidget(mBtnUnloadIR);
        lay->addWidget(mLblIRStatus, 1);

        connect(mSwIREnable,  &ToggleSwitch::toggled,   this, &MainWindow::onIREnabledToggled);
        connect(mBtnLoadIR,   &QPushButton::clicked,    this, &MainWindow::onLoadIR);
        connect(mBtnUnloadIR, &QPushButton::clicked,    this, &MainWindow::onUnloadIR);

        mEffectRows.push_back({row, EffectId::IR, true});
        mPostChainLayout->addWidget(row);
    }

    chainLayout->addWidget(postGrp);

    // --- Output gain ---
    {
        auto* grp = new QGroupBox("Output");
        auto* r   = new QHBoxLayout(grp);
        r->setContentsMargins(6, 3, 6, 3);
        mKnobOutputGain = new KnobWidget("Gain", -20.0, 20.0, 0.0, "dB");
        connect(mKnobOutputGain, &KnobWidget::valueChanged, this, &MainWindow::onOutputGainChanged);
        r->addWidget(mKnobOutputGain);
        r->addStretch();
        chainLayout->addWidget(grp);
    }

    chainLayout->addStretch();
    scroll->setWidget(chain);
    rootLayout->addWidget(scroll, 1);

    // Dark palette
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Window,     QColor(28, 28, 33));
    pal.setColor(QPalette::WindowText, QColor(210, 210, 215));
    pal.setColor(QPalette::Base,       QColor(22, 22, 26));
    pal.setColor(QPalette::Button,     QColor(50, 50, 58));
    pal.setColor(QPalette::ButtonText, QColor(210, 210, 215));
    pal.setColor(QPalette::Highlight,  QColor(42, 130, 218));
    setPalette(pal);
    central->setAutoFillBackground(true);
}

void MainWindow::setupAudio()
{
    if (mEngine->start("NAMPlayer"))
    {
        mLblJACKStatus->setText(
            QString("JACK: connected  |  %1 Hz  |  stereo out")
                .arg(static_cast<int>(mEngine->sampleRate())));
        mLblJACKStatus->setStyleSheet("color:#4caf50; font-size:8pt;");
    }
    else
    {
        mLblJACKStatus->setText("JACK: not connected (check jackd)");
        mLblJACKStatus->setStyleSheet("color:#f44336; font-size:8pt;");
    }
}

// ---------------------------------------------------------------------------
// Effect reorder
// ---------------------------------------------------------------------------

void MainWindow::rebuildEffectOrderFromUI()
{
    EffectOrder order;

    int preIdx  = 0;
    int postIdx = 0;

    for (const auto& er : mEffectRows)
    {
        if (!er.isPost)
        {
            if (preIdx < EffectId::PreNamCount)
                order.pre[preIdx++] = er.effectId;
        }
        else
        {
            if (postIdx < EffectId::PostNamCount)
                order.post[postIdx++] = er.effectId;
        }
    }

    mEngine->setEffectOrder(order);
}

void MainWindow::onMoveUp(QWidget* rowWidget)
{
    // Resolve current index by widget pointer — always correct even after previous reorders
    int gi = -1;
    for (int i = 0; i < static_cast<int>(mEffectRows.size()); ++i)
        if (mEffectRows[i].widget == rowWidget) { gi = i; break; }

    if (gi <= 0) return;

    const bool isPost     = mEffectRows[gi].isPost;
    const bool prevIsPost = mEffectRows[gi - 1].isPost;

    // Can't cross the pre/post boundary
    if (isPost != prevIsPost) return;

    std::swap(mEffectRows[gi], mEffectRows[gi - 1]);

    QVBoxLayout* lay = isPost ? mPostChainLayout : mPreChainLayout;
    int posA = lay->indexOf(mEffectRows[gi - 1].widget);  // after swap gi-1 has the moved row
    int posB = lay->indexOf(mEffectRows[gi].widget);

    if (posA >= 0 && posB >= 0 && posA != posB)
    {
        auto* item = lay->takeAt(posB);
        lay->insertItem(posA, item);
    }

    rebuildEffectOrderFromUI();
}

void MainWindow::onMoveDown(QWidget* rowWidget)
{
    int gi = -1;
    for (int i = 0; i < static_cast<int>(mEffectRows.size()); ++i)
        if (mEffectRows[i].widget == rowWidget) { gi = i; break; }

    if (gi < 0 || gi >= static_cast<int>(mEffectRows.size()) - 1) return;

    const bool isPost     = mEffectRows[gi].isPost;
    const bool nextIsPost = mEffectRows[gi + 1].isPost;

    if (isPost != nextIsPost) return;

    std::swap(mEffectRows[gi], mEffectRows[gi + 1]);

    QVBoxLayout* lay = isPost ? mPostChainLayout : mPreChainLayout;
    int posA = lay->indexOf(mEffectRows[gi].widget);      // after swap gi has the moved row
    int posB = lay->indexOf(mEffectRows[gi + 1].widget);

    if (posA >= 0 && posB >= 0 && posA != posB)
    {
        auto* item = lay->takeAt(posA);
        lay->insertItem(posB, item);
    }

    rebuildEffectOrderFromUI();
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
void MainWindow::onIREnabledToggled(bool en)    { mEngine->setIREnabled(en); }

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
// Pitch Shift slots
// ---------------------------------------------------------------------------

void MainWindow::onPitchEnabledToggled(bool en)  { mEngine->setPitchEnabled(en); }
void MainWindow::onPitchSemitonesChanged(double v) { mEngine->pitch().setSemitones(static_cast<float>(v)); }
void MainWindow::onPitchMixChanged(double v)       { mEngine->pitch().setMix      (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 1 slots
// ---------------------------------------------------------------------------

void MainWindow::onChorus1EnabledToggled(bool en) { mEngine->setChorus1Enabled(en); }
void MainWindow::onChorus1RateChanged(double v)   { mEngine->chorus1().setRate (static_cast<float>(v)); }
void MainWindow::onChorus1DepthChanged(double v)  { mEngine->chorus1().setDepth(static_cast<float>(v)); }
void MainWindow::onChorus1MixChanged(double v)    { mEngine->chorus1().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Flanger slots
// ---------------------------------------------------------------------------

void MainWindow::onFlangerEnabledToggled(bool en)   { mEngine->setFlangerEnabled(en); }
void MainWindow::onFlangerRateChanged(double v)     { mEngine->flanger().setRate    (static_cast<float>(v)); }
void MainWindow::onFlangerDepthChanged(double v)    { mEngine->flanger().setDepth   (static_cast<float>(v)); }
void MainWindow::onFlangerFeedbackChanged(double v) { mEngine->flanger().setFeedback(static_cast<float>(v)); }
void MainWindow::onFlangerMixChanged(double v)      { mEngine->flanger().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Phaser slots
// ---------------------------------------------------------------------------

void MainWindow::onPhaserEnabledToggled(bool en)    { mEngine->setPhaserEnabled(en); }
void MainWindow::onPhaserRateChanged(double v)      { mEngine->phaser().setRate    (static_cast<float>(v)); }
void MainWindow::onPhaserDepthChanged(double v)     { mEngine->phaser().setDepth   (static_cast<float>(v)); }
void MainWindow::onPhaserFeedbackChanged(double v)  { mEngine->phaser().setFeedback(static_cast<float>(v)); }
void MainWindow::onPhaserMixChanged(double v)       { mEngine->phaser().setMix     (static_cast<float>(v)); }

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
// Meters & tuner refresh
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
        mLblJACKStatus->setStyleSheet("color:#f44336; font-size:8pt;");
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
        mLblNAMStatus->setStyleSheet("color:#4caf50;");
    }
    else
    {
        mLblNAMStatus->setText("No model loaded");
        mLblNAMStatus->setStyleSheet("color:#888; font-style:italic;");
    }

    if (mEngine->isIRLoaded())
    {
        QString p = QString::fromStdString(mEngine->irPath());
        mLblIRStatus->setText(p.section('/', -1));
        mLblIRStatus->setStyleSheet("color:#4caf50;");
    }
    else
    {
        mLblIRStatus->setText("No IR loaded");
        mLblIRStatus->setStyleSheet("color:#888; font-style:italic;");
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

    // Asset paths — saved for reference / auto-reload on load
    obj["namModelPath"] = QString::fromStdString(mEngine->modelPath());
    obj["irPath"]       = QString::fromStdString(mEngine->irPath());

    obj["inputGain"]  = mEngine->inputGain();
    obj["outputGain"] = mEngine->outputGain();

    QJsonObject eq;
    eq["low"]    = mEngine->lowGain();
    eq["mid"]    = mEngine->midGain();
    eq["treble"] = mEngine->trebleGain();
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
    ch1["rate"]    = static_cast<double>(mEngine->chorus1().rate());
    ch1["depth"]   = static_cast<double>(mEngine->chorus1().depth());
    ch1["mix"]     = static_cast<double>(mEngine->chorus1().mix());
    obj["chorus1"] = ch1;

    QJsonObject fl;
    fl["enabled"]  = mEngine->flangerEnabled();
    fl["rate"]     = static_cast<double>(mEngine->flanger().rate());
    fl["depth"]    = static_cast<double>(mEngine->flanger().depth());
    fl["feedback"] = static_cast<double>(mEngine->flanger().feedback());
    fl["mix"]      = static_cast<double>(mEngine->flanger().mix());
    obj["flanger"] = fl;

    QJsonObject ph;
    ph["enabled"]  = mEngine->phaserEnabled();
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

    // Save effect order
    EffectOrder order = mEngine->currentOrder();
    QJsonArray preArr, postArr;
    for (int v : order.pre)  preArr.append(v);
    for (int v : order.post) postArr.append(v);
    obj["effectOrderPre"]  = preArr;
    obj["effectOrderPost"] = postArr;

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

    // Reload NAM model if path is saved and the file still exists
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

    // Reload IR if path is saved and the file still exists
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
        mKnobGateThresh ->setValue(getD(g, "threshold", -40.0));
        mKnobGateAttack ->setValue(getD(g, "attack",     10.0));
        mKnobGateRelease->setValue(getD(g, "release",   100.0));
    }
    if (obj.contains("compressor")) {
        auto c = obj["compressor"].toObject();
        mSwCompEnable   ->setChecked(getB(c, "enabled",   false));
        mKnobCompThresh ->setValue(getD(c, "threshold", -20.0));
        mKnobCompRatio  ->setValue(getD(c, "ratio",       4.0));
        mKnobCompAttack ->setValue(getD(c, "attack",     10.0));
        mKnobCompRelease->setValue(getD(c, "release",   100.0));
        mKnobCompMakeup ->setValue(getD(c, "makeup",      0.0));
    }
    if (obj.contains("pitchShift")) {
        auto p = obj["pitchShift"].toObject();
        mSwPitchEnable ->setChecked(getB(p, "enabled",   false));
        mKnobPitchSemi ->setValue(getD(p, "semitones",   0.0));
        mKnobPitchMix  ->setValue(getD(p, "mix",       100.0));
    }
    if (obj.contains("chorus1")) {
        auto c = obj["chorus1"].toObject();
        mSwChorus1Enable->setChecked(getB(c, "enabled", false));
        mKnobCh1Rate ->setValue(getD(c, "rate",   1.0));
        mKnobCh1Depth->setValue(getD(c, "depth", 50.0));
        mKnobCh1Mix  ->setValue(getD(c, "mix",   50.0));
    }
    if (obj.contains("flanger")) {
        auto fl = obj["flanger"].toObject();
        mSwFlangerEnable->setChecked(getB(fl, "enabled", false));
        mKnobFlRate     ->setValue(getD(fl, "rate",      0.5));
        mKnobFlDepth    ->setValue(getD(fl, "depth",    50.0));
        mKnobFlFeedback ->setValue(getD(fl, "feedback",  0.0));
        mKnobFlMix      ->setValue(getD(fl, "mix",      50.0));
    }
    if (obj.contains("phaser")) {
        auto p = obj["phaser"].toObject();
        mSwPhaserEnable ->setChecked(getB(p, "enabled", false));
        mKnobPhRate     ->setValue(getD(p, "rate",      1.0));
        mKnobPhDepth    ->setValue(getD(p, "depth",    50.0));
        mKnobPhFeedback ->setValue(getD(p, "feedback", 50.0));
        mKnobPhMix      ->setValue(getD(p, "mix",      50.0));
    }
    if (obj.contains("chorus2")) {
        auto c = obj["chorus2"].toObject();
        mSwChorus2Enable->setChecked(getB(c, "enabled", false));
        mKnobCh2Rate ->setValue(getD(c, "rate",   1.0));
        mKnobCh2Depth->setValue(getD(c, "depth", 50.0));
        mKnobCh2Mix  ->setValue(getD(c, "mix",   50.0));
    }
    if (obj.contains("tremolo")) {
        auto t = obj["tremolo"].toObject();
        mSwTremoloEnable->setChecked(getB(t, "enabled", false));
        mKnobTrRate ->setValue(getD(t, "rate",   5.0));
        mKnobTrDepth->setValue(getD(t, "depth", 50.0));
    }
    if (obj.contains("delay")) {
        auto d = obj["delay"].toObject();
        mSwDelayEnable  ->setChecked(getB(d, "enabled", false));
        mKnobDelTime    ->setValue(getD(d, "time",     250.0));
        mKnobDelFeedback->setValue(getD(d, "feedback",  40.0));
        mKnobDelMix     ->setValue(getD(d, "mix",       30.0));
    }
    if (obj.contains("reverb")) {
        auto r = obj["reverb"].toObject();
        mSwReverbEnable ->setChecked(getB(r, "enabled", false));
        mKnobRevRoom    ->setValue(getD(r, "roomSize",  50.0));
        mKnobRevDamping ->setValue(getD(r, "damping",   50.0));
        mKnobRevWidth   ->setValue(getD(r, "width",    100.0));
        mKnobRevMix     ->setValue(getD(r, "mix",       30.0));
    }
    if (obj.contains("irEnabled"))
    {
        bool en = obj["irEnabled"].toBool(true);
        mEngine->setIREnabled(en);
        mSwIREnable->setChecked(en);
    }

    // Restore effect order if present (best-effort: just push to engine)
    if (obj.contains("effectOrderPre") && obj.contains("effectOrderPost"))
    {
        EffectOrder order = mEngine->currentOrder();
        auto preArr  = obj["effectOrderPre"].toArray();
        auto postArr = obj["effectOrderPost"].toArray();
        for (int i = 0; i < std::min((int)preArr.size(),  (int)EffectId::PreNamCount);  ++i)
            order.pre[i]  = preArr[i].toInt(i);
        for (int i = 0; i < std::min((int)postArr.size(), (int)EffectId::PostNamCount); ++i)
            order.post[i] = postArr[i].toInt(i);
        mEngine->setEffectOrder(order);
        // Note: UI rows are not reordered visually on load — audio order is applied.
    }
}

#include "MainWindow.h"
#include "Constants.h"
#include "EffectShortcuts.h"
#include "Theme.h"
#include "TunerProcessor.h"

#include <QAction>
#include <QComboBox>
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
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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
        lbl->setAlignment(Qt::AlignHCenter);
        QPalette p = lbl->palette();
        p.setColor(QPalette::WindowText, QColor("#aab0bc"));
        lbl->setPalette(p);
        lay->addWidget(sw, 0, Qt::AlignHCenter);
        lay->addWidget(lbl);
        lay->addStretch();
        *outToggle = sw;
    }

    return w;
}

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
        "stop:0 #5cc8a8, stop:0.65 #f0c850, stop:0.9 #e85a4f, stop:1 #ff5252); }");
    return b;
}

// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player");
    // 15 cards (Input + 13 effects + Output) at min width 110, plus 4 dividers,
    // strip spacing and root margins. 1800 gives a small breathing margin.
    setMinimumSize(1800, 1000);
    resize(1800, 1000);

    setupUI();
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
    saveConnections();
    mEngine->stop();
}

// ---------------------------------------------------------------------------

QWidget* MainWindow::makeEffectColumn(const QString& title,
                                      ToggleSwitch*& toggleOut,
                                      const std::initializer_list<KnobWidget*>& knobs,
                                      ToggleSwitch** stereoToggleOut)
{
    auto* col = new QGroupBox(title);
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
        connect(presetMenu->addAction("Save Preset…"), &QAction::triggered, this, &MainWindow::onSavePreset);
        connect(presetMenu->addAction("Load Preset…"), &QAction::triggered, this, &MainWindow::onLoadPreset);
    }

    // ---- Top status row ----
    {
        auto* row = new QHBoxLayout;
        row->setContentsMargins(4, 4, 4, 4);
        row->setSpacing(10);

        auto* title = new QLabel("NAM Player");
        title->setObjectName("appTitle");

        mLblJACKStatus = new QLabel("JACK: connecting…");
        mLblJACKStatus->setObjectName("jackStatusErr");

        auto* btnSave = new QPushButton("Save Preset…");
        auto* btnLoad = new QPushButton("Load Preset…");
        connect(btnSave, &QPushButton::clicked, this, &MainWindow::onSavePreset);
        connect(btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadPreset);

        row->addWidget(title);
        row->addWidget(mLblJACKStatus);
        row->addStretch();
        row->addWidget(btnSave);
        row->addWidget(btnLoad);
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
        Theme::tagCard(namBox, Theme::Category::Amp);
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
        Theme::tagCard(irBox, Theme::Category::Cab);
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

    // =========================================================================
    // Phase 1: Create all effect columns and store them keyed by EffectBase*.
    //          Do NOT add to the layout here — layout order is driven by the
    //          audio chain in Phase 2 so the two stay in sync automatically.
    // =========================================================================
    std::unordered_map<EffectBase*, QWidget*> fxCols;

    // ---- Tuner ----
    {
        auto* col = new QGroupBox("Tuner");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::Neutral);
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
        mTunerBar->setOrientation(Qt::Vertical);
        mTunerBar->setRange(0, 100);
        mTunerBar->setValue(50);
        mTunerBar->setTextVisible(false);
        mTunerBar->setFixedWidth(14);
        mTunerBar->setMinimumHeight(60);
        mTunerBar->setStyleSheet("QProgressBar::chunk { background: #5cc8a8; border-radius: 2px; margin: 1px; }");

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
        fxCols[&mEngine->tuner()] = col;
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
        Theme::tagCard(col, Theme::Category::Dynamics);

        connect(mSwGateEnable,    &ToggleSwitch::toggled,    this, &MainWindow::onGateEnabledToggled);
        connect(mKnobGateThresh,  &KnobWidget::valueChanged, this, &MainWindow::onGateThresholdChanged);
        connect(mKnobGateAttack,  &KnobWidget::valueChanged, this, &MainWindow::onGateAttackChanged);
        connect(mKnobGateRelease, &KnobWidget::valueChanged, this, &MainWindow::onGateReleaseChanged);
        fxCols[&mEngine->gate()] = col;
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
        Theme::tagCard(col, Theme::Category::Dynamics);

        connect(mSwCompEnable,    &ToggleSwitch::toggled,    this, &MainWindow::onCompEnabledToggled);
        connect(mKnobCompThresh,  &KnobWidget::valueChanged, this, &MainWindow::onCompThresholdChanged);
        connect(mKnobCompRatio,   &KnobWidget::valueChanged, this, &MainWindow::onCompRatioChanged);
        connect(mKnobCompAttack,  &KnobWidget::valueChanged, this, &MainWindow::onCompAttackChanged);
        connect(mKnobCompRelease, &KnobWidget::valueChanged, this, &MainWindow::onCompReleaseChanged);
        connect(mKnobCompMakeup,  &KnobWidget::valueChanged, this, &MainWindow::onCompMakeupChanged);
        fxCols[&mEngine->comp()] = col;
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
        Theme::tagCard(col, Theme::Category::Filter);

        connect(mSwWahEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onDynamicWahEnabledToggled);
        connect(mKnobWahSens,  &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahSensChanged);
        connect(mKnobWahRes,   &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahResChanged);
        connect(mKnobWahMix,   &KnobWidget::valueChanged, this, &MainWindow::onDynamicWahMixChanged);
        fxCols[&mEngine->wah()] = col;
    }

    // ---- Amp (NAM) ----
    {
        auto* col = new QGroupBox("Amp");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::Amp);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);

        mSwNAMEnable = new ToggleSwitch;
        mSwNAMEnable->setCheckable(true);
        mSwNAMEnable->setChecked(true);
        lay->addWidget(mSwNAMEnable, 0, Qt::AlignHCenter);
        lay->addStretch();

        connect(mSwNAMEnable, &ToggleSwitch::toggled, this, &MainWindow::onNAMEnabledToggled);
        fxCols[&mEngine->nam()] = col;
    }

    // ---- EQ ----
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("EQ");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::EQ);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->setSpacing(4);

        mSwEQEnable = new ToggleSwitch;
        mSwEQEnable->setCheckable(true);
        mSwEQEnable->setChecked(true);
        lay->addWidget(mSwEQEnable, 0, Qt::AlignHCenter);
        connect(mSwEQEnable, &ToggleSwitch::toggled, this, &MainWindow::onEQEnabledToggled);

        // Reserve stereo slot so EQ's knob 1 aligns with neighbour cards.
        lay->addWidget(makeStereoSlot(nullptr));

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
        fxCols[&mEngine->eq()] = col;
    }

    // ---- Chorus 1 ----
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
        Theme::tagCard(col, Theme::Category::ModPre);

        connect(mSwChorus1Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus1EnabledToggled);
        connect(mSwChorus1Stereo, &ToggleSwitch::toggled,    this, &MainWindow::onChorus1StereoToggled);
        connect(mKnobCh1Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus1RateChanged);
        connect(mKnobCh1Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus1DepthChanged);
        connect(mKnobCh1Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus1MixChanged);
        fxCols[&mEngine->chorus1()] = col;
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
        Theme::tagCard(col, Theme::Category::ModPre);

        connect(mSwFlangerEnable, &ToggleSwitch::toggled,    this, &MainWindow::onFlangerEnabledToggled);
        connect(mSwFlangerStereo, &ToggleSwitch::toggled,    this, &MainWindow::onFlangerStereoToggled);
        connect(mKnobFlRate,      &KnobWidget::valueChanged, this, &MainWindow::onFlangerRateChanged);
        connect(mKnobFlDepth,     &KnobWidget::valueChanged, this, &MainWindow::onFlangerDepthChanged);
        connect(mKnobFlFeedback,  &KnobWidget::valueChanged, this, &MainWindow::onFlangerFeedbackChanged);
        connect(mKnobFlMix,       &KnobWidget::valueChanged, this, &MainWindow::onFlangerMixChanged);
        fxCols[&mEngine->flanger()] = col;
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
        Theme::tagCard(col, Theme::Category::ModPre);

        connect(mSwPhaserEnable, &ToggleSwitch::toggled,    this, &MainWindow::onPhaserEnabledToggled);
        connect(mSwPhaserStereo, &ToggleSwitch::toggled,    this, &MainWindow::onPhaserStereoToggled);
        connect(mKnobPhRate,     &KnobWidget::valueChanged, this, &MainWindow::onPhaserRateChanged);
        connect(mKnobPhDepth,    &KnobWidget::valueChanged, this, &MainWindow::onPhaserDepthChanged);
        connect(mKnobPhFeedback, &KnobWidget::valueChanged, this, &MainWindow::onPhaserFeedbackChanged);
        connect(mKnobPhMix,      &KnobWidget::valueChanged, this, &MainWindow::onPhaserMixChanged);
        fxCols[&mEngine->phaser()] = col;
    }

    // ---- Chorus 2 ----
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
        Theme::tagCard(col, Theme::Category::ModPost);

        connect(mSwChorus2Enable, &ToggleSwitch::toggled,    this, &MainWindow::onChorus2EnabledToggled);
        connect(mKnobCh2Rate,     &KnobWidget::valueChanged, this, &MainWindow::onChorus2RateChanged);
        connect(mKnobCh2Depth,    &KnobWidget::valueChanged, this, &MainWindow::onChorus2DepthChanged);
        connect(mKnobCh2Mix,      &KnobWidget::valueChanged, this, &MainWindow::onChorus2MixChanged);
        fxCols[&mEngine->chorus2()] = col;
    }

    // ---- Tremolo ----
    {
        using namespace NAMConstants;
        mKnobTrRate  = new KnobWidget("Rate",  kTremoloRateMin,  kTremoloRateMax,  kTremoloRateDefault,  "Hz");
        mKnobTrDepth = new KnobWidget("Depth", kTremoloDepthMin, kTremoloDepthMax, kTremoloDepthDefault, "%");
        mKnobTrRate->setScrollStep(0.1);
        mKnobTrDepth->setScrollStep(1.0);

        auto* col = makeEffectColumn("Tremolo", mSwTremoloEnable, {mKnobTrRate, mKnobTrDepth});
        Theme::tagCard(col, Theme::Category::ModPost);

        connect(mSwTremoloEnable, &ToggleSwitch::toggled,    this, &MainWindow::onTremoloEnabledToggled);
        connect(mKnobTrRate,      &KnobWidget::valueChanged, this, &MainWindow::onTremoloRateChanged);
        connect(mKnobTrDepth,     &KnobWidget::valueChanged, this, &MainWindow::onTremoloDepthChanged);
        fxCols[&mEngine->tremolo()] = col;
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
        Theme::tagCard(col, Theme::Category::Time);

        connect(mSwDelayEnable,   &ToggleSwitch::toggled,    this, &MainWindow::onDelayEnabledToggled);
        connect(mSwDelayStereo,   &ToggleSwitch::toggled,    this, &MainWindow::onDelayStereoToggled);
        connect(mKnobDelTime,     &KnobWidget::valueChanged, this, &MainWindow::onDelayTimeChanged);
        connect(mKnobDelFeedback, &KnobWidget::valueChanged, this, &MainWindow::onDelayFeedbackChanged);
        connect(mKnobDelMix,      &KnobWidget::valueChanged, this, &MainWindow::onDelayMixChanged);
        fxCols[&mEngine->delay()] = col;
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
        Theme::tagCard(col, Theme::Category::Time);

        connect(mSwReverbEnable,  &ToggleSwitch::toggled,    this, &MainWindow::onReverbEnabledToggled);
        connect(mKnobRevRoom,     &KnobWidget::valueChanged, this, &MainWindow::onReverbRoomChanged);
        connect(mKnobRevDamping,  &KnobWidget::valueChanged, this, &MainWindow::onReverbDampingChanged);
        connect(mKnobRevWidth,    &KnobWidget::valueChanged, this, &MainWindow::onReverbWidthChanged);
        connect(mKnobRevMix,      &KnobWidget::valueChanged, this, &MainWindow::onReverbMixChanged);
        fxCols[&mEngine->reverb()] = col;
    }

    // ---- IR toggle ----
    {
        auto* col = new QGroupBox("IR");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::Cab);
        auto* lay = new QVBoxLayout(col);
        lay->setContentsMargins(4, 6, 4, 4);

        mSwIREnable = new ToggleSwitch;
        mSwIREnable->setCheckable(true);
        mSwIREnable->setChecked(true);
        lay->addWidget(mSwIREnable, 0, Qt::AlignHCenter);
        lay->addStretch();

        connect(mSwIREnable, &ToggleSwitch::toggled, this, &MainWindow::onIREnabledToggled);
        fxCols[&mEngine->ir()] = col;
    }

    // =========================================================================
    // Phase 2: Lay out the strip in audio chain order.
    //          Dividers are inserted at semantic boundaries:
    //            - after Input (fixed)
    //            - before NAM (mono pre-amp → amp)
    //            - after EQ   (amp → stereo post-amp)
    //            - before Output (fixed)
    // =========================================================================

    // Input gain (not in the chain — always first)
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("Input");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::Neutral);
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

    // Keyboard shortcuts follow the same left-to-right order as the strip:
    // digits (1 2 3 … 0 - =) toggle each card's enable, Q W E … toggle the
    // stereo switches of the cards that have one.
    struct CardToggles { ToggleSwitch* enable; ToggleSwitch* stereo; };
    const std::unordered_map<EffectBase*, CardToggles> fxToggles = {
        {&mEngine->tuner(),   {mSwTunerEnable,   nullptr}},
        {&mEngine->gate(),    {mSwGateEnable,    nullptr}},
        {&mEngine->comp(),    {mSwCompEnable,    nullptr}},
        {&mEngine->wah(),     {mSwWahEnable,     nullptr}},
        {&mEngine->chorus1(), {mSwChorus1Enable, mSwChorus1Stereo}},
        {&mEngine->flanger(), {mSwFlangerEnable, mSwFlangerStereo}},
        {&mEngine->phaser(),  {mSwPhaserEnable,  mSwPhaserStereo}},
        {&mEngine->nam(),     {mSwNAMEnable,     nullptr}},
        {&mEngine->eq(),      {mSwEQEnable,      nullptr}},
        {&mEngine->chorus2(), {mSwChorus2Enable, nullptr}},
        {&mEngine->tremolo(), {mSwTremoloEnable, nullptr}},
        {&mEngine->delay(),   {mSwDelayEnable,   mSwDelayStereo}},
        {&mEngine->reverb(),  {mSwReverbEnable,  nullptr}},
        {&mEngine->ir(),      {mSwIREnable,      nullptr}},
    };
    EffectShortcuts shortcuts(this);

    // Chain-ordered effects — reordering mChain in AudioEngine reorders the UI here
    for (auto* fx : mEngine->chain())
    {
        if (fx == &mEngine->nam())
            addDivider();

        auto it = fxCols.find(fx);
        if (it != fxCols.end())
            stripLay->addWidget(it->second);

        auto tg = fxToggles.find(fx);
        if (tg != fxToggles.end())
            shortcuts.addEffect(tg->second.enable, tg->second.stereo);

        if (fx == &mEngine->eq())
            addDivider();
    }

    addDivider();

    // Output gain (not in the chain — always last)
    {
        using namespace NAMConstants;
        auto* col = new QGroupBox("Output");
        col->setAlignment(Qt::AlignHCenter);
        col->setMinimumWidth(110);
        Theme::tagCard(col, Theme::Category::Neutral);
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
    auto setJackStatus = [this](const QString& text, bool ok) {
        mLblJACKStatus->setText(text);
        mLblJACKStatus->setObjectName(ok ? "jackStatusOk" : "jackStatusErr");
        mLblJACKStatus->style()->unpolish(mLblJACKStatus);
        mLblJACKStatus->style()->polish(mLblJACKStatus);
    };

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
        setJackStatus(QString::fromStdString(mEngine->backendStatus()), true);
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        setJackStatus("Audio backend  \xe2\x97\x8f  not connected", false);
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
    auto setJackStatus = [this](const QString& text, bool ok) {
        mLblJACKStatus->setText(text);
        mLblJACKStatus->setObjectName(ok ? "jackStatusOk" : "jackStatusErr");
        mLblJACKStatus->style()->unpolish(mLblJACKStatus);
        mLblJACKStatus->style()->polish(mLblJACKStatus);
    };

    setJackStatus("Audio backend  \xe2\x97\x8f  switching devices…", false);

    const QString inId  = mCbxInputDevice->currentData().toString();
    const QString outId = mCbxOutputDevice->currentData().toString();

    if (mEngine->startWithDevices(inId.toStdString(), outId.toStdString())) {
        setJackStatus(QString::fromStdString(mEngine->backendStatus()), true);
        if (mEngine->supportsPortRouting())
            restoreConnections();
    } else {
        setJackStatus("Audio backend  \xe2\x97\x8f  not connected", false);
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
        QMessageBox::critical(this, "Error", "Failed to load IR:\n" + path);
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
// Gain / EQ
// ---------------------------------------------------------------------------

void MainWindow::onInputGainChanged(double dB)  { mEngine->setInputGain (static_cast<float>(dB)); }
void MainWindow::onOutputGainChanged(double dB) { mEngine->setOutputGain(static_cast<float>(dB)); }
void MainWindow::onLowChanged(double dB)        { mEngine->eq().setLowGain   (dB); }
void MainWindow::onMidChanged(double dB)        { mEngine->eq().setMidGain   (dB); }
void MainWindow::onTrebleChanged(double dB)     { mEngine->eq().setTrebleGain(dB); }
void MainWindow::onNAMEnabledToggled(bool en)   { mEngine->nam().setEnabled(en); }
void MainWindow::onEQEnabledToggled(bool en)    { mEngine->eq().setEnabled(en);  }
void MainWindow::onIREnabledToggled(bool en)    { mEngine->ir().setEnabled(en);  }

// ---------------------------------------------------------------------------
// Tuner
// ---------------------------------------------------------------------------

void MainWindow::onTunerEnabledToggled(bool en) { mEngine->tuner().setEnabled(en); }
void MainWindow::onTunerMuteToggled(bool en)    { mEngine->tuner().setMute(en);    }

// ---------------------------------------------------------------------------
// Noise gate
// ---------------------------------------------------------------------------

void MainWindow::onGateEnabledToggled(bool en)    { mEngine->gate().setEnabled(en); }
void MainWindow::onGateThresholdChanged(double v) { mEngine->gate().setThreshold(static_cast<float>(v)); }
void MainWindow::onGateAttackChanged(double v)    { mEngine->gate().setAttack   (static_cast<float>(v)); }
void MainWindow::onGateReleaseChanged(double v)   { mEngine->gate().setRelease  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Compressor
// ---------------------------------------------------------------------------

void MainWindow::onCompEnabledToggled(bool en)    { mEngine->comp().setEnabled(en); }
void MainWindow::onCompThresholdChanged(double v) { mEngine->comp().setThreshold(static_cast<float>(v)); }
void MainWindow::onCompRatioChanged(double v)     { mEngine->comp().setRatio    (static_cast<float>(v)); }
void MainWindow::onCompAttackChanged(double v)    { mEngine->comp().setAttack   (static_cast<float>(v)); }
void MainWindow::onCompReleaseChanged(double v)   { mEngine->comp().setRelease  (static_cast<float>(v)); }
void MainWindow::onCompMakeupChanged(double v)    { mEngine->comp().setMakeup   (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Dynamic Wah
// ---------------------------------------------------------------------------

void MainWindow::onDynamicWahEnabledToggled(bool en) { mEngine->wah().setEnabled(en); }
void MainWindow::onDynamicWahSensChanged(double v)   { mEngine->wah().setSensitivity(static_cast<float>(v)); }
void MainWindow::onDynamicWahResChanged(double v)    { mEngine->wah().setResonance  (static_cast<float>(v)); }
void MainWindow::onDynamicWahMixChanged(double v)    { mEngine->wah().setMix        (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 1
// ---------------------------------------------------------------------------

void MainWindow::onChorus1EnabledToggled(bool en) { mEngine->chorus1().setEnabled   (en); }
void MainWindow::onChorus1StereoToggled(bool en)  { mEngine->chorus1().setStereoMode(en); }
void MainWindow::onChorus1RateChanged(double v)   { mEngine->chorus1().setRate (static_cast<float>(v)); }
void MainWindow::onChorus1DepthChanged(double v)  { mEngine->chorus1().setDepth(static_cast<float>(v)); }
void MainWindow::onChorus1MixChanged(double v)    { mEngine->chorus1().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Flanger
// ---------------------------------------------------------------------------

void MainWindow::onFlangerEnabledToggled(bool en)   { mEngine->flanger().setEnabled   (en); }
void MainWindow::onFlangerStereoToggled(bool en)    { mEngine->flanger().setStereoMode(en); }
void MainWindow::onFlangerRateChanged(double v)     { mEngine->flanger().setRate    (static_cast<float>(v)); }
void MainWindow::onFlangerDepthChanged(double v)    { mEngine->flanger().setDepth   (static_cast<float>(v)); }
void MainWindow::onFlangerFeedbackChanged(double v) { mEngine->flanger().setFeedback(static_cast<float>(v)); }
void MainWindow::onFlangerMixChanged(double v)      { mEngine->flanger().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Phaser
// ---------------------------------------------------------------------------

void MainWindow::onPhaserEnabledToggled(bool en)   { mEngine->phaser().setEnabled   (en); }
void MainWindow::onPhaserStereoToggled(bool en)    { mEngine->phaser().setStereoMode(en); }
void MainWindow::onPhaserRateChanged(double v)     { mEngine->phaser().setRate    (static_cast<float>(v)); }
void MainWindow::onPhaserDepthChanged(double v)    { mEngine->phaser().setDepth   (static_cast<float>(v)); }
void MainWindow::onPhaserFeedbackChanged(double v) { mEngine->phaser().setFeedback(static_cast<float>(v)); }
void MainWindow::onPhaserMixChanged(double v)      { mEngine->phaser().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Chorus 2
// ---------------------------------------------------------------------------

void MainWindow::onChorus2EnabledToggled(bool en) { mEngine->chorus2().setEnabled(en); }
void MainWindow::onChorus2RateChanged(double v)   { mEngine->chorus2().setRate (static_cast<float>(v)); }
void MainWindow::onChorus2DepthChanged(double v)  { mEngine->chorus2().setDepth(static_cast<float>(v)); }
void MainWindow::onChorus2MixChanged(double v)    { mEngine->chorus2().setMix  (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Tremolo
// ---------------------------------------------------------------------------

void MainWindow::onTremoloEnabledToggled(bool en) { mEngine->tremolo().setEnabled(en); }
void MainWindow::onTremoloRateChanged(double v)   { mEngine->tremolo().setRate (static_cast<float>(v)); }
void MainWindow::onTremoloDepthChanged(double v)  { mEngine->tremolo().setDepth(static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Delay
// ---------------------------------------------------------------------------

void MainWindow::onDelayEnabledToggled(bool en)   { mEngine->delay().setEnabled   (en); }
void MainWindow::onDelayStereoToggled(bool en)    { mEngine->delay().setStereoMode(en); }
void MainWindow::onDelayTimeChanged(double v)     { mEngine->delay().setTime    (static_cast<float>(v)); }
void MainWindow::onDelayFeedbackChanged(double v) { mEngine->delay().setFeedback(static_cast<float>(v)); }
void MainWindow::onDelayMixChanged(double v)      { mEngine->delay().setMix     (static_cast<float>(v)); }

// ---------------------------------------------------------------------------
// Reverb
// ---------------------------------------------------------------------------

void MainWindow::onReverbEnabledToggled(bool en)  { mEngine->reverb().setEnabled(en); }
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

    if (!mEngine->isRunning() && mLblJACKStatus->objectName() == "jackStatusOk")
    {
        mLblJACKStatus->setText("Audio backend  ●  disconnected");
        mLblJACKStatus->setObjectName("jackStatusErr");
        mLblJACKStatus->style()->unpolish(mLblJACKStatus);
        mLblJACKStatus->style()->polish(mLblJACKStatus);
    }
}

void MainWindow::updateTuner()
{
    if (!mEngine->tuner().isEnabled()) return;

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
    if (!s.contains("connections/saved")) return;

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

    QJsonObject obj;
    obj["inputGain"]  = mEngine->inputGain();
    obj["outputGain"] = mEngine->outputGain();

    QJsonObject eq;
    eq["enabled"] = mEngine->eq().isEnabled();
    eq["low"]     = mEngine->eq().lowGainDB();
    eq["mid"]     = mEngine->eq().midGainDB();
    eq["treble"]  = mEngine->eq().trebleGainDB();
    obj["eq"] = eq;

    QJsonObject tuner;
    tuner["enabled"] = mEngine->tuner().isEnabled();
    tuner["mute"]    = mEngine->tuner().mute();
    obj["tuner"] = tuner;

    QJsonObject gate;
    gate["enabled"]   = mEngine->gate().isEnabled();
    gate["threshold"] = static_cast<double>(mEngine->gate().threshold());
    gate["attack"]    = static_cast<double>(mEngine->gate().attack());
    gate["release"]   = static_cast<double>(mEngine->gate().release());
    obj["noiseGate"] = gate;

    QJsonObject comp;
    comp["enabled"]   = mEngine->comp().isEnabled();
    comp["threshold"] = static_cast<double>(mEngine->comp().threshold());
    comp["ratio"]     = static_cast<double>(mEngine->comp().ratio());
    comp["attack"]    = static_cast<double>(mEngine->comp().attack());
    comp["release"]   = static_cast<double>(mEngine->comp().release());
    comp["makeup"]    = static_cast<double>(mEngine->comp().makeup());
    obj["compressor"] = comp;

    QJsonObject wah;
    wah["enabled"]     = mEngine->wah().isEnabled();
    wah["sensitivity"] = static_cast<double>(mEngine->wah().sensitivity());
    wah["resonance"]   = static_cast<double>(mEngine->wah().resonance());
    wah["mix"]         = static_cast<double>(mEngine->wah().mix());
    obj["dynamicWah"] = wah;

    QJsonObject ch1;
    ch1["enabled"] = mEngine->chorus1().isEnabled();
    ch1["stereo"]  = mEngine->chorus1().isStereoMode();
    ch1["rate"]    = static_cast<double>(mEngine->chorus1().rate());
    ch1["depth"]   = static_cast<double>(mEngine->chorus1().depth());
    ch1["mix"]     = static_cast<double>(mEngine->chorus1().mix());
    obj["chorus1"] = ch1;

    QJsonObject fl;
    fl["enabled"]  = mEngine->flanger().isEnabled();
    fl["stereo"]   = mEngine->flanger().isStereoMode();
    fl["rate"]     = static_cast<double>(mEngine->flanger().rate());
    fl["depth"]    = static_cast<double>(mEngine->flanger().depth());
    fl["feedback"] = static_cast<double>(mEngine->flanger().feedback());
    fl["mix"]      = static_cast<double>(mEngine->flanger().mix());
    obj["flanger"] = fl;

    QJsonObject ph;
    ph["enabled"]  = mEngine->phaser().isEnabled();
    ph["stereo"]   = mEngine->phaser().isStereoMode();
    ph["rate"]     = static_cast<double>(mEngine->phaser().rate());
    ph["depth"]    = static_cast<double>(mEngine->phaser().depth());
    ph["feedback"] = static_cast<double>(mEngine->phaser().feedback());
    ph["mix"]      = static_cast<double>(mEngine->phaser().mix());
    obj["phaser"] = ph;

    QJsonObject ch2;
    ch2["enabled"] = mEngine->chorus2().isEnabled();
    ch2["rate"]    = static_cast<double>(mEngine->chorus2().rate());
    ch2["depth"]   = static_cast<double>(mEngine->chorus2().depth());
    ch2["mix"]     = static_cast<double>(mEngine->chorus2().mix());
    obj["chorus2"] = ch2;

    QJsonObject tr;
    tr["enabled"] = mEngine->tremolo().isEnabled();
    tr["rate"]    = static_cast<double>(mEngine->tremolo().rate());
    tr["depth"]   = static_cast<double>(mEngine->tremolo().depth());
    obj["tremolo"] = tr;

    QJsonObject del;
    del["enabled"]  = mEngine->delay().isEnabled();
    del["stereo"]   = mEngine->delay().isStereoMode();
    del["time"]     = static_cast<double>(mEngine->delay().time());
    del["feedback"] = static_cast<double>(mEngine->delay().feedback());
    del["mix"]      = static_cast<double>(mEngine->delay().mix());
    obj["delay"] = del;

    QJsonObject rev;
    rev["enabled"]  = mEngine->reverb().isEnabled();
    rev["roomSize"] = static_cast<double>(mEngine->reverb().roomSize());
    rev["damping"]  = static_cast<double>(mEngine->reverb().damping());
    rev["width"]    = static_cast<double>(mEngine->reverb().width());
    rev["mix"]      = static_cast<double>(mEngine->reverb().mix());
    obj["reverb"] = rev;

    obj["irEnabled"] = mEngine->ir().isEnabled();

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

    QJsonObject obj = doc.object();

    auto getD = [](const QJsonObject& o, const QString& k, double def) -> double {
        return o.contains(k) ? o[k].toDouble(def) : def;
    };
    auto getB = [](const QJsonObject& o, const QString& k, bool def) -> bool {
        return o.contains(k) ? o[k].toBool(def) : def;
    };

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
    if (obj.contains("dynamicWah")) {
        auto w = obj["dynamicWah"].toObject();
        mSwWahEnable ->setChecked(getB(w, "enabled",     false));
        mKnobWahSens ->setValue(getD(w, "sensitivity", NAMConstants::kWahSensDefault));
        mKnobWahRes  ->setValue(getD(w, "resonance",   NAMConstants::kWahResDefault));
        mKnobWahMix  ->setValue(getD(w, "mix",         NAMConstants::kWahMixDefault));
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

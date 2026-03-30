#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QFrame>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , mEngine(std::make_unique<AudioEngine>())
{
    setWindowTitle("NAM Player");
    setMinimumSize(600, 420);

    setupUI();
    setupAudio();

    mMeterTimer = new QTimer(this);
    connect(mMeterTimer, &QTimer::timeout, this, &MainWindow::updateMeters);
    mMeterTimer->start(50); // 20 fps
}

MainWindow::~MainWindow()
{
    mMeterTimer->stop();
    mEngine->stop();
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

static QFrame* makeSeparator()
{
    auto* f = new QFrame;
    f->setFrameShape(QFrame::VLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

void MainWindow::setupUI()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 10, 12, 10);

    // ---- Title bar ----
    {
        auto* titleRow = new QHBoxLayout;
        auto* title = new QLabel("<b><span style='font-size:16pt;color:#dc7828;'>NAM</span>"
                                 "<span style='font-size:16pt;'> Player</span></b>");
        title->setTextFormat(Qt::RichText);
        auto* subtitle = new QLabel("Neural Amp Modeler  |  JACK Audio");
        subtitle->setStyleSheet("color: #888; font-size: 9pt;");
        titleRow->addWidget(title);
        titleRow->addStretch();
        titleRow->addWidget(subtitle);
        mainLayout->addLayout(titleRow);
    }

    // ---- JACK status ----
    {
        auto* row = new QHBoxLayout;
        mLblJACKStatus = new QLabel("JACK: connecting…");
        mLblJACKStatus->setStyleSheet("color: #aaa; font-size: 8pt;");
        row->addWidget(mLblJACKStatus);
        row->addStretch();
        mainLayout->addLayout(row);
    }

    // ---- Model / IR file loading ----
    {
        auto* grp = new QGroupBox("Files");
        auto* gl  = new QVBoxLayout(grp);

        // NAM row
        auto* namRow = new QHBoxLayout;
        mBtnLoadNAM   = new QPushButton("Load NAM…");
        mBtnUnloadNAM = new QPushButton("Unload");
        mBtnUnloadNAM->setEnabled(false);
        mLblNAMStatus = new QLabel("No model loaded");
        mLblNAMStatus->setStyleSheet("color: #888; font-style: italic;");
        mLblNAMStatus->setMinimumWidth(180);
        namRow->addWidget(new QLabel("Model:"));
        namRow->addWidget(mBtnLoadNAM);
        namRow->addWidget(mBtnUnloadNAM);
        namRow->addWidget(mLblNAMStatus, 1);
        gl->addLayout(namRow);

        // IR row
        auto* irRow = new QHBoxLayout;
        mBtnLoadIR   = new QPushButton("Load IR…");
        mBtnUnloadIR = new QPushButton("Unload");
        mBtnUnloadIR->setEnabled(false);
        mChkIREnable = new QCheckBox("Enable");
        mChkIREnable->setChecked(true);
        mLblIRStatus = new QLabel("No IR loaded");
        mLblIRStatus->setStyleSheet("color: #888; font-style: italic;");
        mLblIRStatus->setMinimumWidth(180);
        irRow->addWidget(new QLabel("Cab IR:"));
        irRow->addWidget(mBtnLoadIR);
        irRow->addWidget(mBtnUnloadIR);
        irRow->addWidget(mChkIREnable);
        irRow->addWidget(mLblIRStatus, 1);
        gl->addLayout(irRow);

        mainLayout->addWidget(grp);
    }

    // ---- Knobs row ----
    {
        auto* grp    = new QGroupBox("Signal Chain");
        auto* kRow   = new QHBoxLayout(grp);
        kRow->setSpacing(4);

        mKnobInputGain  = new KnobWidget("Input",   -20.0, 20.0,  0.0, "dB");
        mKnobLow        = new KnobWidget("Low",     -12.0, 12.0,  0.0, "dB");
        mKnobMid        = new KnobWidget("Mid",     -12.0, 12.0,  0.0, "dB");
        mKnobTreble     = new KnobWidget("Treble",  -12.0, 12.0,  0.0, "dB");
        mKnobOutputGain = new KnobWidget("Output",  -20.0, 20.0,  0.0, "dB");

        kRow->addWidget(mKnobInputGain);
        kRow->addWidget(makeSeparator());
        kRow->addStretch();
        kRow->addWidget(mKnobLow);
        kRow->addWidget(mKnobMid);
        kRow->addWidget(mKnobTreble);
        kRow->addStretch();
        kRow->addWidget(makeSeparator());
        kRow->addWidget(mKnobOutputGain);

        mainLayout->addWidget(grp);
    }

    // ---- VU meters ----
    {
        auto* meterRow = new QHBoxLayout;
        meterRow->addWidget(new QLabel("In:"));
        mMeterIn = new QProgressBar;
        mMeterIn->setRange(0, 100);
        mMeterIn->setValue(0);
        mMeterIn->setTextVisible(false);
        mMeterIn->setFixedHeight(10);
        mMeterIn->setStyleSheet(
            "QProgressBar { border: 1px solid #333; background: #222; border-radius:3px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #2a9d2a, stop:0.7 #d4a017, stop:1 #cc2222); border-radius:3px; }");
        meterRow->addWidget(mMeterIn, 1);

        meterRow->addSpacing(16);
        meterRow->addWidget(new QLabel("Out:"));
        mMeterOut = new QProgressBar;
        mMeterOut->setRange(0, 100);
        mMeterOut->setValue(0);
        mMeterOut->setTextVisible(false);
        mMeterOut->setFixedHeight(10);
        mMeterOut->setStyleSheet(
            "QProgressBar { border: 1px solid #333; background: #222; border-radius:3px; }"
            "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #2a9d2a, stop:0.7 #d4a017, stop:1 #cc2222); border-radius:3px; }");
        meterRow->addWidget(mMeterOut, 1);
        mainLayout->addLayout(meterRow);
    }

    // ---- Wire signals ----
    connect(mBtnLoadNAM,  &QPushButton::clicked, this, &MainWindow::onLoadNAM);
    connect(mBtnUnloadNAM,&QPushButton::clicked, this, &MainWindow::onUnloadNAM);
    connect(mBtnLoadIR,   &QPushButton::clicked, this, &MainWindow::onLoadIR);
    connect(mBtnUnloadIR, &QPushButton::clicked, this, &MainWindow::onUnloadIR);
    connect(mChkIREnable, &QCheckBox::toggled,   this, &MainWindow::onIREnabledToggled);

    connect(mKnobInputGain,  &KnobWidget::valueChanged, this, &MainWindow::onInputGainChanged);
    connect(mKnobOutputGain, &KnobWidget::valueChanged, this, &MainWindow::onOutputGainChanged);
    connect(mKnobLow,    &KnobWidget::valueChanged, this, &MainWindow::onLowChanged);
    connect(mKnobMid,    &KnobWidget::valueChanged, this, &MainWindow::onMidChanged);
    connect(mKnobTreble, &KnobWidget::valueChanged, this, &MainWindow::onTrebleChanged);

    // Dark palette
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Window,     QColor(28, 28, 33));
    pal.setColor(QPalette::WindowText, QColor(210, 210, 215));
    pal.setColor(QPalette::Base,       QColor(22, 22, 26));
    pal.setColor(QPalette::Button,     QColor(50, 50, 58));
    pal.setColor(QPalette::ButtonText, QColor(210, 210, 215));
    setPalette(pal);
    central->setAutoFillBackground(true);
}

void MainWindow::setupAudio()
{
    if (mEngine->start("NAMPlayer"))
    {
        mLblJACKStatus->setText(
            QString("JACK: connected  |  %1 Hz").arg(static_cast<int>(mEngine->sampleRate())));
        mLblJACKStatus->setStyleSheet("color: #4caf50; font-size: 8pt;");
    }
    else
    {
        mLblJACKStatus->setText("JACK: not connected (check jackd)");
        mLblJACKStatus->setStyleSheet("color: #f44336; font-size: 8pt;");
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void MainWindow::onLoadNAM()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load NAM Model", QString(),
        "NAM Files (*.nam);;All Files (*)");
    if (path.isEmpty())
        return;

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
        this, "Load Impulse Response", QString(),
        "WAV Files (*.wav);;All Files (*)");
    if (path.isEmpty())
        return;

    if (!mEngine->loadIR(path.toStdString()))
    {
        QMessageBox::critical(this, "Error", "Failed to load IR file:\n" + path);
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

void MainWindow::onInputGainChanged(double dB)  { mEngine->setInputGain(static_cast<float>(dB)); }
void MainWindow::onOutputGainChanged(double dB) { mEngine->setOutputGain(static_cast<float>(dB)); }
void MainWindow::onLowChanged(double dB)        { mEngine->setLowGain(static_cast<float>(dB)); }
void MainWindow::onMidChanged(double dB)        { mEngine->setMidGain(static_cast<float>(dB)); }
void MainWindow::onTrebleChanged(double dB)     { mEngine->setTrebleGain(static_cast<float>(dB)); }
void MainWindow::onIREnabledToggled(bool en)    { mEngine->setIREnabled(en); }

void MainWindow::updateMeters()
{
    // Convert peak linear to a 0-100 scale (dB -60 to 0)
    auto toMeter = [](float peak) -> int {
        if (peak <= 0.0f) return 0;
        float dB = 20.0f * std::log10(peak);
        // map [-60, 0] dB to [0, 100]
        int v = static_cast<int>((dB + 60.0f) / 60.0f * 100.0f);
        return std::max(0, std::min(100, v));
    };

    mMeterIn->setValue(toMeter(mEngine->inputPeak()));
    mMeterOut->setValue(toMeter(mEngine->outputPeak()));

    // Also update JACK status if engine stopped
    if (!mEngine->isRunning() &&
        mLblJACKStatus->text().startsWith("JACK: connected"))
    {
        mLblJACKStatus->setText("JACK: disconnected");
        mLblJACKStatus->setStyleSheet("color: #f44336; font-size: 8pt;");
    }
}

void MainWindow::updateStatusLabels()
{
    if (mEngine->isModelLoaded())
    {
        QString p = QString::fromStdString(mEngine->modelPath());
        mLblNAMStatus->setText(p.section('/', -1).section('\\', -1));
        mLblNAMStatus->setStyleSheet("color: #4caf50;");
    }
    else
    {
        mLblNAMStatus->setText("No model loaded");
        mLblNAMStatus->setStyleSheet("color: #888; font-style: italic;");
    }

    if (mEngine->isIRLoaded())
    {
        QString p = QString::fromStdString(mEngine->irPath());
        mLblIRStatus->setText(p.section('/', -1).section('\\', -1));
        mLblIRStatus->setStyleSheet("color: #4caf50;");
    }
    else
    {
        mLblIRStatus->setText("No IR loaded");
        mLblIRStatus->setStyleSheet("color: #888; font-style: italic;");
    }
}

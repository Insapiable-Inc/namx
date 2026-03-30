#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

#include "AudioEngine.h"
#include "KnobWidget.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QCheckBox;
class QGroupBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onLoadNAM();
    void onUnloadNAM();
    void onLoadIR();
    void onUnloadIR();
    void onInputGainChanged(double dB);
    void onOutputGainChanged(double dB);
    void onLowChanged(double dB);
    void onMidChanged(double dB);
    void onTrebleChanged(double dB);
    void onIREnabledToggled(bool enabled);
    void updateMeters();

private:
    void setupUI();
    void setupAudio();
    void updateStatusLabels();

    std::unique_ptr<AudioEngine> mEngine;

    // File buttons
    QPushButton* mBtnLoadNAM    = nullptr;
    QPushButton* mBtnUnloadNAM  = nullptr;
    QPushButton* mBtnLoadIR     = nullptr;
    QPushButton* mBtnUnloadIR   = nullptr;
    QCheckBox*   mChkIREnable   = nullptr;

    // Status labels
    QLabel* mLblNAMStatus = nullptr;
    QLabel* mLblIRStatus  = nullptr;
    QLabel* mLblJACKStatus = nullptr;

    // Knobs
    KnobWidget* mKnobInputGain  = nullptr;
    KnobWidget* mKnobOutputGain = nullptr;
    KnobWidget* mKnobLow        = nullptr;
    KnobWidget* mKnobMid        = nullptr;
    KnobWidget* mKnobTreble     = nullptr;

    // VU meters
    QProgressBar* mMeterIn  = nullptr;
    QProgressBar* mMeterOut = nullptr;

    QTimer* mMeterTimer = nullptr;
};

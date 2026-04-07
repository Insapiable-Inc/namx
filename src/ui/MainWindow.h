#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

#include "AudioEngine.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

class QLabel;
class QPushButton;
class QProgressBar;

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

    void onNAMEnabledToggled(bool en);
    void onEQEnabledToggled(bool en);
    void onIREnabledToggled(bool en);

    void onTunerEnabledToggled(bool en);
    void onTunerMuteToggled(bool en);

    void onGateEnabledToggled(bool en);
    void onGateThresholdChanged(double v);
    void onGateAttackChanged(double v);
    void onGateReleaseChanged(double v);

    void onCompEnabledToggled(bool en);
    void onCompThresholdChanged(double v);
    void onCompRatioChanged(double v);
    void onCompAttackChanged(double v);
    void onCompReleaseChanged(double v);
    void onCompMakeupChanged(double v);

    void onDynamicWahEnabledToggled(bool en);
    void onDynamicWahSensChanged(double v);
    void onDynamicWahResChanged(double v);
    void onDynamicWahMixChanged(double v);

    void onChorus1EnabledToggled(bool en);
    void onChorus1StereoToggled(bool en);
    void onChorus1RateChanged(double v);
    void onChorus1DepthChanged(double v);
    void onChorus1MixChanged(double v);

    void onFlangerEnabledToggled(bool en);
    void onFlangerStereoToggled(bool en);
    void onFlangerRateChanged(double v);
    void onFlangerDepthChanged(double v);
    void onFlangerFeedbackChanged(double v);
    void onFlangerMixChanged(double v);

    void onPhaserEnabledToggled(bool en);
    void onPhaserStereoToggled(bool en);
    void onPhaserRateChanged(double v);
    void onPhaserDepthChanged(double v);
    void onPhaserFeedbackChanged(double v);
    void onPhaserMixChanged(double v);

    void onChorus2EnabledToggled(bool en);
    void onChorus2RateChanged(double v);
    void onChorus2DepthChanged(double v);
    void onChorus2MixChanged(double v);

    void onTremoloEnabledToggled(bool en);
    void onTremoloRateChanged(double v);
    void onTremoloDepthChanged(double v);

    void onDelayEnabledToggled(bool en);
    void onDelayStereoToggled(bool en);
    void onDelayTimeChanged(double v);
    void onDelayFeedbackChanged(double v);
    void onDelayMixChanged(double v);

    void onReverbEnabledToggled(bool en);
    void onReverbRoomChanged(double v);
    void onReverbDampingChanged(double v);
    void onReverbWidthChanged(double v);
    void onReverbMixChanged(double v);

    void onSavePreset();
    void onLoadPreset();

    void updateMeters();
    void updateTuner();

private:
    void setupUI();
    void setupAudio();
    void updateStatusLabels();

    QWidget* makeEffectColumn(const QString& title,
                              ToggleSwitch*& toggleOut,
                              const std::initializer_list<KnobWidget*>& knobs,
                              ToggleSwitch** stereoToggleOut = nullptr);

    std::unique_ptr<AudioEngine> mEngine;

    QPushButton* mBtnLoadNAM    = nullptr;
    QPushButton* mBtnUnloadNAM  = nullptr;
    QPushButton* mBtnLoadIR     = nullptr;
    QPushButton* mBtnUnloadIR   = nullptr;
    QLabel*      mLblNAMStatus  = nullptr;
    QLabel*      mLblIRStatus   = nullptr;
    QLabel*      mLblJACKStatus = nullptr;

    QProgressBar* mMeterIn  = nullptr;
    QProgressBar* mMeterOut = nullptr;

    QLabel*       mLblNote  = nullptr;
    QLabel*       mLblCents = nullptr;
    QProgressBar* mTunerBar = nullptr;

    KnobWidget* mKnobInputGain  = nullptr;
    KnobWidget* mKnobOutputGain = nullptr;

    ToggleSwitch* mSwEQEnable = nullptr;
    KnobWidget*   mKnobLow    = nullptr;
    KnobWidget*   mKnobMid    = nullptr;
    KnobWidget*   mKnobTreble = nullptr;

    ToggleSwitch* mSwTunerEnable = nullptr;
    ToggleSwitch* mSwTunerMute   = nullptr;

    ToggleSwitch* mSwGateEnable   = nullptr;
    KnobWidget*   mKnobGateThresh  = nullptr;
    KnobWidget*   mKnobGateAttack  = nullptr;
    KnobWidget*   mKnobGateRelease = nullptr;

    ToggleSwitch* mSwCompEnable   = nullptr;
    KnobWidget*   mKnobCompThresh  = nullptr;
    KnobWidget*   mKnobCompRatio   = nullptr;
    KnobWidget*   mKnobCompAttack  = nullptr;
    KnobWidget*   mKnobCompRelease = nullptr;
    KnobWidget*   mKnobCompMakeup  = nullptr;

    ToggleSwitch* mSwWahEnable  = nullptr;
    KnobWidget*   mKnobWahSens  = nullptr;
    KnobWidget*   mKnobWahRes   = nullptr;
    KnobWidget*   mKnobWahMix   = nullptr;

    ToggleSwitch* mSwChorus1Enable = nullptr;
    ToggleSwitch* mSwChorus1Stereo = nullptr;
    KnobWidget*   mKnobCh1Rate    = nullptr;
    KnobWidget*   mKnobCh1Depth   = nullptr;
    KnobWidget*   mKnobCh1Mix     = nullptr;

    ToggleSwitch* mSwFlangerEnable = nullptr;
    ToggleSwitch* mSwFlangerStereo = nullptr;
    KnobWidget*   mKnobFlRate      = nullptr;
    KnobWidget*   mKnobFlDepth     = nullptr;
    KnobWidget*   mKnobFlFeedback  = nullptr;
    KnobWidget*   mKnobFlMix       = nullptr;

    ToggleSwitch* mSwPhaserEnable = nullptr;
    ToggleSwitch* mSwPhaserStereo = nullptr;
    KnobWidget*   mKnobPhRate     = nullptr;
    KnobWidget*   mKnobPhDepth    = nullptr;
    KnobWidget*   mKnobPhFeedback = nullptr;
    KnobWidget*   mKnobPhMix      = nullptr;

    ToggleSwitch* mSwChorus2Enable = nullptr;
    KnobWidget*   mKnobCh2Rate    = nullptr;
    KnobWidget*   mKnobCh2Depth   = nullptr;
    KnobWidget*   mKnobCh2Mix     = nullptr;

    ToggleSwitch* mSwTremoloEnable = nullptr;
    KnobWidget*   mKnobTrRate     = nullptr;
    KnobWidget*   mKnobTrDepth    = nullptr;

    ToggleSwitch* mSwDelayEnable = nullptr;
    ToggleSwitch* mSwDelayStereo = nullptr;
    KnobWidget*   mKnobDelTime     = nullptr;
    KnobWidget*   mKnobDelFeedback = nullptr;
    KnobWidget*   mKnobDelMix      = nullptr;

    ToggleSwitch* mSwReverbEnable = nullptr;
    KnobWidget*   mKnobRevRoom    = nullptr;
    KnobWidget*   mKnobRevDamping = nullptr;
    KnobWidget*   mKnobRevWidth   = nullptr;
    KnobWidget*   mKnobRevMix     = nullptr;

    ToggleSwitch* mSwNAMEnable = nullptr;
    ToggleSwitch* mSwIREnable  = nullptr;

    QTimer* mMeterTimer = nullptr;
    QTimer* mTunerTimer = nullptr;
};

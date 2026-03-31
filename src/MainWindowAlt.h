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

/// Alternative main window layout: each effect is a column,
/// knobs within a given effect are stacked as rows.
class MainWindowAlt : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindowAlt(QWidget* parent = nullptr);
    ~MainWindowAlt() override;

private slots:
    // Files
    void onLoadNAM();
    void onUnloadNAM();
    void onLoadIR();
    void onUnloadIR();

    // Gain
    void onInputGainChanged(double dB);
    void onOutputGainChanged(double dB);

    // EQ
    void onLowChanged(double dB);
    void onMidChanged(double dB);
    void onTrebleChanged(double dB);

    // NAM
    void onNAMEnabledToggled(bool en);

    // IR
    void onIREnabledToggled(bool enabled);

    // Tuner
    void onTunerEnabledToggled(bool en);
    void onTunerMuteToggled(bool en);

    // Noise Gate
    void onGateEnabledToggled(bool en);
    void onGateThresholdChanged(double v);
    void onGateAttackChanged(double v);
    void onGateReleaseChanged(double v);

    // Compressor
    void onCompEnabledToggled(bool en);
    void onCompThresholdChanged(double v);
    void onCompRatioChanged(double v);
    void onCompAttackChanged(double v);
    void onCompReleaseChanged(double v);
    void onCompMakeupChanged(double v);

    // Pitch Shift
    void onPitchEnabledToggled(bool en);
    void onPitchSemitonesChanged(double v);
    void onPitchMixChanged(double v);

    // Chorus 1
    void onChorus1EnabledToggled(bool en);
    void onChorus1RateChanged(double v);
    void onChorus1DepthChanged(double v);
    void onChorus1MixChanged(double v);

    // Flanger
    void onFlangerEnabledToggled(bool en);
    void onFlangerRateChanged(double v);
    void onFlangerDepthChanged(double v);
    void onFlangerFeedbackChanged(double v);
    void onFlangerMixChanged(double v);

    // Phaser
    void onPhaserEnabledToggled(bool en);
    void onPhaserRateChanged(double v);
    void onPhaserDepthChanged(double v);
    void onPhaserFeedbackChanged(double v);
    void onPhaserMixChanged(double v);

    // Chorus 2
    void onChorus2EnabledToggled(bool en);
    void onChorus2RateChanged(double v);
    void onChorus2DepthChanged(double v);
    void onChorus2MixChanged(double v);

    // Tremolo
    void onTremoloEnabledToggled(bool en);
    void onTremoloRateChanged(double v);
    void onTremoloDepthChanged(double v);

    // Delay
    void onDelayEnabledToggled(bool en);
    void onDelayTimeChanged(double v);
    void onDelayFeedbackChanged(double v);
    void onDelayMixChanged(double v);

    // Reverb
    void onReverbEnabledToggled(bool en);
    void onReverbRoomChanged(double v);
    void onReverbDampingChanged(double v);
    void onReverbWidthChanged(double v);
    void onReverbMixChanged(double v);

    // Preset
    void onSavePreset();
    void onLoadPreset();

    // Meters & tuner
    void updateMeters();
    void updateTuner();

private:
    void setupUI();
    void setupAudio();
    void updateStatusLabels();

    /// Build a vertical effect column: enable toggle, title, then knobs as rows.
    QWidget* makeEffectColumn(const QString& title,
                              ToggleSwitch*& toggleOut,
                              const std::initializer_list<KnobWidget*>& knobs);

    std::unique_ptr<AudioEngine> mEngine;

    // File buttons & labels
    QPushButton* mBtnLoadNAM    = nullptr;
    QPushButton* mBtnUnloadNAM  = nullptr;
    QPushButton* mBtnLoadIR     = nullptr;
    QPushButton* mBtnUnloadIR   = nullptr;
    QLabel*      mLblNAMStatus  = nullptr;
    QLabel*      mLblIRStatus   = nullptr;
    QLabel*      mLblJACKStatus = nullptr;

    // VU meters
    QProgressBar* mMeterIn  = nullptr;
    QProgressBar* mMeterOut = nullptr;

    // Tuner
    QLabel*       mLblNote   = nullptr;
    QLabel*       mLblCents  = nullptr;
    QProgressBar* mTunerBar  = nullptr;

    // Gain
    KnobWidget* mKnobInputGain  = nullptr;
    KnobWidget* mKnobOutputGain = nullptr;

    // EQ
    KnobWidget* mKnobLow    = nullptr;
    KnobWidget* mKnobMid    = nullptr;
    KnobWidget* mKnobTreble = nullptr;

    // Tuner
    ToggleSwitch* mSwTunerEnable = nullptr;
    ToggleSwitch* mSwTunerMute   = nullptr;

    // Noise gate
    ToggleSwitch* mSwGateEnable   = nullptr;
    KnobWidget*   mKnobGateThresh  = nullptr;
    KnobWidget*   mKnobGateAttack  = nullptr;
    KnobWidget*   mKnobGateRelease = nullptr;

    // Compressor
    ToggleSwitch* mSwCompEnable    = nullptr;
    KnobWidget*   mKnobCompThresh   = nullptr;
    KnobWidget*   mKnobCompRatio    = nullptr;
    KnobWidget*   mKnobCompAttack   = nullptr;
    KnobWidget*   mKnobCompRelease  = nullptr;
    KnobWidget*   mKnobCompMakeup   = nullptr;

    // Pitch Shift
    ToggleSwitch* mSwPitchEnable    = nullptr;
    KnobWidget*   mKnobPitchSemi    = nullptr;
    KnobWidget*   mKnobPitchMix     = nullptr;

    // Chorus 1
    ToggleSwitch* mSwChorus1Enable = nullptr;
    KnobWidget*   mKnobCh1Rate     = nullptr;
    KnobWidget*   mKnobCh1Depth    = nullptr;
    KnobWidget*   mKnobCh1Mix      = nullptr;

    // Flanger
    ToggleSwitch* mSwFlangerEnable  = nullptr;
    KnobWidget*   mKnobFlRate       = nullptr;
    KnobWidget*   mKnobFlDepth      = nullptr;
    KnobWidget*   mKnobFlFeedback   = nullptr;
    KnobWidget*   mKnobFlMix        = nullptr;

    // Phaser
    ToggleSwitch* mSwPhaserEnable   = nullptr;
    KnobWidget*   mKnobPhRate       = nullptr;
    KnobWidget*   mKnobPhDepth      = nullptr;
    KnobWidget*   mKnobPhFeedback   = nullptr;
    KnobWidget*   mKnobPhMix        = nullptr;

    // Chorus 2
    ToggleSwitch* mSwChorus2Enable = nullptr;
    KnobWidget*   mKnobCh2Rate     = nullptr;
    KnobWidget*   mKnobCh2Depth    = nullptr;
    KnobWidget*   mKnobCh2Mix      = nullptr;

    // Tremolo
    ToggleSwitch* mSwTremoloEnable = nullptr;
    KnobWidget*   mKnobTrRate      = nullptr;
    KnobWidget*   mKnobTrDepth     = nullptr;

    // Delay
    ToggleSwitch* mSwDelayEnable    = nullptr;
    KnobWidget*   mKnobDelTime      = nullptr;
    KnobWidget*   mKnobDelFeedback  = nullptr;
    KnobWidget*   mKnobDelMix       = nullptr;

    // Reverb
    ToggleSwitch* mSwReverbEnable   = nullptr;
    KnobWidget*   mKnobRevRoom      = nullptr;
    KnobWidget*   mKnobRevDamping   = nullptr;
    KnobWidget*   mKnobRevWidth     = nullptr;
    KnobWidget*   mKnobRevMix       = nullptr;

    // NAM
    ToggleSwitch* mSwNAMEnable = nullptr;

    // IR
    ToggleSwitch* mSwIREnable = nullptr;

    QTimer* mMeterTimer = nullptr;
    QTimer* mTunerTimer = nullptr;
};

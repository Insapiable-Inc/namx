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
class QGroupBox;
class QScrollArea;
class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

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

    // Reorder
    void onMoveUp(int effectGlobalIndex);
    void onMoveDown(int effectGlobalIndex);

    // Preset
    void onSavePreset();
    void onLoadPreset();

    // Meter + tuner refresh
    void updateMeters();
    void updateTuner();

private:
    void setupUI();
    void setupAudio();
    void updateStatusLabels();
    void rebuildEffectOrderFromUI();

    // Build a standard effect group row with ToggleSwitch + knobs + reorder buttons.
    // effectGlobalIndex is the position in mEffectWidgets used for reorder buttons.
    QWidget* makeEffectRow(int effectGlobalIndex,
                           const QString& title,
                           ToggleSwitch*& toggleOut,
                           const std::initializer_list<KnobWidget*>& knobs,
                           QWidget* extraWidget = nullptr);

    std::unique_ptr<AudioEngine> mEngine;

    // All draggable effect rows in signal-chain order (pre-NAM then post-NAM).
    // Index 0..PreNamCount-1 = pre-NAM, Index PreNamCount..end = post-NAM
    // Matches EffectId::PreNam and EffectId::PostNam ordering initially.
    struct EffectRow {
        QWidget* widget = nullptr;  // the row QWidget
        int      effectId = 0;      // EffectId enum value within its chain
        bool     isPost = false;    // false = pre-NAM, true = post-NAM
    };
    std::vector<EffectRow> mEffectRows;

    QVBoxLayout* mPreChainLayout  = nullptr;  // layout holding pre-NAM rows
    QVBoxLayout* mPostChainLayout = nullptr;  // layout holding post-NAM rows

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

    // Tuner display
    QLabel* mLblNote   = nullptr;
    QLabel* mLblCents  = nullptr;
    QProgressBar* mTunerBar = nullptr;

    // Gain knobs
    KnobWidget* mKnobInputGain  = nullptr;
    KnobWidget* mKnobOutputGain = nullptr;

    // EQ knobs
    KnobWidget* mKnobLow    = nullptr;
    KnobWidget* mKnobMid    = nullptr;
    KnobWidget* mKnobTreble = nullptr;

    // Tuner controls
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

    // IR
    ToggleSwitch* mSwIREnable = nullptr;

    QTimer* mMeterTimer  = nullptr;
    QTimer* mTunerTimer  = nullptr;
};

#pragma once

#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "KnobWidget.h"
#include "ToggleSwitch.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QComboBox;

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

    void onSavePreset();
    void onLoadPreset();

    void onInputDeviceChanged(int index);
    void onOutputDeviceChanged(int index);
    void populateDeviceDropdowns();
    void restartAudioWithCurrentDevices();

    void updateMeters();
    void updateTuner();

private:
    void setupUI();
    void setupAudio();
    void setAudioStatus(const QString& text, bool ok);
    void restoreLastFiles();
    void restoreConnections();
    void saveConnections();
    void updateStatusLabels();

    QWidget* makeEffectColumn(const QString& title,
                              ToggleSwitch*& toggleOut,
                              const std::initializer_list<KnobWidget*>& knobs,
                              ToggleSwitch** stereoToggleOut = nullptr);

    // --- Parameter registry -------------------------------------------------
    // One entry per user-facing parameter. Registration (registerParameters)
    // wires the widget to the engine; the same table then drives preset
    // save/load, and later session persistence and MIDI mapping — so the
    // parameter list exists in exactly one place.
    struct ParamDesc {
        QString group;                    // JSON object holding the key; empty = top-level
        QString key;
        KnobWidget*   knob   = nullptr;   // exactly one of knob/toggle is set
        ToggleSwitch* toggle = nullptr;
        double knobDefault   = 0.0;       // applied when the key is absent from a present group
        bool   toggleDefault = false;
        bool   inPreset      = true;      // false: wired to the engine but not part of the v1 preset schema
    };
    void registerParameters();
    void knobParam(const QString& group, const QString& key, KnobWidget* knob,
                   const std::function<void(double)>& apply);
    void toggleParam(const QString& group, const QString& key, ToggleSwitch* sw,
                     const std::function<void(bool)>& apply, bool inPreset = true);

    std::vector<ParamDesc> mParams;

    std::unique_ptr<AudioEngine> mEngine;

    QPushButton* mBtnLoadNAM    = nullptr;
    QPushButton* mBtnUnloadNAM  = nullptr;
    QPushButton* mBtnLoadIR     = nullptr;
    QPushButton* mBtnUnloadIR   = nullptr;
    QLabel*      mLblNAMStatus  = nullptr;
    QLabel*      mLblIRStatus   = nullptr;
    QLabel*      mLblJACKStatus = nullptr;
    QPushButton* mBtnReconnect  = nullptr;

    QComboBox* mCbxInputDevice  = nullptr;
    QComboBox* mCbxOutputDevice = nullptr;
    bool       mDeviceChanging  = false;

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

    ToggleSwitch* mSwChorusEnable = nullptr;
    KnobWidget*   mKnobChRate    = nullptr;
    KnobWidget*   mKnobChDepth   = nullptr;
    KnobWidget*   mKnobChMix     = nullptr;

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

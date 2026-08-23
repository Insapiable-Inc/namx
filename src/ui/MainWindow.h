#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QTimer>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "AudioEngine.h"
#include "EffectShortcuts.h"
#include "KnobWidget.h"
#include "ParamRegistry.h"
#include "ToggleSwitch.h"

class QHBoxLayout;
class QLabel;
class QPushButton;
class QProgressBar;
class QStackedWidget;
class AudioDevicePanel;
class VUMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    // Drag-and-drop reordering of the effect strip (filter installed on the
    // effect cards and on the strip container).
    bool eventFilter(QObject* obj, QEvent* ev) override;

private slots:
    void onLoadNAM();
    void onUnloadNAM();
    void onLoadIR();
    void onUnloadIR();

    void onSavePreset();
    void onLoadPreset();
    void onResetAll();

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
                              QWidget*& editorOut,
                              ToggleSwitch** stereoToggleOut = nullptr);

    // --- Effect strip / chain reordering ---
    // Lays the cards out in the given order: dividers follow NAM/EQ.
    void layoutStrip(const std::vector<EffectBase*>& order);
    // layoutStrip(engine chain) + shortcuts re-assigned left to right.
    void rebuildEffectStrip();
    void selectEffectCard(EffectBase* effect);
    // Keep bypassed cards visually subordinate without disabling their controls.
    void updateCardBypassState(EffectBase* effect, bool enabled);
    // setChainOrder + strip rebuild; used by session restore, preset load, DnD.
    void applyChainOrder(const QStringList& ids);
    // Live drag preview: while a card is dragged the strip re-lays out to the
    // prospective order (engine untouched); drop commits it, cancel restores.
    void startCardDrag(QWidget* card);
    void updateDragPreview(const QPoint& pos);
    void commitDragPreview();
    void cancelDragPreview();
    // Card context menu: reset one effect's sound parameters (knobs + stereo)
    // to defaults; the enable toggle keeps its state.
    void resetCardToDefaults(QWidget* card);

    // --- Parameter registry (ParamDesc lives in ParamRegistry.h) ------------
    // Registration wires each widget to the engine; the same table then
    // drives preset save/load (PresetSerializer) and session persistence.
    void registerParameters();
    void knobParam(const QString& group, const QString& key, KnobWidget* knob,
                   const std::function<void(double)>& apply);
    void toggleParam(const QString& group, const QString& key, ToggleSwitch* sw,
                     const std::function<void(bool)>& apply);

    // Session persistence: same table, QSettings instead of a preset file.
    void saveSessionState();
    void restoreSessionState();
    void scheduleSessionSave();

    ParamRegistry mParams;

    std::unique_ptr<AudioEngine> mEngine;

    // Effect strip state
    struct CardToggles { ToggleSwitch* enable; ToggleSwitch* stereo; };
    std::unordered_map<EffectBase*, QWidget*>    mFxCols;
    std::unordered_map<EffectBase*, QWidget*>    mFxEditors;
    std::unordered_map<EffectBase*, CardToggles> mFxToggles;
    QWidget*     mStrip      = nullptr;
    QHBoxLayout* mStripLay   = nullptr;
    QWidget*     mInputCard  = nullptr;
    QWidget*     mOutputCard = nullptr;
    QStackedWidget* mEffectEditor = nullptr;
    EffectBase*  mSelectedEffect = nullptr;
    EffectShortcuts mShortcuts{this};

    // Drag state for card reordering
    QWidget* mDragCard = nullptr;
    QPoint   mDragStartPos;
    std::string              mDragId;      // effect ID being dragged; empty = no drag
    std::vector<std::string> mPreviewIds;  // strip order currently shown while dragging

    QPushButton* mBtnLoadNAM    = nullptr;
    QPushButton* mBtnUnloadNAM  = nullptr;
    QPushButton* mBtnLoadIR     = nullptr;
    QPushButton* mBtnUnloadIR   = nullptr;
    QLabel*      mLblNAMStatus  = nullptr;
    QLabel*      mLblIRStatus   = nullptr;
    QLabel*      mLblAudioStatus = nullptr;
    QPushButton* mBtnReconnect  = nullptr;

    AudioDevicePanel* mDevicePanel = nullptr;

    VUMeter* mMeterIn  = nullptr;
    VUMeter* mMeterOut = nullptr;

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
    QTimer* mSessionSaveTimer = nullptr;
};

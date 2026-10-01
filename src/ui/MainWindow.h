#pragma once

#include <QJsonObject>
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
#include "LibraryIndex.h"
#include "ParamRegistry.h"
#include "PresetSerializer.h"
#include "ToggleSwitch.h"

class QDockWidget;
class QDragEnterEvent;
class QDropEvent;
class QMimeData;
class QFrame;
class QHBoxLayout;
class QVBoxLayout;
class QLabel;
class QPushButton;
class QToolButton;
class AudioStatusBar;
class GainReductionMeter;
class LibraryDock;
class LibraryPanel;
class PresetBar;
class MidiInput;
class MidiMapper;
class QMenu;
class QShortcut;
class TempoBar;
class Tone3000Client;
class Tone3000Panel;
class TunerDisplay;
class VUMeter;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Command-line / file-manager arguments: .nam, .wav, .nampreset (a preset
    /// first, then a model/IR given alongside overrides the preset's own).
    void openPaths(const QStringList& paths);

protected:
    // Drag-and-drop reordering of the effect strip (filter installed on the
    // effect cards and on the strip container).
    bool eventFilter(QObject* obj, QEvent* ev) override;
    // .nam / .wav / .nampreset files dropped anywhere on the window load.
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;
    static bool hasLoadableUrls(const QMimeData* mime);
    // Meters run at display rate only while the window can be seen.
    void changeEvent(QEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private slots:
    // File dialogs — the one-off path; the Library is the primary one.
    void onImportNAM();
    void onUnloadNAM();
    void onImportIR();
    void onUnloadIR();

    void onSavePreset();   // overwrite the current file; Save As when untitled
    void onSavePresetAs();
    void onLoadPreset();
    void onImportPresets(); // copy files into the presets folder, load the last
    void onExportPreset();  // write a copy elsewhere; the current file is unchanged
    void onDeletePreset();
    void onLocatePreset();
    void onResetAll();

    void restartAudioWithCurrentDevices();

    void updateMeters();
    void updateStatus(); // status bar metrics + disconnect watchdog
    void updateTuner();

private:
    void setupUI();
    void setupAudio();
    void restoreLastFiles();
    void restoreConnections();
    void saveConnections();
    // Amplifier / Cabinet hero rows: file name + metadata line.
    void updateHeroCards();
    // Status bar, left: model / IR / TONE3000 sign-in at a glance.
    void updateRigStatus();

    // --- Loading (every path in: dialogs, library, drops, session, presets) ---
    // Return false on failure; `quiet` suppresses the error dialog.
    bool loadModelFile(const QString& path, bool quiet = false);
    bool loadIRFile(const QString& path, bool quiet = false);
    bool loadIRBFile(const QString& path, bool quiet = false); // Cabinet slot B
    bool loadPresetFile(const QString& path);
    // Registry values + chain order from a preset object (no files).
    PresetSerializer::Applied applyPresetParams(const QJsonObject& obj);
    void loadLibraryEntry(LibraryEntry::Kind kind, const QString& path);
    // ‹ › on the hero rows: next/previous entry in the library's visible order.
    void stepLibrary(LibraryEntry::Kind kind, int step);
    // Hero row scaffold: ‹ › Browse + origin mark + name + metadata labels.
    void makeHeroControls(QWidget* card, LibraryEntry::Kind kind,
                          QLabel*& nameOut, QLabel*& metaOut, QLabel*& originOut);
    // Preset references a TONE3000 model that isn't on disk: find it in the
    // library by model id, else offer to download it. Calls `then(path)`
    // with an empty path when nothing could be resolved.
    void resolveTone3000Model(int modelId, const QString& title,
                              std::function<void(const QString&)> then);

    // Row-card scaffold: [≡ grip | name label]. The caller appends its
    // controls to the card's QHBoxLayout.
    QFrame* makeRowCard(const QString& title);
    // Full effect row: scaffold + enable toggle + stereo slot + knobs.
    QWidget* makeEffectRow(const QString& title,
                           ToggleSwitch*& toggleOut,
                           const std::initializer_list<KnobWidget*>& knobs,
                           ToggleSwitch** stereoToggleOut = nullptr);

    // --- Tempo sync (Phase 15) ---------------------------------------------
    // A time-based knob (delay time, LFO rate) gets a Sync toggle and a
    // note-value knob shown in its place while synced. All three are registry
    // parameters; each change re-pushes the effective value through `push`,
    // as does a tempo change.
    struct SyncedParam
    {
        KnobWidget*   freeKnob;
        KnobWidget*   noteKnob;
        ToggleSwitch* sync;
        bool          isTime;     // ms (delay) vs Hz (LFO)
        double        min, max;   // engine range; synced values are clamped
        std::function<void(double)> push;
    };
    void addTempoSync(QWidget* card, KnobWidget* freeKnob, const QString& label, bool isTime,
                      int defaultNote, double min, double max, std::function<void(double)> push);
    SyncedParam& syncOf(KnobWidget* freeKnob);
    void pushSynced(KnobWidget* freeKnob);
    void setTempo(double bpm);

    // Second row of advanced controls under a card's main row, behind a
    // ▾ toggle at the row's right end. Starts at the knob column; expanded
    // state persists per card (QSettings ui/expandedCards).
    void addDetailRow(QWidget* card, const QList<QWidget*>& widgets);

    // --- Effect strip / chain reordering ---
    // Lays the cards out in the given order: dividers follow NAM/EQ.
    void layoutStrip(const std::vector<EffectBase*>& order);
    // layoutStrip(engine chain) + shortcuts re-assigned left to right.
    void rebuildEffectStrip();
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

    // --- Current preset: name, `*` state, A/B ---------------------------
    // Serialized state written by Save (registry + order + model/IR + the
    // TONE3000 linkage).
    QJsonObject currentPresetJson() const;
    bool writePresetFile(const QString& path);
    // Makes `path` the current preset with the live state as its baseline
    // (empty = untitled); resets A/B.
    void setCurrentPreset(const QString& path);
    // Recomputes the `*`: live state vs the baseline. Coalesced via a timer.
    void updatePresetState();
    void stepPreset(int step);
    void switchAb(int side);
    void copyAb();
    void restorePresetState();
    static void seedStarterPresets();

    // --- MIDI (Phase 16) ----------------------------------------------------
    // Every registry knob/toggle is a target (registered by knobParam /
    // toggleParam), plus the actions added here.
    void setupMidi(QMenu* midiMenu);
    QString targetLabelFor(QWidget* w, const QString& group, const QString& key) const;
    void addLearnEntries(const QString& targetId, QMenu* menu);
    void startLearn(const QString& targetId);
    void onLearnChanged();
    QWidget* widgetForTarget(const QString& targetId) const;

    // Session persistence: same table, QSettings instead of a preset file.
    void saveSessionState();
    void restoreSessionState();
    void scheduleSessionSave();

    ParamRegistry mParams;
    // Parameters created in setupUI that don't need a named member (the
    // Phase 17 advanced knobs): registered at the end of registerParameters.
    std::vector<std::function<void()>> mLateParams;
    KnobWidget* lateKnob(const QString& group, const QString& key, KnobWidget* k,
                         std::function<void(double)> apply);

    std::unique_ptr<AudioEngine> mEngine;

    // Effect strip state
    struct CardToggles { ToggleSwitch* enable; ToggleSwitch* stereo; };
    std::unordered_map<EffectBase*, QWidget*>    mFxCols;
    std::unordered_map<EffectBase*, CardToggles> mFxToggles;
    std::unordered_map<QWidget*, QWidget*>       mCardGrips; // card → ≡ drag handle
    QWidget*     mStrip      = nullptr;
    QVBoxLayout* mStripLay   = nullptr;
    QWidget*     mInputCard  = nullptr;
    QWidget*     mOutputCard = nullptr;
    EffectShortcuts mShortcuts{this};

    // Drag state for card reordering
    QWidget* mDragCard = nullptr;
    QPoint   mDragStartPos;
    std::string              mDragId;      // effect ID being dragged; empty = no drag
    std::vector<std::string> mPreviewIds;  // strip order currently shown while dragging

    QLabel*      mLblNAMStatus  = nullptr; // file name (accent when loaded)
    QLabel*      mLblNAMMeta    = nullptr; // architecture · rate · gear
    QLabel*      mLblIRStatus   = nullptr;
    QLabel*      mLblIRMeta     = nullptr;
    QLabel*      mLblIRBStatus  = nullptr; // Cabinet slot B (detail row)

    LibraryIndex*   mLibraryIndex = nullptr;
    Tone3000Client* mTone3000     = nullptr;
    LibraryDock*    mLibraryTabs  = nullptr;
    LibraryPanel*   mLibrary      = nullptr; // local tab (owned by mLibraryTabs)
    Tone3000Panel*  mT3kPanel     = nullptr; // community tab
    QDockWidget*    mLibraryDock  = nullptr;
    QLabel*         mLblNAMOrigin = nullptr; // T3K mark when the model came from TONE3000
    QLabel*         mLblIROrigin  = nullptr;

    AudioStatusBar* mAudioBar = nullptr;
    QLabel*         mLblRigStatus = nullptr;
    bool            mAudioOk  = false; // last known backend state (drives the
                                       // disconnect watchdog in updateMeters)

    VUMeter* mMeterIn  = nullptr;
    VUMeter* mMeterOut = nullptr;
    GainReductionMeter* mGrGate = nullptr;
    GainReductionMeter* mGrComp = nullptr;

    TunerDisplay* mTunerDisplay = nullptr;
    int           mTunerRefHz   = 440;

    KnobWidget* mKnobInputGain  = nullptr;
    KnobWidget* mKnobOutputGain = nullptr;

    ToggleSwitch* mSwEQEnable = nullptr;
    KnobWidget*   mKnobLow    = nullptr;
    KnobWidget*   mKnobMid    = nullptr;
    KnobWidget*   mKnobTreble = nullptr;

    ToggleSwitch* mSwTunerEnable = nullptr;

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
    ToggleSwitch* mSwChorusStereo = nullptr;
    KnobWidget*   mKnobChRate    = nullptr;
    KnobWidget*   mKnobChDepth   = nullptr;
    KnobWidget*   mKnobChMix     = nullptr;

    ToggleSwitch* mSwTremoloEnable = nullptr;
    ToggleSwitch* mSwTremoloStereo = nullptr;
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

    ToggleSwitch* mSwTransposeEnable = nullptr;
    ToggleSwitch* mSwDriveEnable = nullptr;
    KnobWidget*   mKnobDrive     = nullptr;
    KnobWidget*   mKnobDriveTone = nullptr;
    KnobWidget*   mKnobDriveLevel = nullptr;

    ToggleSwitch* mSwDoublerEnable = nullptr;
    KnobWidget*   mKnobDblSpread  = nullptr;
    KnobWidget*   mKnobDblMix     = nullptr;

    ToggleSwitch* mSwVolumeEnable = nullptr;
    KnobWidget*   mKnobVolPos     = nullptr;
    KnobWidget*   mKnobVolMid     = nullptr;

    ToggleSwitch* mSwLimiter   = nullptr; // Output card
    ToggleSwitch* mSwNAMStereo = nullptr; // Amplifier row: per-channel amp

    ToggleSwitch* mSwNAMEnable = nullptr;
    ToggleSwitch* mSwIREnable  = nullptr;

    void updateMeterTimer();

    QTimer* mMeterTimer  = nullptr; // ~60 fps, paused while minimized/hidden
    QTimer* mStatusTimer = nullptr; // 50 ms, always
    QTimer* mTunerTimer = nullptr;
    QTimer* mSessionSaveTimer = nullptr;

    TempoBar*   mTempoBar  = nullptr;
    double      mBpm       = 120.0;
    std::vector<SyncedParam> mSynced;
    PresetBar*  mPresetBar = nullptr;
    QString     mPresetPath;        // current preset file; empty = untitled
    QJsonObject mPresetBaseline;    // toJson() when it was loaded/saved
    QJsonObject mAb[2];             // A/B snapshots; empty = not taken yet
    int         mAbActive = 0;
    QTimer*     mPresetStateTimer = nullptr;

    std::unique_ptr<MidiInput> mMidi;
    MidiMapper* mMidiMapper   = nullptr;
    bool        mMidiOk       = false;
    QShortcut*  mLearnEsc     = nullptr;
    QWidget*    mLearnWidget  = nullptr; // highlighted while learning
    QString     mLearnTarget;
    QLabel*     mLblMidi      = nullptr; // status bar activity light
    QMenu*      mMidiMenu     = nullptr;
    QTimer*     mMidiBlink    = nullptr;
};

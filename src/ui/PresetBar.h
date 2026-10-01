#pragma once

#include <QFrame>
#include <QString>
#include <QStringList>

class LibraryIndex;
class QLabel;
class QMenu;
class QToolButton;

/// The preset strip above the rack: ‹ [name ▾] › with a `*` while the state
/// differs from the file, Save / Save As, quick slots 1–8, A/B compare and
/// the full Preset menu behind ⋯.
///
/// Display + intent only: the owner (MainWindow) loads, saves and compares
/// state, then reports back through setPreset / setAbState. The quick slots
/// are self-contained (QSettings `presets/slots`, Ctrl+1…8) and ask the
/// owner to load through presetChosen like the name menu does.
class PresetBar : public QFrame
{
    Q_OBJECT
public:
    static constexpr int kSlotCount = 8;

    PresetBar(const LibraryIndex& index, QMenu* presetMenu, QWidget* parent = nullptr);

    /// Current preset file (empty = untitled) and whether the live state
    /// differs from it.
    void setPreset(const QString& path, bool modified);
    /// Active A/B side (0 = A, 1 = B).
    void setAbState(int active);

signals:
    void presetChosen(const QString& path); // name menu or a quick slot
    void stepRequested(int step);           // ‹ ›
    void saveRequested();
    void saveAsRequested();
    void abSwitchRequested(int side);
    void abCopyRequested();                 // active side → the other one

private:
    void populatePresetMenu(QMenu* menu);
    void activateSlot(int i);
    void showSlotMenu(int i, const QPoint& globalPos);
    void updateSlots();

    const LibraryIndex& mIndex;
    QString      mPath;
    QToolButton* mName     = nullptr;
    QLabel*      mModified = nullptr;
    QToolButton* mSlots[kSlotCount]{};
    QStringList  mSlotPaths;   // kSlotCount entries, "" = empty slot
    QToolButton* mAb[2]{};
    QToolButton* mAbCopy = nullptr;
    int          mAbActive = 0;
};

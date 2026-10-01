#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;
class QThread;
class QTimer;
class LibraryScanWorker;

/// One file the library knows about. The kind-specific metadata (model
/// architecture, IR length, preset contents…) is flattened by the scanner
/// into `summary` (one line for the list) and `details` (tooltip / hero
/// card) so the UI never parses files itself.
struct LibraryEntry
{
    enum class Kind
    {
        Model,
        IR,
        Preset
    };

    Kind kind{Kind::Model};
    QString path; // absolute, QDir::cleanPath'd — the identity of the entry
    QString name; // file name without extension
    QString folder; // directory relative to its root; "" = the root itself
    qint64 size{0};
    qint64 mtimeMs{0};
    QString summary; // metadata line: "WaveNet · 48 kHz · Mesa Boogie"
    QString details; // multi-line, everything the file says about itself
    // TONE3000 origin from the `.t3k.json` sidecar written on download.
    int t3kToneId = 0;
    int t3kModelId = 0;
    QString t3kTitle, t3kCreator;
    bool fromTone3000() const {
        return t3kModelId != 0;
    }
};

/// Configured root folders per kind. A folder may appear under several kinds.
struct LibraryRoots
{
    QStringList models, irs, presets;

    /// ~/NAM for models and IRs, ~/Music/IRs, and the app's own presets dir.
    static LibraryRoots defaults();
    /// The folder presets are written to by default (first presets root).
    static QString defaultPresetDir();
    /// Roots with the TONE3000 download folder added to models and IRs — the
    /// scanner always covers it, the Folders dialog shows the configured ones.
    LibraryRoots withTone3000Dir() const;
};

/// The library index: a recursive scan of the roots on a worker thread, a
/// metadata cache so only new/changed files are parsed, and a
/// QFileSystemWatcher on every scanned directory so the list stays live.
/// Never blocks the GUI: entries() is a snapshot replaced when a scan lands.
class LibraryIndex : public QObject
{
    Q_OBJECT
public:
    explicit LibraryIndex(QObject* parent = nullptr);
    ~LibraryIndex() override;

    void setRoots(const LibraryRoots& roots); // persists + rescans
    const LibraryRoots& roots() const {
        return mRoots;
    }

    void rescan();
    bool isScanning() const {
        return mScanning;
    }

    const QList<LibraryEntry>& entries() const {
        return mEntries;
    }
    const LibraryEntry* find(const QString& path) const;
    /// Library file downloaded from TONE3000 with this model id, or null.
    const LibraryEntry* findTone3000Model(int modelId) const;
    bool hasTone3000Tone(int toneId) const;

    static LibraryEntry::Kind kindForPath(const QString& path, bool* ok = nullptr);
    /// Synchronous one-file description (files outside the library: imports,
    /// drops). Reads the file — a model parse is a few ms per MB.
    static LibraryEntry describe(const QString& path);

signals:
    void scanStarted();
    void entriesChanged();

private:
    void requestScan();
    void onScanFinished(QList<LibraryEntry> entries, QStringList dirs);

    LibraryRoots mRoots;
    QList<LibraryEntry> mEntries;
    QThread* mThread = nullptr;
    LibraryScanWorker* mWorker = nullptr;
    QFileSystemWatcher* mWatcher = nullptr;
    QTimer* mDebounce = nullptr; // coalesces watcher bursts
    bool mScanning = false;
    bool mRescanQueued = false;
    QString mCachePath;
};

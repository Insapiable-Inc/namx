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
    enum class Kind { Model, IR, Preset };

    Kind    kind{Kind::Model};
    QString path;     // absolute, QDir::cleanPath'd — the identity of the entry
    QString name;     // file name without extension
    QString folder;   // directory relative to its root; "" = the root itself
    qint64  size{0};
    qint64  mtimeMs{0};
    QString summary;  // metadata line: "WaveNet · 48 kHz · Mesa Boogie"
    QString details;  // multi-line, everything the file says about itself
};

/// Configured root folders per kind. A folder may appear under several kinds.
struct LibraryRoots
{
    QStringList models, irs, presets;

    /// ~/NAM for models and IRs, ~/Music/IRs, and the app's own presets dir.
    static LibraryRoots defaults();
    /// The folder presets are written to by default (first presets root).
    static QString defaultPresetDir();
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

    void                setRoots(const LibraryRoots& roots); // persists + rescans
    const LibraryRoots& roots() const { return mRoots; }

    void rescan();
    bool isScanning() const { return mScanning; }

    const QList<LibraryEntry>& entries() const { return mEntries; }
    const LibraryEntry*        find(const QString& path) const;

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

    LibraryRoots         mRoots;
    QList<LibraryEntry>  mEntries;
    QThread*             mThread  = nullptr;
    LibraryScanWorker*   mWorker  = nullptr;
    QFileSystemWatcher*  mWatcher = nullptr;
    QTimer*              mDebounce = nullptr; // coalesces watcher bursts
    bool                 mScanning = false;
    bool                 mRescanQueued = false;
    QString              mCachePath;
};

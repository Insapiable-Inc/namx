#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "Common.h"

class QFileSystemWatcher;
class QThread;
class QTimer;

namespace npl
{

class LibraryScanWorker;

/// One file the library knows about. The kind-specific metadata (model
/// architecture, IR length, preset contents…) is flattened by the scanner
/// into `summary` (one line for the list) and `details` (tooltip / hero
/// card) so the UI never parses files itself.
struct LibraryEntry
{
    enum class Kind
    {
        MODEL,
        IR,
        PRESET
    };

    Kind kind{Kind::MODEL};
    QString path; // absolute, QDir::cleanPath'd — the identity of the entry
    QString name; // file name without extension
    QString folder; // directory relative to its root; "" = the root itself
    qint64 size{0};
    qint64 mtime_ms{0};
    QString summary; // metadata line: "WaveNet · 48 kHz · Mesa Boogie"
    QString details; // multi-line, everything the file says about itself
    // TONE3000 origin from the `.t3k.json` sidecar written on download.
    int t3k_tone_id = 0;
    int t3k_model_id = 0;
    QString t3k_title, t3k_creator;
    bool from_tone3000() const {
        return t3k_model_id != 0;
    }
};

/// Configured root folders per kind. A folder may appear under several kinds.
struct LibraryRoots
{
    QStringList models, irs, presets;

    /// ~/NAM for models and IRs, ~/Music/IRs, and the app's own presets dir.
    static LibraryRoots defaults();
    /// The folder presets are written to by default (first presets root).
    static QString default_preset_dir();
    /// Roots with the TONE3000 download folder added to models and IRs — the
    /// scanner always covers it, the Folders dialog shows the configured ones.
    LibraryRoots with_tone3000_dir() const;
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

    void set_roots(const LibraryRoots& new_roots); // persists + rescans
    const LibraryRoots& get_roots() const {
        return roots;
    }

    void rescan();
    bool is_scanning() const {
        return scanning;
    }

    const QList<LibraryEntry>& get_entries() const {
        return entries;
    }
    const LibraryEntry* find(const QString& path) const;
    /// Library file downloaded from TONE3000 with this model id, or null.
    const LibraryEntry* find_tone3000_model(int model_id) const;
    bool has_tone3000_tone(int tone_id) const;

    static LibraryEntry::Kind kind_for_path(const QString& path, bool* ok = nullptr);
    /// Synchronous one-file description (files outside the library: imports,
    /// drops). Reads the file — a model parse is a few ms per MB.
    static LibraryEntry describe(const QString& path);

signals:
    void scan_started();
    void entries_changed();

private:
    void request_scan();
    void on_scan_finished(QList<LibraryEntry> new_entries, QStringList dirs);

    LibraryRoots roots;
    QList<LibraryEntry> entries;
    QThread* thread = nullptr;
    LibraryScanWorker* worker = nullptr;
    QFileSystemWatcher* watcher = nullptr;
    QTimer* debounce = nullptr; // coalesces watcher bursts
    bool scanning = false;
    bool rescan_queued = false;
    QString cache_path;
};

} // namespace npl

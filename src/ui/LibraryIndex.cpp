#include "LibraryIndex.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtEndian>

#include "Tone3000Client.h"

// ---------------------------------------------------------------------------
// Roots
// ---------------------------------------------------------------------------

LibraryRoots LibraryRoots::defaults() {
    const QString home = QDir::homePath();
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    LibraryRoots r;
    r.models = {home + "/NAM"};
    r.irs = {home + "/NAM", music + "/IRs"};
    r.presets = {default_preset_dir()};
    return r;
}

QString LibraryRoots::default_preset_dir() {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
           "/NAM Player/Presets";
}

LibraryRoots LibraryRoots::with_tone3000_dir() const {
    LibraryRoots r = *this;
    const QString t3k = Tone3000Client::download_dir();
    if (!r.models.contains(t3k)) {
        r.models << t3k;
    }
    if (!r.irs.contains(t3k)) {
        r.irs << t3k;
    }
    return r;
}

static LibraryRoots load_roots() {
    QSettings s;
    if (!s.contains("library/modelRoots")) {
        return LibraryRoots::defaults();
    }
    LibraryRoots r;
    r.models = s.value("library/modelRoots").toStringList();
    r.irs = s.value("library/irRoots").toStringList();
    r.presets = s.value("library/presetRoots").toStringList();
    return r;
}

static void save_roots(const LibraryRoots& r) {
    QSettings s;
    s.setValue("library/modelRoots", r.models);
    s.setValue("library/irRoots", r.irs);
    s.setValue("library/presetRoots", r.presets);
}

// ---------------------------------------------------------------------------
// File parsers (worker thread)
// ---------------------------------------------------------------------------

static QString khz(double sr) {
    if (sr <= 0) {
        return {};
    }
    return QString::number(sr / 1000.0, 'g', 4) + " kHz";
}

// TONE3000 sidecar next to a downloaded model/IR: origin for the list, the
// hero row marker and preset linkage.
static void read_sidecar(const QString& path, LibraryEntry& e) {
    QFile f(Tone3000Client::sidecar_path(path));
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    e.t3k_tone_id = o.value("tone_id").toInt();
    e.t3k_model_id = o.value("model_id").toInt();
    e.t3k_title = o.value("tone_title").toString();
    e.t3k_creator = o.value("creator").toString();
}

static void append_sidecar_info(LibraryEntry& e) {
    if (!e.from_tone3000()) {
        return;
    }
    QString who =
        e.t3k_creator.isEmpty() ? QStringLiteral("TONE3000") : "TONE3000 · @" + e.t3k_creator;
    e.summary = e.summary.isEmpty() ? who : e.summary + " · " + who;
    QString line =
        "From TONE3000: " + (e.t3k_title.isEmpty() ? QStringLiteral("tone") : e.t3k_title);
    if (!e.t3k_creator.isEmpty()) {
        line += " by @" + e.t3k_creator;
    }
    e.details = e.details.isEmpty() ? line : e.details + "\n" + line;
}

// Model metadata from the .nam JSON. Files carry the full weight array, so
// this is the one expensive step of a scan — the cache keeps it one-time.
static void describe_model(const QString& path, LibraryEntry& e) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        e.summary = "Unreadable model file";
        return;
    }
    const QJsonObject o = doc.object();

    const QString arch = o.value("architecture").toString();
    const double sr = o.value("sample_rate").toDouble(0);
    const QJsonObject m = o.value("metadata").toObject();

    QStringList sum;
    if (!arch.isEmpty()) {
        sum << arch;
    }
    if (sr > 0) {
        sum << khz(sr);
    }
    // The trainer's placeholder strings ("tz-make", "tz-model") are not gear.
    auto field = [&](const char* key) {
        const QString v = m.value(key).toString().trimmed();
        return v.startsWith("tz-") ? QString() : v;
    };
    const QString make = field("gear_make");
    const QString model_name = field("gear_model");
    const QString gear =
        (model_name.startsWith(make, Qt::CaseInsensitive) ? model_name : (make + " " + model_name))
            .trimmed();
    if (!gear.isEmpty()) {
        sum << gear;
    }
    e.summary = sum.join(" · ");

    QStringList det;
    const QString title = m.value("name").toString();
    if (!title.isEmpty() && title != e.name) {
        det << "Name: " + title;
    }
    if (!arch.isEmpty()) {
        det << "Architecture: " + arch;
    }
    det << "Sample rate: " + (sr > 0 ? khz(sr) : QStringLiteral("unspecified"));
    if (!gear.isEmpty()) {
        det << "Gear: " + gear;
    }
    const QString gear_type = field("gear_type");
    const QString tone_type = field("tone_type");
    QStringList types;
    if (!gear_type.isEmpty()) {
        types << gear_type;
    }
    if (!tone_type.isEmpty()) {
        types << tone_type;
    }
    if (!types.isEmpty()) {
        det << "Type: " + types.join(" / ");
    }
    const QString by = field("modeled_by");
    if (!by.isEmpty()) {
        det << "Modeled by: " + by;
    }
    if (m.contains("loudness") && !m.value("loudness").isNull()) {
        det << QString("Loudness: %1 dB").arg(m.value("loudness").toDouble(), 0, 'f', 1);
    }
    if (m.contains("input_level_dbu") && !m.value("input_level_dbu").isNull()) {
        det << QString("Calibration: in %1 dBu, out %2 dBu")
                   .arg(m.value("input_level_dbu").toDouble(), 0, 'f', 1)
                   .arg(m.value("output_level_dbu").toDouble(), 0, 'f', 1);
    }
    const QJsonObject date = m.value("date").toObject();
    if (!date.isEmpty()) {
        det << QString("Trained: %1-%2-%3")
                   .arg(date.value("year").toInt())
                   .arg(date.value("month").toInt(), 2, 10, QChar('0'))
                   .arg(date.value("day").toInt(), 2, 10, QChar('0'));
    }
    const QString ver = o.value("version").toString();
    if (!ver.isEmpty()) {
        det << "File version: " + ver;
    }
    e.details = det.join('\n');
}

// RIFF/WAVE header: channels, rate, bit depth, data length. No decoding.
static void describe_ir(const QString& path, LibraryEntry& e) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray head = f.read(64 * 1024);
    if (head.size() < 12 || head.left(4) != "RIFF" || head.mid(8, 4) != "WAVE") {
        e.summary = "Not a WAV file";
        return;
    }
    int channels = 0, bits = 0;
    quint32 rate = 0;
    qint64 data_bytes = -1;
    qsizetype pos = 12;
    while (pos + 8 <= head.size()) {
        const QByteArray id = head.mid(pos, 4);
        const quint32 len =
            qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(head.constData() + pos + 4));
        if (id == "fmt " && pos + 24 <= head.size()) {
            const uchar* p = reinterpret_cast<const uchar*>(head.constData() + pos + 8);
            channels = qFromLittleEndian<quint16>(p + 2);
            rate = qFromLittleEndian<quint32>(p + 4);
            bits = qFromLittleEndian<quint16>(p + 14);
        } else if (id == "data") {
            data_bytes = len;
            break;
        }
        pos += 8 + static_cast<qsizetype>(len) + (len & 1);
    }

    QStringList sum, det;
    if (rate > 0) {
        sum << khz(rate);
    }
    if (channels > 0) {
        sum << (channels == 1 ? "mono" : channels == 2 ? "stereo" : QString("%1 ch").arg(channels));
    }
    if (data_bytes > 0 && channels > 0 && bits > 0 && rate > 0) {
        const qint64 frames = data_bytes / (channels * bits / 8);
        const double ms = 1000.0 * static_cast<double>(frames) / static_cast<double>(rate);
        sum << QString("%1 ms").arg(ms, 0, 'f', ms < 10 ? 1 : 0);
        det << QString("Length: %1 ms").arg(ms, 0, 'f', 1);
    }
    e.summary = sum.join(" · ");
    if (rate > 0) {
        det << "Sample rate: " + khz(rate);
    }
    if (channels > 0) {
        det << QString("Channels: %1").arg(channels);
    }
    if (bits > 0) {
        det << QString("Bit depth: %1").arg(bits);
    }
    e.details = det.join('\n');
}

static void describe_preset(const QString& path, LibraryEntry& e) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        e.summary = "Unreadable preset";
        return;
    }
    const QJsonObject o = doc.object();

    const QString nam = QFileInfo(o.value("namPath").toString()).completeBaseName();
    const QString ir = QFileInfo(o.value("irPath").toString()).completeBaseName();
    QStringList sum, det;
    if (!nam.isEmpty()) {
        sum << nam;
        det << "Model: " + nam;
    }
    if (!ir.isEmpty()) {
        sum << ir;
        det << "Cabinet: " + ir;
    }
    if (nam.isEmpty() && ir.isEmpty()) {
        sum << "no model or cabinet";
    }
    det << QString("Schema: v%1").arg(o.contains("version") ? o.value("version").toInt() : 1);
    e.summary = sum.join(" · ");
    e.details = det.join('\n');
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------

/// Lives on LibraryIndex's thread. Owns the on-disk cache: it is the only
/// code that reads or writes it, so no locking anywhere.
class LibraryScanWorker : public QObject
{
    Q_OBJECT
public slots:
    void scan(LibraryRoots roots, QString cache_path) {
        // dir → kinds it feeds. One walk per unique directory tree.
        QHash<QString, QSet<int>> root_kinds;
        auto add = [&](const QStringList& list, LibraryEntry::Kind k) {
            for (const QString& r : list) {
                const QString c = QDir::cleanPath(r);
                if (!c.isEmpty()) {
                    root_kinds[c].insert(static_cast<int>(k));
                }
            }
        };
        add(roots.models, LibraryEntry::Kind::MODEL);
        add(roots.irs, LibraryEntry::Kind::IR);
        add(roots.presets, LibraryEntry::Kind::PRESET);

        const QHash<QString, LibraryEntry> cache = read_cache(cache_path);
        bool cache_dirty = false;

        QList<LibraryEntry> out;
        QSet<QString> seen; // a file under two overlapping roots lists once
        QStringList dirs;

        // Longest roots first so a nested root claims its own files and the
        // outer root skips them (both list the same path otherwise).
        QStringList sorted_roots = root_kinds.keys();
        std::sort(sorted_roots.begin(), sorted_roots.end(), [](const QString& a, const QString& b) {
            return a.size() > b.size();
        });

        for (const QString& root : sorted_roots) {
            QDir rd(root);
            if (!rd.exists()) {
                continue;
            }
            dirs << root;
            const QSet<int>& kinds = root_kinds[root];

            QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                            QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
            while (it.hasNext()) {
                const QString p = QDir::cleanPath(it.next());
                const QFileInfo fi = it.fileInfo();
                if (fi.isDir()) {
                    dirs << p;
                    continue;
                }

                bool ok = false;
                const auto kind = LibraryIndex::kind_for_path(p, &ok);
                if (!ok || !kinds.contains(static_cast<int>(kind))) {
                    continue;
                }
                if (seen.contains(p)) {
                    continue;
                }
                seen.insert(p);

                LibraryEntry e;
                e.kind = kind;
                e.path = p;
                e.name = fi.completeBaseName();
                e.folder = rd.relativeFilePath(fi.absolutePath());
                if (e.folder == ".") {
                    e.folder.clear();
                }
                e.size = fi.size();
                e.mtime_ms = fi.lastModified().toMSecsSinceEpoch();

                const auto c = cache.constFind(p);
                if (c != cache.constEnd() && c->size == e.size && c->mtime_ms == e.mtime_ms &&
                    c->kind == kind) {
                    e.summary = c->summary;
                    e.details = c->details;
                    e.t3k_tone_id = c->t3k_tone_id;
                    e.t3k_model_id = c->t3k_model_id;
                    e.t3k_title = c->t3k_title;
                    e.t3k_creator = c->t3k_creator;
                } else {
                    switch (kind) {
                        case LibraryEntry::Kind::MODEL:
                            describe_model(p, e);
                            break;
                        case LibraryEntry::Kind::IR:
                            describe_ir(p, e);
                            break;
                        case LibraryEntry::Kind::PRESET:
                            describe_preset(p, e);
                            break;
                    }
                    read_sidecar(p, e);
                    append_sidecar_info(e);
                    cache_dirty = true;
                }
                out << e;
            }
        }

        if (cache_dirty || cache.size() != out.size()) {
            write_cache(cache_path, out);
        }
        dirs.removeDuplicates();
        emit finished(out, dirs);
    }

signals:
    void finished(QList<LibraryEntry> entries, QStringList dirs);

private:
    static QHash<QString, LibraryEntry> read_cache(const QString& path) {
        QHash<QString, LibraryEntry> h;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return h;
        }
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (root.value("version").toInt() != 2) {
            return h;
        }
        for (const QJsonValue v : root.value("entries").toArray()) {
            const QJsonObject o = v.toObject();
            LibraryEntry e;
            e.kind = static_cast<LibraryEntry::Kind>(o.value("kind").toInt());
            e.path = o.value("path").toString();
            e.size = static_cast<qint64>(o.value("size").toDouble());
            e.mtime_ms = static_cast<qint64>(o.value("mtime").toDouble());
            e.summary = o.value("summary").toString();
            e.details = o.value("details").toString();
            e.t3k_tone_id = o.value("t3kTone").toInt();
            e.t3k_model_id = o.value("t3kModel").toInt();
            e.t3k_title = o.value("t3kTitle").toString();
            e.t3k_creator = o.value("t3kCreator").toString();
            if (!e.path.isEmpty()) {
                h.insert(e.path, e);
            }
        }
        return h;
    }

    static void write_cache(const QString& path, const QList<LibraryEntry>& entries) {
        QJsonArray arr;
        for (const auto& e : entries) {
            QJsonObject o;
            o["kind"] = static_cast<int>(e.kind);
            o["path"] = e.path;
            o["size"] = static_cast<double>(e.size);
            o["mtime"] = static_cast<double>(e.mtime_ms);
            o["summary"] = e.summary;
            o["details"] = e.details;
            if (e.from_tone3000()) {
                o["t3kTone"] = e.t3k_tone_id;
                o["t3kModel"] = e.t3k_model_id;
                o["t3kTitle"] = e.t3k_title;
                o["t3kCreator"] = e.t3k_creator;
            }
            arr.append(o);
        }
        QJsonObject root;
        root["version"] = 2;
        root["entries"] = arr;
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
    }
};

// ---------------------------------------------------------------------------
// LibraryIndex
// ---------------------------------------------------------------------------

LibraryIndex::LibraryIndex(QObject* parent)
    : QObject(parent)
    , m_roots(load_roots()) {
    m_cache_path =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library-index.json";

    m_thread = new QThread(this);
    m_thread->setObjectName("LibraryScan");
    m_worker = new LibraryScanWorker;
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &LibraryScanWorker::finished, this, &LibraryIndex::on_scan_finished);
    m_thread->start(QThread::LowPriority);

    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(600);
    connect(m_debounce, &QTimer::timeout, this, &LibraryIndex::request_scan);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, m_debounce,
            qOverload<>(&QTimer::start));

    request_scan();
}

LibraryIndex::~LibraryIndex() {
    m_thread->quit();
    m_thread->wait();
}

void LibraryIndex::set_roots(const LibraryRoots& roots) {
    m_roots = roots;
    save_roots(roots);
    request_scan();
}

void LibraryIndex::rescan() {
    request_scan();
}

void LibraryIndex::request_scan() {
    if (m_scanning) {
        m_rescan_queued = true;
        return;
    }
    m_scanning = true;
    emit scan_started();
    QMetaObject::invokeMethod(
        m_worker,
        [w = m_worker, roots = m_roots.with_tone3000_dir(), cache = m_cache_path] {
            w->scan(roots, cache);
        },
        Qt::QueuedConnection);
}

void LibraryIndex::on_scan_finished(QList<LibraryEntry> entries, QStringList dirs) {
    m_entries = std::move(entries);
    m_scanning = false;

    // Watch every scanned directory (new files, deletions, new subfolders);
    // a burst of changes collapses into one rescan through the debounce.
    const QStringList old = m_watcher->directories();
    if (!old.isEmpty()) {
        m_watcher->removePaths(old);
    }
    if (!dirs.isEmpty()) {
        m_watcher->addPaths(dirs);
    }

    emit entries_changed();

    if (m_rescan_queued) {
        m_rescan_queued = false;
        request_scan();
    }
}

const LibraryEntry* LibraryIndex::find(const QString& path) const {
    const QString c = QDir::cleanPath(path);
    for (const auto& e : m_entries) {
        if (e.path == c) {
            return &e;
        }
    }
    return nullptr;
}

LibraryEntry LibraryIndex::describe(const QString& path) {
    LibraryEntry e;
    const QFileInfo fi(path);
    bool ok = false;
    e.kind = kind_for_path(path, &ok);
    e.path = QDir::cleanPath(fi.absoluteFilePath());
    e.name = fi.completeBaseName();
    e.size = fi.size();
    e.mtime_ms = fi.lastModified().toMSecsSinceEpoch();
    if (!ok || !fi.exists()) {
        return e;
    }
    switch (e.kind) {
        case LibraryEntry::Kind::MODEL:
            describe_model(e.path, e);
            break;
        case LibraryEntry::Kind::IR:
            describe_ir(e.path, e);
            break;
        case LibraryEntry::Kind::PRESET:
            describe_preset(e.path, e);
            break;
    }
    read_sidecar(e.path, e);
    append_sidecar_info(e);
    return e;
}

const LibraryEntry* LibraryIndex::find_tone3000_model(int model_id) const {
    if (model_id == 0) {
        return nullptr;
    }
    for (const auto& e : m_entries) {
        if (e.t3k_model_id == model_id) {
            return &e;
        }
    }
    return nullptr;
}

bool LibraryIndex::has_tone3000_tone(int tone_id) const {
    if (tone_id == 0) {
        return false;
    }
    for (const auto& e : m_entries) {
        if (e.t3k_tone_id == tone_id) {
            return true;
        }
    }
    return false;
}

LibraryEntry::Kind LibraryIndex::kind_for_path(const QString& path, bool* ok) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ok) {
        *ok = true;
    }
    if (ext == "nam") {
        return LibraryEntry::Kind::MODEL;
    }
    if (ext == "wav") {
        return LibraryEntry::Kind::IR;
    }
    if (ext == "nampreset") {
        return LibraryEntry::Kind::PRESET;
    }
    if (ok) {
        *ok = false;
    }
    return LibraryEntry::Kind::MODEL;
}

#include "LibraryIndex.moc"

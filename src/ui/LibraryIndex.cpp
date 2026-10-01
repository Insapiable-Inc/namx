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
    r.presets = {defaultPresetDir()};
    return r;
}

QString LibraryRoots::defaultPresetDir() {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
           "/NAM Player/Presets";
}

LibraryRoots LibraryRoots::withTone3000Dir() const {
    LibraryRoots r = *this;
    const QString t3k = Tone3000Client::downloadDir();
    if (!r.models.contains(t3k)) {
        r.models << t3k;
    }
    if (!r.irs.contains(t3k)) {
        r.irs << t3k;
    }
    return r;
}

static LibraryRoots loadRoots() {
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

static void saveRoots(const LibraryRoots& r) {
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
static void readSidecar(const QString& path, LibraryEntry& e) {
    QFile f(Tone3000Client::sidecarPath(path));
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    e.t3kToneId = o.value("tone_id").toInt();
    e.t3kModelId = o.value("model_id").toInt();
    e.t3kTitle = o.value("tone_title").toString();
    e.t3kCreator = o.value("creator").toString();
}

static void appendSidecarInfo(LibraryEntry& e) {
    if (!e.fromTone3000()) {
        return;
    }
    QString who =
        e.t3kCreator.isEmpty() ? QStringLiteral("TONE3000") : "TONE3000 · @" + e.t3kCreator;
    e.summary = e.summary.isEmpty() ? who : e.summary + " · " + who;
    QString line = "From TONE3000: " + (e.t3kTitle.isEmpty() ? QStringLiteral("tone") : e.t3kTitle);
    if (!e.t3kCreator.isEmpty()) {
        line += " by @" + e.t3kCreator;
    }
    e.details = e.details.isEmpty() ? line : e.details + "\n" + line;
}

// Model metadata from the .nam JSON. Files carry the full weight array, so
// this is the one expensive step of a scan — the cache keeps it one-time.
static void describeModel(const QString& path, LibraryEntry& e) {
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
    const QString modelName = field("gear_model");
    const QString gear =
        (modelName.startsWith(make, Qt::CaseInsensitive) ? modelName : (make + " " + modelName))
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
    const QString gearType = field("gear_type");
    const QString toneType = field("tone_type");
    QStringList types;
    if (!gearType.isEmpty()) {
        types << gearType;
    }
    if (!toneType.isEmpty()) {
        types << toneType;
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
static void describeIR(const QString& path, LibraryEntry& e) {
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
    qint64 dataBytes = -1;
    int pos = 12;
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
            dataBytes = len;
            break;
        }
        pos += 8 + static_cast<int>(len) + (len & 1);
    }

    QStringList sum, det;
    if (rate > 0) {
        sum << khz(rate);
    }
    if (channels > 0) {
        sum << (channels == 1 ? "mono" : channels == 2 ? "stereo" : QString("%1 ch").arg(channels));
    }
    if (dataBytes > 0 && channels > 0 && bits > 0 && rate > 0) {
        const double ms = 1000.0 * (dataBytes / (channels * bits / 8)) / double(rate);
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

static void describePreset(const QString& path, LibraryEntry& e) {
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
    void scan(LibraryRoots roots, QString cachePath) {
        // dir → kinds it feeds. One walk per unique directory tree.
        QHash<QString, QSet<int>> rootKinds;
        auto add = [&](const QStringList& list, LibraryEntry::Kind k) {
            for (const QString& r : list) {
                const QString c = QDir::cleanPath(r);
                if (!c.isEmpty()) {
                    rootKinds[c].insert(static_cast<int>(k));
                }
            }
        };
        add(roots.models, LibraryEntry::Kind::Model);
        add(roots.irs, LibraryEntry::Kind::IR);
        add(roots.presets, LibraryEntry::Kind::Preset);

        const QHash<QString, LibraryEntry> cache = readCache(cachePath);
        bool cacheDirty = false;

        QList<LibraryEntry> out;
        QSet<QString> seen; // a file under two overlapping roots lists once
        QStringList dirs;

        // Longest roots first so a nested root claims its own files and the
        // outer root skips them (both list the same path otherwise).
        QStringList roots_ = rootKinds.keys();
        std::sort(roots_.begin(), roots_.end(), [](const QString& a, const QString& b) {
            return a.size() > b.size();
        });

        for (const QString& root : roots_) {
            QDir rd(root);
            if (!rd.exists()) {
                continue;
            }
            dirs << root;
            const QSet<int>& kinds = rootKinds[root];

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
                const auto kind = LibraryIndex::kindForPath(p, &ok);
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
                e.mtimeMs = fi.lastModified().toMSecsSinceEpoch();

                const auto c = cache.constFind(p);
                if (c != cache.constEnd() && c->size == e.size && c->mtimeMs == e.mtimeMs &&
                    c->kind == kind) {
                    e.summary = c->summary;
                    e.details = c->details;
                    e.t3kToneId = c->t3kToneId;
                    e.t3kModelId = c->t3kModelId;
                    e.t3kTitle = c->t3kTitle;
                    e.t3kCreator = c->t3kCreator;
                } else {
                    switch (kind) {
                        case LibraryEntry::Kind::Model:
                            describeModel(p, e);
                            break;
                        case LibraryEntry::Kind::IR:
                            describeIR(p, e);
                            break;
                        case LibraryEntry::Kind::Preset:
                            describePreset(p, e);
                            break;
                    }
                    readSidecar(p, e);
                    appendSidecarInfo(e);
                    cacheDirty = true;
                }
                out << e;
            }
        }

        if (cacheDirty || cache.size() != out.size()) {
            writeCache(cachePath, out);
        }
        dirs.removeDuplicates();
        emit finished(out, dirs);
    }

signals:
    void finished(QList<LibraryEntry> entries, QStringList dirs);

private:
    static QHash<QString, LibraryEntry> readCache(const QString& path) {
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
            e.mtimeMs = static_cast<qint64>(o.value("mtime").toDouble());
            e.summary = o.value("summary").toString();
            e.details = o.value("details").toString();
            e.t3kToneId = o.value("t3kTone").toInt();
            e.t3kModelId = o.value("t3kModel").toInt();
            e.t3kTitle = o.value("t3kTitle").toString();
            e.t3kCreator = o.value("t3kCreator").toString();
            if (!e.path.isEmpty()) {
                h.insert(e.path, e);
            }
        }
        return h;
    }

    static void writeCache(const QString& path, const QList<LibraryEntry>& entries) {
        QJsonArray arr;
        for (const auto& e : entries) {
            QJsonObject o;
            o["kind"] = static_cast<int>(e.kind);
            o["path"] = e.path;
            o["size"] = static_cast<double>(e.size);
            o["mtime"] = static_cast<double>(e.mtimeMs);
            o["summary"] = e.summary;
            o["details"] = e.details;
            if (e.fromTone3000()) {
                o["t3kTone"] = e.t3kToneId;
                o["t3kModel"] = e.t3kModelId;
                o["t3kTitle"] = e.t3kTitle;
                o["t3kCreator"] = e.t3kCreator;
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
    , mRoots(loadRoots()) {
    mCachePath =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/library-index.json";

    mThread = new QThread(this);
    mThread->setObjectName("LibraryScan");
    mWorker = new LibraryScanWorker;
    mWorker->moveToThread(mThread);
    connect(mThread, &QThread::finished, mWorker, &QObject::deleteLater);
    connect(mWorker, &LibraryScanWorker::finished, this, &LibraryIndex::onScanFinished);
    mThread->start(QThread::LowPriority);

    mWatcher = new QFileSystemWatcher(this);
    mDebounce = new QTimer(this);
    mDebounce->setSingleShot(true);
    mDebounce->setInterval(600);
    connect(mDebounce, &QTimer::timeout, this, &LibraryIndex::requestScan);
    connect(mWatcher, &QFileSystemWatcher::directoryChanged, mDebounce,
            qOverload<>(&QTimer::start));

    requestScan();
}

LibraryIndex::~LibraryIndex() {
    mThread->quit();
    mThread->wait();
}

void LibraryIndex::setRoots(const LibraryRoots& roots) {
    mRoots = roots;
    saveRoots(roots);
    requestScan();
}

void LibraryIndex::rescan() {
    requestScan();
}

void LibraryIndex::requestScan() {
    if (mScanning) {
        mRescanQueued = true;
        return;
    }
    mScanning = true;
    emit scanStarted();
    QMetaObject::invokeMethod(
        mWorker,
        [w = mWorker, roots = mRoots.withTone3000Dir(), cache = mCachePath] {
            w->scan(roots, cache);
        },
        Qt::QueuedConnection);
}

void LibraryIndex::onScanFinished(QList<LibraryEntry> entries, QStringList dirs) {
    mEntries = std::move(entries);
    mScanning = false;

    // Watch every scanned directory (new files, deletions, new subfolders);
    // a burst of changes collapses into one rescan through the debounce.
    const QStringList old = mWatcher->directories();
    if (!old.isEmpty()) {
        mWatcher->removePaths(old);
    }
    if (!dirs.isEmpty()) {
        mWatcher->addPaths(dirs);
    }

    emit entriesChanged();

    if (mRescanQueued) {
        mRescanQueued = false;
        requestScan();
    }
}

const LibraryEntry* LibraryIndex::find(const QString& path) const {
    const QString c = QDir::cleanPath(path);
    for (const auto& e : mEntries) {
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
    e.kind = kindForPath(path, &ok);
    e.path = QDir::cleanPath(fi.absoluteFilePath());
    e.name = fi.completeBaseName();
    e.size = fi.size();
    e.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    if (!ok || !fi.exists()) {
        return e;
    }
    switch (e.kind) {
        case LibraryEntry::Kind::Model:
            describeModel(e.path, e);
            break;
        case LibraryEntry::Kind::IR:
            describeIR(e.path, e);
            break;
        case LibraryEntry::Kind::Preset:
            describePreset(e.path, e);
            break;
    }
    readSidecar(e.path, e);
    appendSidecarInfo(e);
    return e;
}

const LibraryEntry* LibraryIndex::findTone3000Model(int modelId) const {
    if (modelId == 0) {
        return nullptr;
    }
    for (const auto& e : mEntries) {
        if (e.t3kModelId == modelId) {
            return &e;
        }
    }
    return nullptr;
}

bool LibraryIndex::hasTone3000Tone(int toneId) const {
    if (toneId == 0) {
        return false;
    }
    for (const auto& e : mEntries) {
        if (e.t3kToneId == toneId) {
            return true;
        }
    }
    return false;
}

LibraryEntry::Kind LibraryIndex::kindForPath(const QString& path, bool* ok) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ok) {
        *ok = true;
    }
    if (ext == "nam") {
        return LibraryEntry::Kind::Model;
    }
    if (ext == "wav") {
        return LibraryEntry::Kind::IR;
    }
    if (ext == "nampreset") {
        return LibraryEntry::Kind::Preset;
    }
    if (ok) {
        *ok = false;
    }
    return LibraryEntry::Kind::Model;
}

#include "LibraryIndex.moc"

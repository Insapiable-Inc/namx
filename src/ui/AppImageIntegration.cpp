#include "AppImageIntegration.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace namx::appimage
{

namespace
{

constexpr const char* APP_ID = "io.github.Insapiable_Inc.namx";
// Marks the launcher as ours, so remove_from_menu() never deletes another tool's.
constexpr const char* MARKER = "X-NAMX-AppImage-Integration=true";

QString appimage_path() {
    return qEnvironmentVariable("APPIMAGE");
}

// Where the running AppImage is mounted; the install layout sits under usr/.
QString appdir_file(const QString& relative) {
    return qEnvironmentVariable("APPDIR") + "/usr/share/" + relative;
}

QString data_home() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString launcher_path() {
    return data_home() + "/applications/" + APP_ID + ".desktop";
}

QString mime_path() {
    return data_home() + "/mime/packages/" + APP_ID + ".xml";
}

QStringList icon_paths() {
    return {data_home() + "/icons/hicolor/256x256/apps/" + APP_ID + ".png",
            data_home() + "/icons/hicolor/scalable/apps/" + APP_ID + ".svg"};
}

QByteArray read_file(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

expected<void, QString> write_file(const QString& path, const QByteArray& data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return std::unexpected("Can't create the folder for " + path);
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        return std::unexpected("Can't write " + path + ": " + f.errorString());
    }
    return {};
}

// Desktop Entry spec quoting for an Exec argument: inside double quotes, the double
// quote, backtick, dollar and backslash take a backslash, and the value's own escaping
// doubles every backslash again.
QString quoted_exec_arg(const QString& path) {
    QString quoted;
    for (const QChar c : path) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') {
            quoted += '\\';
        }
        quoted += c;
    }
    quoted.replace("\\", "\\\\");
    return '"' + quoted + '"';
}

// The bundled launcher, pointed at this AppImage file.
QByteArray make_launcher(const QByteArray& bundled, const QString& image) {
    QStringList out;
    for (const QString& line : QString::fromUtf8(bundled).split('\n')) {
        if (line.startsWith("Exec=")) {
            out << "Exec=" + quoted_exec_arg(image) + " %F";
        } else if (line.startsWith("TryExec=")) {
            out << "TryExec=" + image;
        } else {
            out << line;
        }
        if (line.trimmed() == "[Desktop Entry]") {
            out << MARKER;
        }
    }
    return out.join('\n').toUtf8();
}

void refresh_databases() {
    const QString update_desktop = QStandardPaths::findExecutable("update-desktop-database");
    if (!update_desktop.isEmpty()) {
        QProcess::startDetached(update_desktop, {data_home() + "/applications"});
    }
    const QString update_mime = QStandardPaths::findExecutable("update-mime-database");
    if (!update_mime.isEmpty()) {
        QProcess::startDetached(update_mime, {data_home() + "/mime"});
    }
}

} // namespace

bool is_running_as_appimage() {
    return !appimage_path().isEmpty();
}

bool is_added_to_menu() {
    return read_file(launcher_path()).contains(MARKER);
}

bool is_integrated_by_anyone() {
    const QString desktop_name = QString(APP_ID) + ".desktop";
    for (const QString& dir :
         QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (QFileInfo::exists(dir + '/' + desktop_name)) {
            return true;
        }
    }
    // AppImageLauncher / Gear Lever name their launchers their own way; theirs run
    // this very file.
    const QByteArray image = appimage_path().toUtf8();
    QDirIterator it(data_home() + "/applications", {"*.desktop"}, QDir::Files);
    while (it.hasNext()) {
        if (read_file(it.next()).contains(image)) {
            return true;
        }
    }
    return false;
}

expected<void, QString> add_to_menu() {
    if (!is_running_as_appimage()) {
        return std::unexpected(QString("Not running as an AppImage."));
    }
    const QByteArray bundled =
        read_file(appdir_file("applications/" + QString(APP_ID) + ".desktop"));
    if (bundled.isEmpty()) {
        return std::unexpected(QString("The AppImage has no launcher file inside."));
    }
    if (auto written = write_file(launcher_path(), make_launcher(bundled, appimage_path()));
        !written) {
        return written;
    }
    const QStringList bundled_icons = {
        appdir_file("icons/hicolor/256x256/apps/" + QString(APP_ID) + ".png"),
        appdir_file("icons/hicolor/scalable/apps/" + QString(APP_ID) + ".svg")};
    const QStringList icons = icon_paths();
    for (qsizetype i = 0; i < icons.size(); ++i) {
        const QByteArray icon = read_file(bundled_icons[i]);
        if (!icon.isEmpty()) {
            if (auto written = write_file(icons[i], icon); !written) {
                return written;
            }
        }
    }
    const QByteArray mime = read_file(appdir_file("mime/packages/" + QString(APP_ID) + ".xml"));
    if (!mime.isEmpty()) {
        if (auto written = write_file(mime_path(), mime); !written) {
            return written;
        }
    }
    refresh_databases();
    return {};
}

expected<void, QString> remove_from_menu() {
    if (!is_added_to_menu()) {
        return {};
    }
    QStringList files = icon_paths();
    files << mime_path() << launcher_path(); // the launcher last: it marks the rest as ours
    for (const QString& path : files) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            return std::unexpected("Can't remove " + path);
        }
    }
    refresh_databases();
    return {};
}

void refresh_launcher_path() {
    if (!is_running_as_appimage() || !is_added_to_menu()) {
        return;
    }
    const QByteArray try_exec = "TryExec=" + appimage_path().toUtf8() + '\n';
    if (!read_file(launcher_path()).contains(try_exec)) {
        (void)add_to_menu();
    }
}

} // namespace namx::appimage

#include "Tone3000Client.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmapCache>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include "Common.h"
#include "Log.h"

namespace npl
{

#ifndef NAM_TONE3000_CLIENT_ID
#define NAM_TONE3000_CLIENT_ID ""
#endif

constexpr qint64 CACHE_TTL_MS = 5 * 60 * 1000;
constexpr int AUTH_TIMEOUT_MS = 5 * 60 * 1000;
constexpr int MAX_RETRY_AFTER_S = 30;
static constexpr int PREFERRED_PORTS[] = {51731, 51732, 51733, 51734};

static QString base64url(const QByteArray& bytes) {
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

static QByteArray random_bytes(int n) {
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(b.data()), n / 4);
    return b;
}

static QString title_case_gear(const QString& gear) {
    if (gear == "amp-cab" || gear == "full-rig") {
        return "Amp + Cab";
    }
    if (gear == "amp") {
        return "Amp";
    }
    if (gear == "pedal") {
        return "Pedal";
    }
    if (gear == "outboard") {
        return "Outboard";
    }
    if (gear == "cab") {
        return "Cab";
    }
    if (gear == "space") {
        return "Space";
    }
    if (gear == "experimental") {
        return "Experimental";
    }
    if (gear == "ir") {
        return "IR";
    }
    return gear;
}

// File-system safe component: no separators, control chars or Windows
// reserved punctuation; trimmed; bounded.
static QString sanitize_component(QString s) {
    static const QString BAD = QStringLiteral("/\\:*?\"<>|");
    for (QChar& c : s) {
        if (BAD.contains(c) || c.unicode() < 32) {
            c = QChar('_');
        }
    }
    s = s.simplified();
    while (s.endsWith('.') || s.endsWith(' ')) {
        s.chop(1);
    }
    if (s.size() > 100) {
        s.truncate(100);
    }
    if (s.isEmpty()) {
        s = "untitled";
    }
    return s;
}

QString T3kTone::gear_label() const {
    return title_case_gear(gear);
}
QString T3kTone::format_label() const {
    return format.toUpper();
}
QString T3kTone::license_label() const {
    if (license == "t3k") {
        return "TONE3000 license";
    }
    if (license == "cco" || license == "cc0") {
        return "CC0";
    }
    if (license.startsWith("cc-")) {
        return license.toUpper().replace('-', ' ').replace("CC ", "CC ");
    }
    return license;
}

T3kUser Tone3000Client::parse_user(const QJsonObject& o) {
    T3kUser u;
    u.id = o.value("id").toInt();
    u.username = o.value("username").toString();
    u.display_name = o.value("display_name").toString();
    u.avatar_url = o.value("avatar_url").toString();
    u.url = o.value("url").toString();
    return u;
}

T3kTone Tone3000Client::parse_tone(const QJsonObject& o) {
    T3kTone t;
    t.id = o.value("id").toInt();
    t.title = o.value("title").toString();
    t.description = o.value("description").toString();
    t.gear = o.value("gear").toString();
    t.format = o.value("format").toString();
    t.license = o.value("license").toString();
    t.url = o.value("url").toString();
    for (const QJsonValue v : o.value("images").toArray()) {
        t.images << v.toString();
    }
    t.user = parse_user(o.value("user").toObject());
    for (const QJsonValue v : o.value("makes").toArray()) {
        t.makes << v.toObject().value("name").toString();
    }
    for (const QJsonValue v : o.value("tags").toArray()) {
        t.tags << v.toObject().value("name").toString();
    }
    t.models_count = o.value("models_count").toInt();
    t.a1_count = o.value("a1_models_count").toInt();
    t.a2_count = o.value("a2_models_count").toInt();
    t.custom_count = o.value("custom_models_count").toInt();
    t.irs_count = o.value("irs_count").toInt();
    t.downloads = o.value("downloads_count").toInt();
    t.favorites = o.value("favorites_count").toInt();
    t.is_favorite = o.value("is_favorite").toBool();
    return t;
}

T3kModel Tone3000Client::parse_model(const QJsonObject& o) {
    T3kModel m;
    m.id = o.value("id").toInt();
    m.tone_id = o.value("tone_id").toInt();
    m.name = o.value("name").toString();
    m.model_url = o.value("model_url").toString();
    m.size = o.value("size").toString();
    m.architecture = o.value("architecture_version").toString();
    return m;
}

// NAM_TONE3000_API_BASE overrides the API host (tests point it at a mock).
static QString api_base_from_env() {
    const QByteArray env = qgetenv("NAM_TONE3000_API_BASE");
    return env.isEmpty() ? QStringLiteral("https://www.tone3000.com/api/v1")
                         : QString::fromUtf8(env).trimmed();
}

QString Tone3000Client::api_base() {
    static const QString BASE = api_base_from_env();
    return BASE;
}

QString Tone3000Client::client_id() {
    const QString from_settings = QSettings().value("tone3000/clientId").toString().trimmed();
    if (!from_settings.isEmpty()) {
        return from_settings;
    }
    return QString::fromUtf8(NAM_TONE3000_CLIENT_ID).trimmed();
}

void Tone3000Client::set_client_id(const QString& id) {
    QSettings s;
    if (id.trimmed().isEmpty()) {
        s.remove("tone3000/clientId");
    } else {
        s.setValue("tone3000/clientId", id.trimmed());
    }
}

QString Tone3000Client::download_dir() {
    const QString v = QSettings().value("library/tone3000Dir").toString();
    if (!v.isEmpty()) {
        return QDir::cleanPath(v);
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
           "/NAM Player/TONE3000";
}

Tone3000Client::Tone3000Client(QObject* parent)
    : QObject(parent)
    , nam(new QNetworkAccessManager(this)) {
    // Disk cache for images (the API answers carry no-store; they go through
    // cache instead).
    auto* disk = new QNetworkDiskCache(this);
    disk->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            "/tone3000");
    disk->setMaximumCacheSize(64 * 1024 * 1024);
    nam->setCache(disk);
    nam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    nam->setTransferTimeout(30000);

    load_tokens();
    if (is_signed_in()) {
        fetch_current_user([](const Result&) {});
    }
}

Tone3000Client::~Tone3000Client() {
    cancel_auth();
}

void Tone3000Client::load_tokens() {
    QSettings s;
    access_token = s.value("tone3000/accessToken").toString();
    refresh_token = s.value("tone3000/refreshToken").toString();
    expires_at_ms = s.value("tone3000/expiresAt").toLongLong();
    user.id = s.value("tone3000/user/id").toInt();
    user.username = s.value("tone3000/user/username").toString();
    user.display_name = s.value("tone3000/user/displayName").toString();
    user.avatar_url = s.value("tone3000/user/avatarUrl").toString();
    user.url = s.value("tone3000/user/url").toString();
}

void Tone3000Client::save_tokens() {
    QSettings s;
    s.setValue("tone3000/accessToken", access_token);
    s.setValue("tone3000/refreshToken", refresh_token);
    s.setValue("tone3000/expiresAt", expires_at_ms);
    s.setValue("tone3000/user/id", user.id);
    s.setValue("tone3000/user/username", user.username);
    s.setValue("tone3000/user/displayName", user.display_name);
    s.setValue("tone3000/user/avatarUrl", user.avatar_url);
    s.setValue("tone3000/user/url", user.url);
}

void Tone3000Client::clear_tokens() {
    access_token.clear();
    refresh_token.clear();
    expires_at_ms = 0;
    user = {};
    QSettings s;
    s.remove("tone3000/accessToken");
    s.remove("tone3000/refreshToken");
    s.remove("tone3000/expiresAt");
    s.remove("tone3000/user");
    cache.clear();
}

void Tone3000Client::apply_token_response(const QJsonObject& o) {
    access_token = o.value("access_token").toString();
    const QString refresh = o.value("refresh_token").toString();
    if (!refresh.isEmpty()) {
        refresh_token = refresh;
    }
    const int expires_in = o.value("expires_in").toInt(3600);
    expires_at_ms = QDateTime::currentMSecsSinceEpoch() + qint64(expires_in) * 1000;
    save_tokens();
}

void Tone3000Client::sign_out() {
    cancel_auth();
    const bool was = is_signed_in();
    clear_tokens();
    if (was) {
        emit auth_changed();
    }
    emit user_changed();
}

void Tone3000Client::start_auth(Flow flow, const QHash<QString, QString>& extra) {
    if (!has_client_id()) {
        emit auth_finished(false, "No TONE3000 publishable key configured.", 0);
        return;
    }
    cancel_auth();

    // Loopback server on a known port so the redirect URI can be registered
    // verbatim; both IPv4 and IPv6 since the browser resolves "localhost".
    auth_server = new QTcpServer(this);
    auth_server_v6 = new QTcpServer(this);
    quint16 port = 0;
    for (int p : PREFERRED_PORTS) {
        if (auth_server->listen(QHostAddress::LocalHost, static_cast<quint16>(p))) {
            port = static_cast<quint16>(p);
            break;
        }
    }
    if (port == 0 && auth_server->listen(QHostAddress::LocalHost, 0)) {
        port = auth_server->serverPort();
    }
    if (port == 0) {
        cancel_auth();
        emit auth_finished(false, "Could not open a local port for the sign-in callback.", 0);
        return;
    }
    if (!auth_server_v6->listen(QHostAddress::LocalHostIPv6, port)) {
        auth_server_v6->deleteLater();
        auth_server_v6 = nullptr;
    }
    connect(auth_server, &QTcpServer::newConnection, this, &Tone3000Client::on_auth_connection);
    if (auth_server_v6) {
        connect(auth_server_v6, &QTcpServer::newConnection, this,
                &Tone3000Client::on_auth_connection);
    }

    auth_flow = flow;
    auth_redirect = QString("http://localhost:%1/callback").arg(port);
    auth_verifier = base64url(random_bytes(32));
    auth_state = base64url(random_bytes(16));
    const QString challenge =
        base64url(QCryptographicHash::hash(auth_verifier.toLatin1(), QCryptographicHash::Sha256));

    QUrl url(api_base() + "/oauth/authorize");
    QUrlQuery q;
    q.addQueryItem("client_id", client_id());
    q.addQueryItem("redirect_uri", auth_redirect);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("code_challenge", challenge);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", auth_state);
    if (flow == Flow::SELECT_TONE) {
        q.addQueryItem("prompt", "select_tone");
        q.addQueryItem("menubar", "true");
        q.addQueryItem("preview", "true");
    }
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) {
        q.addQueryItem(it.key(), it.value());
    }
    url.setQuery(q);

    auth_timeout = new QTimer(this);
    auth_timeout->setSingleShot(true);
    connect(auth_timeout, &QTimer::timeout, this, [this] {
        finish_auth(false, "Sign-in timed out.", 0);
    });
    auth_timeout->start(AUTH_TIMEOUT_MS);

    // NAM_TONE3000_NO_BROWSER=1 (development): log the URL instead of
    // opening it, so a script can drive the callback.
    if (!qEnvironmentVariableIsEmpty("NAM_TONE3000_NO_BROWSER")) {
        qCInfo(lc_audio) << "TONE3000 authorize URL:" << url.toString(QUrl::FullyEncoded);
        return;
    }
    if (!QDesktopServices::openUrl(url)) {
        finish_auth(false, "Could not open the browser.", 0);
    }
}

void Tone3000Client::cancel_auth() {
    for (QTcpServer** s : {&auth_server, &auth_server_v6}) {
        if (*s) {
            (*s)->close();
            (*s)->deleteLater();
            *s = nullptr;
        }
    }
    if (auth_timeout) {
        auth_timeout->stop();
        auth_timeout->deleteLater();
        auth_timeout = nullptr;
    }
}

void Tone3000Client::on_auth_connection() {
    auto* server = qobject_cast<QTcpServer*>(sender());
    if (!server) {
        return;
    }
    while (QTcpSocket* sock = server->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            on_auth_request(sock);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

// The browser hit the loopback server: answer it and finish the sign-in.
void Tone3000Client::on_auth_request(QTcpSocket* sock) {
    const QByteArray data = sock->peek(64 * 1024);
    const qsizetype eol = data.indexOf("\r\n");
    if (eol < 0) {
        return; // request line not complete yet
    }
    sock->readAll();

    // "GET /callback?code=…&state=… HTTP/1.1"
    const QList<QByteArray> parts = data.left(eol).split(' ');
    const QUrl req_url =
        parts.size() >= 2 ? QUrl("http://localhost" + QString::fromLatin1(parts[1])) : QUrl();
    const QUrlQuery q(req_url);

    QString body;
    const bool is_callback = req_url.path() == "/callback";
    if (is_callback) {
        body = "<!doctype html><html><head><meta charset=\"utf-8\"><title>NAM Player</title>"
               "<style>body{font-family:sans-serif;background:#15171c;color:#d8dee9;"
               "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
               "div{text-align:center}h1{font-weight:600}</style></head><body><div>"
               "<h1>NAM Player</h1><p>You can close this tab and return to the app.</p>"
               "</div></body></html>";
    } else {
        body = "<!doctype html><html><body>Not found</body></html>";
    }
    const QByteArray bytes = body.toUtf8();
    sock->write(QByteArray(is_callback ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n") +
                "Content-Type: text/html; charset=utf-8\r\n" + "Content-Length: " +
                QByteArray::number(bytes.size()) + "\r\n" + "Connection: close\r\n\r\n" + bytes);
    sock->disconnectFromHost();
    if (!is_callback) {
        return;
    }

    const QString state = q.queryItemValue("state");
    const QString code = q.queryItemValue("code");
    const QString error = q.queryItemValue("error");
    const bool canceled = q.queryItemValue("canceled") == "true";
    const int tone_id = q.queryItemValue("tone_id").toInt();

    if (state != auth_state) {
        finish_auth(false, "Sign-in response did not match the request (state).", 0);
        return;
    }
    if (canceled && code.isEmpty()) {
        finish_auth(false, "Sign-in canceled.", 0);
        return;
    }
    if (!error.isEmpty()) {
        finish_auth(false, "TONE3000 refused the sign-in: " + error, 0);
        return;
    }
    if (code.isEmpty()) {
        finish_auth(false, "No authorization code returned.", 0);
        return;
    }
    exchange_code(code, tone_id);
}

void Tone3000Client::exchange_code(const QString& code, int tone_id) {
    QUrlQuery form;
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code", code);
    form.addQueryItem("code_verifier", auth_verifier);
    form.addQueryItem("redirect_uri", auth_redirect);
    form.addQueryItem("client_id", client_id());

    Request req;
    req.method = "POST";
    req.url = QUrl(api_base() + "/oauth/token");
    req.body = form.query(QUrl::FullyEncoded).toUtf8();
    req.use_cache = false;
    req.cb = [this, tone_id](const Result& r) {
        on_code_exchanged(r, tone_id);
    };
    send(std::move(req));
}

void Tone3000Client::on_code_exchanged(const Result& r, int tone_id) {
    if (!r.ok) {
        finish_auth(false, "Token exchange failed: " + r.error, 0);
        return;
    }
    apply_token_response(r.object());
    emit auth_changed();
    fetch_current_user([this, tone_id](const Result&) {
        finish_auth(true, {}, tone_id);
    });
}

void Tone3000Client::finish_auth(bool ok, const QString& error, int tone_id) {
    // The socket handler may still be on the stack: tear the servers down
    // on the next event loop turn.
    QTimer::singleShot(0, this, [this] {
        cancel_auth();
    });
    emit auth_finished(ok, error, tone_id);
}

void Tone3000Client::ensure_fresh_token(std::function<void(bool)> then) {
    if (!is_signed_in()) {
        then(false);
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < expires_at_ms - 60 * 1000) {
        then(true);
        return;
    }
    refresh_access_token(std::move(then));
}

void Tone3000Client::refresh_access_token(std::function<void(bool)> then) {
    if (refresh_token.isEmpty()) {
        then(false);
        return;
    }
    waiting_for_token << std::move(then);
    if (refreshing) {
        return;
    }
    refreshing = true;

    QUrlQuery form;
    form.addQueryItem("grant_type", "refresh_token");
    form.addQueryItem("refresh_token", refresh_token);
    form.addQueryItem("client_id", client_id());

    Request req;
    req.method = "POST";
    req.url = QUrl(api_base() + "/oauth/token");
    req.body = form.query(QUrl::FullyEncoded).toUtf8();
    req.use_cache = false;
    req.cb = [this](const Result& r) {
        on_token_refreshed(r);
    };
    send(std::move(req));
}

void Tone3000Client::on_token_refreshed(const Result& r) {
    refreshing = false;
    bool ok = false;
    if (r.ok) {
        apply_token_response(r.object());
        ok = true;
    } else if (r.status == 400 || r.status == 401) {
        // invalid_grant: the refresh token is dead — back to signed out.
        qCWarning(lc_audio) << "TONE3000 refresh token rejected:" << r.error;
        clear_tokens();
        emit auth_changed();
        emit user_changed();
    }
    const QList<std::function<void(bool)>> waiting = std::move(waiting_for_token);
    waiting_for_token.clear();
    for (const auto& w : waiting) {
        w(ok);
    }
}

void Tone3000Client::send(Request req) {
    const bool is_token_call = req.url.path().endsWith("/oauth/token");

    if (req.method == "GET" && req.use_cache) {
        const auto it = cache.constFind(req.url.toString());
        if (it != cache.constEnd() &&
            QDateTime::currentMSecsSinceEpoch() - it->at_ms < CACHE_TTL_MS) {
            Result r;
            r.ok = true;
            r.status = 200;
            r.json = it->doc;
            req.cb(r);
            return;
        }
    }

    if (is_token_call) {
        start_request(req);
        return;
    }

    // Bearer calls: refresh first when the token is about to expire.
    ensure_fresh_token([this, req](bool fresh) {
        on_token_checked(req, fresh);
    });
}

void Tone3000Client::on_token_checked(const Request& req, bool fresh) {
    if (!fresh) {
        Result r;
        r.auth_required = true;
        r.status = 401;
        r.error = "Sign in to TONE3000 first.";
        req.cb(r);
        return;
    }
    start_request(req);
}

void Tone3000Client::start_request(const Request& req) {
    QNetworkRequest nr(req.url);
    nr.setHeader(QNetworkRequest::UserAgentHeader,
                 "NAMPlayer/1.0 (+https://github.com/silvercobraa/nam-player)");
    nr.setRawHeader("Accept", "application/json");
    nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    nr.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    if (!req.url.path().endsWith("/oauth/token") && is_signed_in()) {
        nr.setRawHeader("Authorization", "Bearer " + access_token.toUtf8());
    }

    QNetworkReply* reply = nullptr;
    if (req.method == "GET") {
        reply = nam->get(nr);
    } else if (req.method == "PUT") {
        reply = nam->put(nr, QByteArray());
    } else if (req.method == "DELETE") {
        reply = nam->deleteResource(nr);
    } else {
        nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = nam->post(nr, req.body);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, req] {
        on_reply_finished(reply, req);
    });
}

// A 401 triggered a token refresh: resend `req`, or report the expired session.
void Tone3000Client::retry_after_refresh(Request req, bool refreshed) {
    if (refreshed) {
        send(std::move(req));
        return;
    }
    Result r;
    r.status = 401;
    r.auth_required = true;
    r.error = "Your TONE3000 session has expired. Sign in again.";
    req.cb(r);
}

void Tone3000Client::on_reply_finished(QNetworkReply* reply, Request req) {
    reply->deleteLater();
    Result r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    r.json = QJsonDocument::fromJson(body);

    if (reply->error() != QNetworkReply::NoError && r.status == 0) {
        switch (reply->error()) {
            case QNetworkReply::ConnectionRefusedError:
            case QNetworkReply::RemoteHostClosedError:
            case QNetworkReply::HostNotFoundError:
            case QNetworkReply::TimeoutError:
            case QNetworkReply::TemporaryNetworkFailureError:
            case QNetworkReply::NetworkSessionFailedError:
            case QNetworkReply::UnknownNetworkError:
            case QNetworkReply::ProxyConnectionRefusedError:
            case QNetworkReply::ProxyNotFoundError:
                r.offline = true;
                break;
            default:
                break;
        }
        r.error = reply->errorString();
        req.cb(r);
        return;
    }

    if (r.status == 401 && !req.retried401 && !req.url.path().endsWith("/oauth/token") &&
        !refresh_token.isEmpty()) {
        // Expiry race between the pre-check and the request: refresh and retry once.
        req.retried401 = true;
        Request again = req;
        refresh_access_token([this, again](bool ok) {
            retry_after_refresh(again, ok);
        });
        return;
    }

    if (r.status == 429) {
        const int retry_after = reply->rawHeader("Retry-After").toInt();
        if (!req.retried429 && retry_after > 0 && retry_after <= MAX_RETRY_AFTER_S) {
            req.retried429 = true;
            emit status(QString("TONE3000 rate limit reached, retrying in %1 s…").arg(retry_after));
            Request again = req;
            QTimer::singleShot(retry_after * 1000, this, [this, again]() mutable {
                send(std::move(again));
            });
            return;
        }
        r.rate_limited = true;
        r.error = retry_after > 0
                      ? QString("Rate limited by TONE3000, try again in %1 s.").arg(retry_after)
                      : QStringLiteral("Rate limited by TONE3000, try again in a minute.");
        req.cb(r);
        return;
    }

    if (r.status >= 200 && r.status < 300) {
        r.ok = true;
        if (req.method == "GET" && req.use_cache) {
            cache.insert(req.url.toString(), {r.json, QDateTime::currentMSecsSinceEpoch()});
        }
        req.cb(r);
        return;
    }

    if (r.status == 401) {
        r.auth_required = true;
    }
    const QJsonObject err = r.json.object();
    QString msg = err.value("error_description").toString();
    if (msg.isEmpty()) {
        msg = err.value("message").toString();
    }
    if (msg.isEmpty()) {
        msg = err.value("error").toString();
    }
    if (msg.isEmpty()) {
        msg = reply->errorString();
    }
    if (r.status == 403) {
        msg = "TONE3000 refused this request (403): " + msg;
    }
    r.error = msg.isEmpty() ? QString("HTTP %1").arg(r.status) : msg;
    req.cb(r);
}

void Tone3000Client::invalidate_cache(const QString& path_prefix) {
    if (path_prefix.isEmpty()) {
        cache.clear();
        return;
    }
    const QString prefix = api_base() + path_prefix;
    for (auto it = cache.begin(); it != cache.end();) {
        if (it.key().startsWith(prefix)) {
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

QString Tone3000Client::list_path(ListKind k) {
    switch (k) {
        case ListKind::TRENDING:
            return "/tones/trending";
        case ListKind::LATEST:
            return "/tones/latest";
        case ListKind::FAVORITED:
            return "/tones/favorited";
        case ListKind::DOWNLOADED:
            return "/tones/downloaded";
        case ListKind::CREATED:
            return "/tones/created";
    }
    return "/tones/latest";
}

void Tone3000Client::list_tones(ListKind kind, int page, Callback cb, bool use_cache) {
    Request req;
    req.method = "GET";
    QUrl url(api_base() + list_path(kind));
    if (kind == ListKind::FAVORITED || kind == ListKind::DOWNLOADED || kind == ListKind::CREATED) {
        QUrlQuery q;
        q.addQueryItem("page", QString::number(std::max(1, page)));
        q.addQueryItem("page_size", "25");
        url.setQuery(q);
    }
    req.url = url;
    req.use_cache = use_cache;
    req.cb = std::move(cb);
    send(std::move(req));
}

void Tone3000Client::fetch_tone(int tone_id, Callback cb) {
    Request req;
    req.method = "GET";
    req.url = QUrl(api_base() + "/tones/" + QString::number(tone_id));
    req.cb = std::move(cb);
    send(std::move(req));
}

void Tone3000Client::fetch_model(int model_id, Callback cb) {
    Request req;
    req.method = "GET";
    req.url = QUrl(api_base() + "/models/" + QString::number(model_id));
    req.use_cache = false; // model_url may be time-limited
    req.cb = std::move(cb);
    send(std::move(req));
}

// list_models runs one request per architecture; their answers meet here.
struct ModelsBatch
{
    QList<T3kModel> models;
    qsizetype pending = 0; // requests still running
    Tone3000Client::Result failed; // first failure, reported if nothing arrived
    std::function<void(const Tone3000Client::Result&, QList<T3kModel>)> cb;
};

// Merges one page into the batch (skipping duplicates); the last one calls back.
static void add_models_page(ModelsBatch& batch, const Tone3000Client::Result& r) {
    if (r.ok) {
        for (const QJsonValue v : r.object().value("data").toArray()) {
            const T3kModel m = Tone3000Client::parse_model(v.toObject());
            const bool dup =
                std::any_of(batch.models.cbegin(), batch.models.cend(), [&m](const T3kModel& x) {
                    return x.id == m.id;
                });
            if (!dup) {
                batch.models.append(m);
            }
        }
    } else if (!batch.failed.status && batch.failed.error.isEmpty()) {
        batch.failed = r;
    }
    if (--batch.pending == 0) {
        if (!batch.models.isEmpty() || batch.failed.error.isEmpty()) {
            Tone3000Client::Result ok;
            ok.ok = true;
            ok.status = 200;
            batch.cb(ok, batch.models);
        } else {
            batch.cb(batch.failed, {});
        }
    }
}

void Tone3000Client::list_models(const T3kTone& tone,
                                 std::function<void(const Result&, QList<T3kModel>)> cb) {
    // NAM tones keep A2 files apart from the legacy A1/custom set; the
    // endpoint returns one set per call. IR and other formats: one plain call.
    QStringList archs;
    if (tone.format == "nam") {
        if (tone.a2_count > 0) {
            archs << "2";
        }
        if (tone.a1_count + tone.custom_count > 0) {
            archs << "";
        }
        if (archs.isEmpty()) {
            archs << "2" << "";
        }
    } else {
        archs << "";
    }

    auto batch = std::make_shared<ModelsBatch>();
    batch->pending = archs.size();
    batch->cb = std::move(cb);

    for (const QString& arch : archs) {
        Request req;
        req.method = "GET";
        QUrl url(api_base() + "/models");
        QUrlQuery q;
        q.addQueryItem("tone_id", QString::number(tone.id));
        q.addQueryItem("page_size", "300");
        if (!arch.isEmpty()) {
            q.addQueryItem("architecture", arch);
        }
        url.setQuery(q);
        req.url = url;
        req.use_cache = false; // model_url values are fetched fresh
        req.cb = [batch](const Result& r) {
            add_models_page(*batch, r);
        };
        send(std::move(req));
    }
}

void Tone3000Client::set_favorite(int tone_id, bool on, Callback cb) {
    Request req;
    req.method = on ? "PUT" : "DELETE";
    req.url = QUrl(api_base() + "/tones/" + QString::number(tone_id) + "/favorite");
    req.use_cache = false;
    req.cb = [this, cb](const Result& r) {
        on_favorite_set(r, cb);
    };
    send(std::move(req));
}

void Tone3000Client::fetch_current_user(Callback cb) {
    Request req;
    req.method = "GET";
    req.url = QUrl(api_base() + "/user");
    req.use_cache = false;
    req.cb = [this, cb](const Result& r) {
        on_user_fetched(r, cb);
    };
    send(std::move(req));
}

void Tone3000Client::on_favorite_set(const Result& r, const Callback& cb) {
    if (r.ok) {
        invalidate_cache("/tones/favorited");
    }
    cb(r);
}

void Tone3000Client::on_user_fetched(const Result& r, const Callback& cb) {
    if (r.ok) {
        user = parse_user(r.object());
        save_tokens();
        emit user_changed();
    }
    cb(r);
}

QString Tone3000Client::local_path_for(const T3kModel& model, const T3kTone& tone) {
    QString ext = QFileInfo(QUrl(model.model_url).path()).suffix().toLower();
    if (ext.isEmpty()) {
        ext = tone.format == "ir" ? "wav" : "nam";
    }
    const QString creator =
        sanitize_component(tone.user.username.isEmpty() ? "unknown" : tone.user.username);
    return download_dir() + "/" + creator + "/" + sanitize_component(tone.title) + "/" +
           sanitize_component(model.name) + "." + ext;
}

void Tone3000Client::download_model(const T3kModel& model, const T3kTone& tone,
                                    std::function<void(bool, QString, QString)> cb) {
    if (model.model_url.isEmpty()) {
        cb(false, {}, "This model has no download URL.");
        return;
    }
    ensure_fresh_token([this, model, tone, cb](bool fresh) {
        start_download(fresh, model, tone, cb);
    });
}

void Tone3000Client::start_download(bool fresh, const T3kModel& model, const T3kTone& tone,
                                    const DownloadCallback& cb) {
    if (!fresh) {
        cb(false, {}, "Sign in to TONE3000 first.");
        return;
    }
    download_step(QUrl(model.model_url), true, 0, model, tone, cb);
}

void Tone3000Client::download_step(QUrl url, bool with_auth, int hops, const T3kModel& model,
                                   const T3kTone& tone,
                                   std::function<void(bool, QString, QString)> cb) {
    if (hops > 5) {
        cb(false, {}, "Too many redirects.");
        return;
    }

    QNetworkRequest nr(url);
    nr.setHeader(QNetworkRequest::UserAgentHeader, "NAMPlayer/1.0");
    nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    nr.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // Redirects are followed by hand so the Bearer header never travels past
    // the first hop: model_url lives on the API host and redirects to signed
    // storage, which rejects any extra authentication.
    nr.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                    QNetworkRequest::ManualRedirectPolicy);
    const bool api_host = url.host() == QUrl(api_base()).host();
    if (with_auth && hops == 0 && api_host) {
        nr.setRawHeader("Authorization", "Bearer " + access_token.toUtf8());
    }

    QNetworkReply* reply = nam->get(nr);
    const int model_id = model.id;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, model_id](qint64 rec, qint64 tot) {
                emit download_progress(model_id, rec, tot);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply, hops, model, tone, cb] {
        on_download_finished(reply, hops, model, tone, cb);
    });
}

void Tone3000Client::on_download_finished(QNetworkReply* reply, int hops, const T3kModel& model,
                                          const T3kTone& tone, const DownloadCallback& cb) {
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (status >= 300 && status < 400 && !target.isEmpty()) {
        download_step(reply->url().resolved(target), false, hops + 1, model, tone, cb);
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
        cb(false, {},
           status ? QString("Download failed (HTTP %1).").arg(status) : reply->errorString());
        return;
    }

    const QString path = local_path_for(model, tone);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        cb(false, path, "Cannot write " + path);
        return;
    }
    f.write(reply->readAll());
    if (!f.commit()) {
        cb(false, path, "Cannot write " + path);
        return;
    }

    QJsonObject side;
    side["tone_id"] = tone.id;
    side["model_id"] = model.id;
    side["tone_title"] = tone.title;
    side["model_name"] = model.name;
    side["creator"] = tone.user.username;
    side["creator_url"] = tone.user.url;
    side["license"] = tone.license;
    side["url"] = tone.url;
    side["gear"] = tone.gear;
    side["format"] = tone.format;
    side["image"] = tone.image_url();
    side["downloaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSaveFile sf(sidecar_path(path));
    if (sf.open(QIODevice::WriteOnly)) {
        sf.write(QJsonDocument(side).toJson());
        sf.commit();
    }
    invalidate_cache("/tones/downloaded");
    cb(true, path, {});
}

// Decodes, scales and caches a fetched image, then hands it over (null on error).
static void on_image_finished(QNetworkReply* reply, const QString& key, const QSize& max_size,
                              const std::function<void(QPixmap)>& cb) {
    reply->deleteLater();
    QPixmap px;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray bytes = reply->readAll();
        QImage img = QImage::fromData(bytes);
        if (!img.isNull()) {
            if (img.width() > max_size.width() || img.height() > max_size.height()) {
                img = img.scaled(max_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            px = QPixmap::fromImage(img);
            QPixmapCache::insert(key, px);
        }
    }
    cb(px);
}

void Tone3000Client::fetch_image(const QString& url, const QSize& max_size,
                                 std::function<void(QPixmap)> cb) {
    if (url.isEmpty()) {
        cb(QPixmap());
        return;
    }
    const QString key =
        QString("t3k:%1:%2x%3").arg(url).arg(max_size.width()).arg(max_size.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) {
        cb(cached);
        return;
    }

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::UserAgentHeader, "NAMPlayer/1.0");
    nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = nam->get(nr);
    connect(reply, &QNetworkReply::finished, this, [reply, key, max_size, cb] {
        on_image_finished(reply, key, max_size, cb);
    });
}

} // namespace npl

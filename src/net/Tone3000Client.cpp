#include "Tone3000Client.h"
#include "Log.h"

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

#include <algorithm>

#ifndef NAM_TONE3000_CLIENT_ID
#define NAM_TONE3000_CLIENT_ID ""
#endif

namespace {

constexpr qint64 kCacheTtlMs      = 5 * 60 * 1000;
constexpr int    kAuthTimeoutMs   = 5 * 60 * 1000;
constexpr int    kMaxRetryAfterS  = 30;
constexpr int    kPreferredPorts[] = {51731, 51732, 51733, 51734};

QString base64url(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding
                                              | QByteArray::OmitTrailingEquals));
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(b.data()), n / 4);
    return b;
}

QString titleCaseGear(const QString& gear)
{
    if (gear == "amp-cab" || gear == "full-rig") return "Amp + Cab";
    if (gear == "amp")          return "Amp";
    if (gear == "pedal")        return "Pedal";
    if (gear == "outboard")     return "Outboard";
    if (gear == "cab")          return "Cab";
    if (gear == "space")        return "Space";
    if (gear == "experimental") return "Experimental";
    if (gear == "ir")           return "IR";
    return gear;
}

// File-system safe component: no separators, control chars or Windows
// reserved punctuation; trimmed; bounded.
QString sanitizeComponent(QString s)
{
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (QChar& c : s)
        if (bad.contains(c) || c.unicode() < 32) c = QChar('_');
    s = s.simplified();
    while (s.endsWith('.') || s.endsWith(' ')) s.chop(1);
    if (s.size() > 100) s.truncate(100);
    if (s.isEmpty()) s = "untitled";
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Data helpers
// ---------------------------------------------------------------------------

QString T3kTone::gearLabel() const   { return titleCaseGear(gear); }
QString T3kTone::formatLabel() const { return format.toUpper(); }
QString T3kTone::licenseLabel() const
{
    if (license == "t3k") return "TONE3000 license";
    if (license == "cco" || license == "cc0") return "CC0";
    if (license.startsWith("cc-")) return license.toUpper().replace('-', ' ').replace("CC ", "CC ");
    return license;
}

T3kUser Tone3000Client::parseUser(const QJsonObject& o)
{
    T3kUser u;
    u.id          = o.value("id").toInt();
    u.username    = o.value("username").toString();
    u.displayName = o.value("display_name").toString();
    u.avatarUrl   = o.value("avatar_url").toString();
    u.url         = o.value("url").toString();
    return u;
}

T3kTone Tone3000Client::parseTone(const QJsonObject& o)
{
    T3kTone t;
    t.id          = o.value("id").toInt();
    t.title       = o.value("title").toString();
    t.description = o.value("description").toString();
    t.gear        = o.value("gear").toString();
    t.format      = o.value("format").toString();
    t.license     = o.value("license").toString();
    t.url         = o.value("url").toString();
    for (const QJsonValue v : o.value("images").toArray()) t.images << v.toString();
    t.user        = parseUser(o.value("user").toObject());
    for (const QJsonValue v : o.value("makes").toArray()) t.makes << v.toObject().value("name").toString();
    for (const QJsonValue v : o.value("tags").toArray())  t.tags  << v.toObject().value("name").toString();
    t.modelsCount = o.value("models_count").toInt();
    t.a1Count     = o.value("a1_models_count").toInt();
    t.a2Count     = o.value("a2_models_count").toInt();
    t.customCount = o.value("custom_models_count").toInt();
    t.irsCount    = o.value("irs_count").toInt();
    t.downloads   = o.value("downloads_count").toInt();
    t.favorites   = o.value("favorites_count").toInt();
    t.isFavorite  = o.value("is_favorite").toBool();
    return t;
}

T3kModel Tone3000Client::parseModel(const QJsonObject& o)
{
    T3kModel m;
    m.id           = o.value("id").toInt();
    m.toneId       = o.value("tone_id").toInt();
    m.name         = o.value("name").toString();
    m.modelUrl     = o.value("model_url").toString();
    m.size         = o.value("size").toString();
    m.architecture = o.value("architecture_version").toString();
    return m;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

QString Tone3000Client::apiBase()
{
    static const QString base = [] {
        const QByteArray env = qgetenv("NAM_TONE3000_API_BASE");
        return env.isEmpty() ? QStringLiteral("https://www.tone3000.com/api/v1")
                             : QString::fromUtf8(env).trimmed();
    }();
    return base;
}

QString Tone3000Client::clientId()
{
    const QString fromSettings = QSettings().value("tone3000/clientId").toString().trimmed();
    if (!fromSettings.isEmpty()) return fromSettings;
    return QString::fromUtf8(NAM_TONE3000_CLIENT_ID).trimmed();
}

void Tone3000Client::setClientId(const QString& id)
{
    QSettings s;
    if (id.trimmed().isEmpty()) s.remove("tone3000/clientId");
    else                        s.setValue("tone3000/clientId", id.trimmed());
}

QString Tone3000Client::downloadDir()
{
    const QString v = QSettings().value("library/tone3000Dir").toString();
    if (!v.isEmpty()) return QDir::cleanPath(v);
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
           + "/NAM Player/TONE3000";
}

// ---------------------------------------------------------------------------
// Construction / tokens
// ---------------------------------------------------------------------------

Tone3000Client::Tone3000Client(QObject* parent)
    : QObject(parent)
    , mNam(new QNetworkAccessManager(this))
{
    // Disk cache for images (the API answers carry no-store; they go through
    // mCache instead).
    auto* disk = new QNetworkDiskCache(this);
    disk->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                            + "/tone3000");
    disk->setMaximumCacheSize(64 * 1024 * 1024);
    mNam->setCache(disk);
    mNam->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    mNam->setTransferTimeout(30000);

    loadTokens();
    if (isSignedIn())
        fetchCurrentUser([](const Result&) {});
}

Tone3000Client::~Tone3000Client()
{
    cancelAuth();
}

void Tone3000Client::loadTokens()
{
    QSettings s;
    mAccessToken  = s.value("tone3000/accessToken").toString();
    mRefreshToken = s.value("tone3000/refreshToken").toString();
    mExpiresAtMs  = s.value("tone3000/expiresAt").toLongLong();
    mUser.id          = s.value("tone3000/user/id").toInt();
    mUser.username    = s.value("tone3000/user/username").toString();
    mUser.displayName = s.value("tone3000/user/displayName").toString();
    mUser.avatarUrl   = s.value("tone3000/user/avatarUrl").toString();
    mUser.url         = s.value("tone3000/user/url").toString();
}

void Tone3000Client::saveTokens()
{
    QSettings s;
    s.setValue("tone3000/accessToken",  mAccessToken);
    s.setValue("tone3000/refreshToken", mRefreshToken);
    s.setValue("tone3000/expiresAt",    mExpiresAtMs);
    s.setValue("tone3000/user/id",          mUser.id);
    s.setValue("tone3000/user/username",    mUser.username);
    s.setValue("tone3000/user/displayName", mUser.displayName);
    s.setValue("tone3000/user/avatarUrl",   mUser.avatarUrl);
    s.setValue("tone3000/user/url",         mUser.url);
}

void Tone3000Client::clearTokens()
{
    mAccessToken.clear();
    mRefreshToken.clear();
    mExpiresAtMs = 0;
    mUser = {};
    QSettings s;
    s.remove("tone3000/accessToken");
    s.remove("tone3000/refreshToken");
    s.remove("tone3000/expiresAt");
    s.remove("tone3000/user");
    mCache.clear();
}

void Tone3000Client::applyTokenResponse(const QJsonObject& o)
{
    mAccessToken = o.value("access_token").toString();
    const QString refresh = o.value("refresh_token").toString();
    if (!refresh.isEmpty()) mRefreshToken = refresh;
    const int expiresIn = o.value("expires_in").toInt(3600);
    mExpiresAtMs = QDateTime::currentMSecsSinceEpoch() + qint64(expiresIn) * 1000;
    saveTokens();
}

void Tone3000Client::signOut()
{
    cancelAuth();
    const bool was = isSignedIn();
    clearTokens();
    if (was) emit authChanged();
    emit userChanged();
}

// ---------------------------------------------------------------------------
// OAuth (PKCE + loopback redirect)
// ---------------------------------------------------------------------------

void Tone3000Client::startAuth(Flow flow, const QHash<QString, QString>& extra)
{
    if (!hasClientId())
    {
        emit authFinished(false, "No TONE3000 publishable key configured.", 0);
        return;
    }
    cancelAuth();

    // Loopback server on a known port so the redirect URI can be registered
    // verbatim; both IPv4 and IPv6 since the browser resolves "localhost".
    mAuthServer   = new QTcpServer(this);
    mAuthServerV6 = new QTcpServer(this);
    quint16 port = 0;
    for (int p : kPreferredPorts)
    {
        if (mAuthServer->listen(QHostAddress::LocalHost, static_cast<quint16>(p)))
        {
            port = static_cast<quint16>(p);
            break;
        }
    }
    if (port == 0 && mAuthServer->listen(QHostAddress::LocalHost, 0))
        port = mAuthServer->serverPort();
    if (port == 0)
    {
        cancelAuth();
        emit authFinished(false, "Could not open a local port for the sign-in callback.", 0);
        return;
    }
    if (!mAuthServerV6->listen(QHostAddress::LocalHostIPv6, port))
    {
        mAuthServerV6->deleteLater();
        mAuthServerV6 = nullptr;
    }
    connect(mAuthServer, &QTcpServer::newConnection, this, &Tone3000Client::onAuthConnection);
    if (mAuthServerV6)
        connect(mAuthServerV6, &QTcpServer::newConnection, this, &Tone3000Client::onAuthConnection);

    mAuthFlow     = flow;
    mAuthRedirect = QString("http://localhost:%1/callback").arg(port);
    mAuthVerifier = base64url(randomBytes(32));
    mAuthState    = base64url(randomBytes(16));
    const QString challenge = base64url(
        QCryptographicHash::hash(mAuthVerifier.toLatin1(), QCryptographicHash::Sha256));

    QUrl url(apiBase() + "/oauth/authorize");
    QUrlQuery q;
    q.addQueryItem("client_id",             clientId());
    q.addQueryItem("redirect_uri",          mAuthRedirect);
    q.addQueryItem("response_type",         "code");
    q.addQueryItem("code_challenge",        challenge);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state",                 mAuthState);
    if (flow == Flow::SelectTone)
    {
        q.addQueryItem("prompt",  "select_tone");
        q.addQueryItem("menubar", "true");
        q.addQueryItem("preview", "true");
    }
    for (auto it = extra.cbegin(); it != extra.cend(); ++it)
        q.addQueryItem(it.key(), it.value());
    url.setQuery(q);

    mAuthTimeout = new QTimer(this);
    mAuthTimeout->setSingleShot(true);
    connect(mAuthTimeout, &QTimer::timeout, this, [this] {
        finishAuth(false, "Sign-in timed out.", 0);
    });
    mAuthTimeout->start(kAuthTimeoutMs);

    // NAM_TONE3000_NO_BROWSER=1 (development): log the URL instead of
    // opening it, so a script can drive the callback.
    if (!qEnvironmentVariableIsEmpty("NAM_TONE3000_NO_BROWSER"))
    {
        qCInfo(lcAudio) << "TONE3000 authorize URL:" << url.toString(QUrl::FullyEncoded);
        return;
    }
    if (!QDesktopServices::openUrl(url))
        finishAuth(false, "Could not open the browser.", 0);
}

void Tone3000Client::cancelAuth()
{
    for (QTcpServer** s : {&mAuthServer, &mAuthServerV6})
    {
        if (*s)
        {
            (*s)->close();
            (*s)->deleteLater();
            *s = nullptr;
        }
    }
    if (mAuthTimeout)
    {
        mAuthTimeout->stop();
        mAuthTimeout->deleteLater();
        mAuthTimeout = nullptr;
    }
}

void Tone3000Client::onAuthConnection()
{
    auto* server = qobject_cast<QTcpServer*>(sender());
    if (!server) return;
    while (QTcpSocket* sock = server->nextPendingConnection())
    {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            const QByteArray data = sock->peek(64 * 1024);
            const int eol = data.indexOf("\r\n");
            if (eol < 0) return; // request line not complete yet
            sock->readAll();

            // "GET /callback?code=…&state=… HTTP/1.1"
            const QList<QByteArray> parts = data.left(eol).split(' ');
            const QUrl reqUrl = parts.size() >= 2 ? QUrl("http://localhost" + QString::fromLatin1(parts[1]))
                                                  : QUrl();
            const QUrlQuery q(reqUrl);

            QString body;
            const bool isCallback = reqUrl.path() == "/callback";
            if (isCallback)
                body = "<!doctype html><html><head><meta charset=\"utf-8\"><title>NAM Player</title>"
                       "<style>body{font-family:sans-serif;background:#15171c;color:#d8dee9;"
                       "display:flex;align-items:center;justify-content:center;height:100vh;margin:0}"
                       "div{text-align:center}h1{font-weight:600}</style></head><body><div>"
                       "<h1>NAM Player</h1><p>You can close this tab and return to the app.</p>"
                       "</div></body></html>";
            else
                body = "<!doctype html><html><body>Not found</body></html>";
            const QByteArray bytes = body.toUtf8();
            sock->write(QByteArray(isCallback ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n")
                        + "Content-Type: text/html; charset=utf-8\r\n"
                        + "Content-Length: " + QByteArray::number(bytes.size()) + "\r\n"
                        + "Connection: close\r\n\r\n" + bytes);
            sock->disconnectFromHost();
            if (!isCallback) return;

            const QString state    = q.queryItemValue("state");
            const QString code     = q.queryItemValue("code");
            const QString error    = q.queryItemValue("error");
            const bool    canceled = q.queryItemValue("canceled") == "true";
            const int     toneId   = q.queryItemValue("tone_id").toInt();

            if (state != mAuthState)         { finishAuth(false, "Sign-in response did not match the request (state).", 0); return; }
            if (canceled && code.isEmpty())  { finishAuth(false, "Sign-in canceled.", 0); return; }
            if (!error.isEmpty())            { finishAuth(false, "TONE3000 refused the sign-in: " + error, 0); return; }
            if (code.isEmpty())              { finishAuth(false, "No authorization code returned.", 0); return; }
            exchangeCode(code, toneId);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void Tone3000Client::exchangeCode(const QString& code, int toneId)
{
    QUrlQuery form;
    form.addQueryItem("grant_type",    "authorization_code");
    form.addQueryItem("code",          code);
    form.addQueryItem("code_verifier", mAuthVerifier);
    form.addQueryItem("redirect_uri",  mAuthRedirect);
    form.addQueryItem("client_id",     clientId());

    Request req;
    req.method = "POST";
    req.url    = QUrl(apiBase() + "/oauth/token");
    req.body   = form.query(QUrl::FullyEncoded).toUtf8();
    req.useCache = false;
    req.cb = [this, toneId](const Result& r) {
        if (!r.ok)
        {
            finishAuth(false, "Token exchange failed: " + r.error, 0);
            return;
        }
        applyTokenResponse(r.object());
        emit authChanged();
        fetchCurrentUser([this, toneId](const Result&) { finishAuth(true, {}, toneId); });
    };
    send(std::move(req));
}

void Tone3000Client::finishAuth(bool ok, const QString& error, int toneId)
{
    // The socket handler may still be on the stack: tear the servers down
    // on the next event loop turn.
    QTimer::singleShot(0, this, [this] { cancelAuth(); });
    emit authFinished(ok, error, toneId);
}

// ---------------------------------------------------------------------------
// Token refresh
// ---------------------------------------------------------------------------

void Tone3000Client::ensureFreshToken(std::function<void(bool)> then)
{
    if (!isSignedIn()) { then(false); return; }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < mExpiresAtMs - 60 * 1000) { then(true); return; }
    refreshToken(std::move(then));
}

void Tone3000Client::refreshToken(std::function<void(bool)> then)
{
    if (mRefreshToken.isEmpty()) { then(false); return; }
    mWaitingForToken << std::move(then);
    if (mRefreshing) return;
    mRefreshing = true;

    QUrlQuery form;
    form.addQueryItem("grant_type",    "refresh_token");
    form.addQueryItem("refresh_token", mRefreshToken);
    form.addQueryItem("client_id",     clientId());

    Request req;
    req.method   = "POST";
    req.url      = QUrl(apiBase() + "/oauth/token");
    req.body     = form.query(QUrl::FullyEncoded).toUtf8();
    req.useCache = false;
    req.cb = [this](const Result& r) {
        mRefreshing = false;
        bool ok = false;
        if (r.ok)
        {
            applyTokenResponse(r.object());
            ok = true;
        }
        else if (r.status == 400 || r.status == 401)
        {
            // invalid_grant: the refresh token is dead — back to signed out.
            qCWarning(lcAudio) << "TONE3000 refresh token rejected:" << r.error;
            clearTokens();
            emit authChanged();
            emit userChanged();
        }
        const auto waiting = std::move(mWaitingForToken);
        mWaitingForToken.clear();
        for (const auto& w : waiting) w(ok);
    };
    send(std::move(req));
}

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

void Tone3000Client::send(Request req)
{
    const bool isTokenCall = req.url.path().endsWith("/oauth/token");

    if (req.method == "GET" && req.useCache)
    {
        const auto it = mCache.constFind(req.url.toString());
        if (it != mCache.constEnd()
            && QDateTime::currentMSecsSinceEpoch() - it->atMs < kCacheTtlMs)
        {
            Result r;
            r.ok = true;
            r.status = 200;
            r.json = it->doc;
            req.cb(r);
            return;
        }
    }

    auto fire = [this, req]() mutable {
        QNetworkRequest nr(req.url);
        nr.setHeader(QNetworkRequest::UserAgentHeader, "NAMPlayer/1.0 (+https://github.com/silvercobraa/nam-player)");
        nr.setRawHeader("Accept", "application/json");
        nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        nr.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        if (!req.url.path().endsWith("/oauth/token") && isSignedIn())
            nr.setRawHeader("Authorization", "Bearer " + mAccessToken.toUtf8());

        QNetworkReply* reply = nullptr;
        if (req.method == "GET")         reply = mNam->get(nr);
        else if (req.method == "PUT")    reply = mNam->put(nr, QByteArray());
        else if (req.method == "DELETE") reply = mNam->deleteResource(nr);
        else
        {
            nr.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            reply = mNam->post(nr, req.body);
        }
        connect(reply, &QNetworkReply::finished, this, [this, reply, req] {
            onReplyFinished(reply, req);
        });
    };

    if (isTokenCall) { fire(); return; }

    // Bearer calls: refresh first when the token is about to expire.
    ensureFreshToken([fire, req](bool fresh) mutable {
        if (!fresh)
        {
            Result r;
            r.authRequired = true;
            r.status = 401;
            r.error = "Sign in to TONE3000 first.";
            req.cb(r);
            return;
        }
        fire();
    });
}

void Tone3000Client::onReplyFinished(QNetworkReply* reply, Request req)
{
    reply->deleteLater();
    Result r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    r.json = QJsonDocument::fromJson(body);

    if (reply->error() != QNetworkReply::NoError && r.status == 0)
    {
        switch (reply->error())
        {
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

    if (r.status == 401 && !req.retried401 && !req.url.path().endsWith("/oauth/token") && !mRefreshToken.isEmpty())
    {
        // Expiry race between the pre-check and the request: refresh and retry once.
        req.retried401 = true;
        Request again = req;
        refreshToken([this, again](bool ok) mutable {
            if (ok) { send(std::move(again)); return; }
            Result rr;
            rr.status = 401;
            rr.authRequired = true;
            rr.error = "Your TONE3000 session has expired. Sign in again.";
            again.cb(rr);
        });
        return;
    }

    if (r.status == 429)
    {
        const int retryAfter = reply->rawHeader("Retry-After").toInt();
        if (!req.retried429 && retryAfter > 0 && retryAfter <= kMaxRetryAfterS)
        {
            req.retried429 = true;
            emit status(QString("TONE3000 rate limit reached, retrying in %1 s…").arg(retryAfter));
            Request again = req;
            QTimer::singleShot(retryAfter * 1000, this, [this, again]() mutable { send(std::move(again)); });
            return;
        }
        r.rateLimited = true;
        r.error = retryAfter > 0 ? QString("Rate limited by TONE3000, try again in %1 s.").arg(retryAfter)
                                 : QStringLiteral("Rate limited by TONE3000, try again in a minute.");
        req.cb(r);
        return;
    }

    if (r.status >= 200 && r.status < 300)
    {
        r.ok = true;
        if (req.method == "GET" && req.useCache)
            mCache.insert(req.url.toString(), {r.json, QDateTime::currentMSecsSinceEpoch()});
        req.cb(r);
        return;
    }

    if (r.status == 401) r.authRequired = true;
    const QJsonObject err = r.json.object();
    QString msg = err.value("error_description").toString();
    if (msg.isEmpty()) msg = err.value("message").toString();
    if (msg.isEmpty()) msg = err.value("error").toString();
    if (msg.isEmpty()) msg = reply->errorString();
    if (r.status == 403) msg = "TONE3000 refused this request (403): " + msg;
    r.error = msg.isEmpty() ? QString("HTTP %1").arg(r.status) : msg;
    req.cb(r);
}

void Tone3000Client::invalidateCache(const QString& pathPrefix)
{
    if (pathPrefix.isEmpty()) { mCache.clear(); return; }
    const QString prefix = apiBase() + pathPrefix;
    for (auto it = mCache.begin(); it != mCache.end();)
    {
        if (it.key().startsWith(prefix)) it = mCache.erase(it);
        else                             ++it;
    }
}

// ---------------------------------------------------------------------------
// API calls
// ---------------------------------------------------------------------------

QString Tone3000Client::listPath(ListKind k)
{
    switch (k)
    {
    case ListKind::Trending:   return "/tones/trending";
    case ListKind::Latest:     return "/tones/latest";
    case ListKind::Favorited:  return "/tones/favorited";
    case ListKind::Downloaded: return "/tones/downloaded";
    case ListKind::Created:    return "/tones/created";
    }
    return "/tones/latest";
}

void Tone3000Client::listTones(ListKind kind, int page, Callback cb, bool useCache)
{
    Request req;
    req.method = "GET";
    QUrl url(apiBase() + listPath(kind));
    if (kind == ListKind::Favorited || kind == ListKind::Downloaded || kind == ListKind::Created)
    {
        QUrlQuery q;
        q.addQueryItem("page", QString::number(std::max(1, page)));
        q.addQueryItem("page_size", "25");
        url.setQuery(q);
    }
    req.url = url;
    req.useCache = useCache;
    req.cb = std::move(cb);
    send(std::move(req));
}

void Tone3000Client::fetchTone(int toneId, Callback cb)
{
    Request req;
    req.method = "GET";
    req.url = QUrl(apiBase() + "/tones/" + QString::number(toneId));
    req.cb = std::move(cb);
    send(std::move(req));
}

void Tone3000Client::fetchModel(int modelId, Callback cb)
{
    Request req;
    req.method = "GET";
    req.url = QUrl(apiBase() + "/models/" + QString::number(modelId));
    req.useCache = false; // model_url may be time-limited
    req.cb = std::move(cb);
    send(std::move(req));
}

void Tone3000Client::listModels(const T3kTone& tone,
                                std::function<void(const Result&, QList<T3kModel>)> cb)
{
    // NAM tones keep A2 files apart from the legacy A1/custom set; the
    // endpoint returns one set per call. IR and other formats: one plain call.
    QStringList archs;
    if (tone.format == "nam")
    {
        if (tone.a2Count > 0)                        archs << "2";
        if (tone.a1Count + tone.customCount > 0)     archs << "";
        if (archs.isEmpty())                         archs << "2" << "";
    }
    else
    {
        archs << "";
    }

    auto models  = std::make_shared<QList<T3kModel>>();
    auto pending = std::make_shared<int>(archs.size());
    auto failed  = std::make_shared<Result>();
    auto cbp     = std::make_shared<decltype(cb)>(std::move(cb));

    for (const QString& arch : archs)
    {
        Request req;
        req.method = "GET";
        QUrl url(apiBase() + "/models");
        QUrlQuery q;
        q.addQueryItem("tone_id", QString::number(tone.id));
        q.addQueryItem("page_size", "300");
        if (!arch.isEmpty()) q.addQueryItem("architecture", arch);
        url.setQuery(q);
        req.url = url;
        req.useCache = false; // model_url values are fetched fresh
        req.cb = [models, pending, failed, cbp](const Result& r) {
            if (r.ok)
            {
                for (const QJsonValue v : r.object().value("data").toArray())
                {
                    const T3kModel m = parseModel(v.toObject());
                    const bool dup = std::any_of(models->cbegin(), models->cend(),
                                                 [&](const T3kModel& x) { return x.id == m.id; });
                    if (!dup) models->append(m);
                }
            }
            else if (!failed->status && failed->error.isEmpty())
            {
                *failed = r;
            }
            if (--*pending == 0)
            {
                if (!models->isEmpty() || failed->error.isEmpty())
                {
                    Result ok;
                    ok.ok = true;
                    ok.status = 200;
                    (*cbp)(ok, *models);
                }
                else
                {
                    (*cbp)(*failed, {});
                }
            }
        };
        send(std::move(req));
    }
}

void Tone3000Client::setFavorite(int toneId, bool on, Callback cb)
{
    Request req;
    req.method = on ? "PUT" : "DELETE";
    req.url = QUrl(apiBase() + "/tones/" + QString::number(toneId) + "/favorite");
    req.useCache = false;
    req.cb = [this, cb](const Result& r) {
        if (r.ok) invalidateCache("/tones/favorited");
        cb(r);
    };
    send(std::move(req));
}

void Tone3000Client::fetchCurrentUser(Callback cb)
{
    Request req;
    req.method = "GET";
    req.url = QUrl(apiBase() + "/user");
    req.useCache = false;
    req.cb = [this, cb](const Result& r) {
        if (r.ok)
        {
            mUser = parseUser(r.object());
            saveTokens();
            emit userChanged();
        }
        cb(r);
    };
    send(std::move(req));
}

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------

QString Tone3000Client::localPathFor(const T3kModel& model, const T3kTone& tone)
{
    QString ext = QFileInfo(QUrl(model.modelUrl).path()).suffix().toLower();
    if (ext.isEmpty()) ext = tone.format == "ir" ? "wav" : "nam";
    const QString creator = sanitizeComponent(tone.user.username.isEmpty() ? "unknown" : tone.user.username);
    return downloadDir() + "/" + creator + "/" + sanitizeComponent(tone.title) + "/"
           + sanitizeComponent(model.name) + "." + ext;
}

void Tone3000Client::downloadModel(const T3kModel& model, const T3kTone& tone,
                                   std::function<void(bool, QString, QString)> cb)
{
    if (model.modelUrl.isEmpty()) { cb(false, {}, "This model has no download URL."); return; }
    ensureFreshToken([this, model, tone, cb](bool fresh) {
        if (!fresh) { cb(false, {}, "Sign in to TONE3000 first."); return; }
        downloadStep(QUrl(model.modelUrl), true, 0, model, tone, cb);
    });
}

void Tone3000Client::downloadStep(QUrl url, bool withAuth, int hops, const T3kModel& model,
                                  const T3kTone& tone, std::function<void(bool, QString, QString)> cb)
{
    if (hops > 5) { cb(false, {}, "Too many redirects."); return; }

    QNetworkRequest nr(url);
    nr.setHeader(QNetworkRequest::UserAgentHeader, "NAMPlayer/1.0");
    nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    nr.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // Redirects are followed by hand so the Bearer header never travels past
    // the first hop: model_url lives on the API host and redirects to signed
    // storage, which rejects any extra authentication.
    nr.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    const bool apiHost = url.host() == QUrl(apiBase()).host();
    if (withAuth && hops == 0 && apiHost)
        nr.setRawHeader("Authorization", "Bearer " + mAccessToken.toUtf8());

    QNetworkReply* reply = mNam->get(nr);
    const int modelId = model.id;
    connect(reply, &QNetworkReply::downloadProgress, this, [this, modelId](qint64 rec, qint64 tot) {
        emit downloadProgress(modelId, rec, tot);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, hops, model, tone, cb] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (status >= 300 && status < 400 && !target.isEmpty())
        {
            downloadStep(reply->url().resolved(target), false, hops + 1, model, tone, cb);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300)
        {
            cb(false, {}, status ? QString("Download failed (HTTP %1).").arg(status)
                                 : reply->errorString());
            return;
        }

        const QString path = localPathFor(model, tone);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly)) { cb(false, path, "Cannot write " + path); return; }
        f.write(reply->readAll());
        if (!f.commit()) { cb(false, path, "Cannot write " + path); return; }

        QJsonObject side;
        side["tone_id"]     = tone.id;
        side["model_id"]    = model.id;
        side["tone_title"]  = tone.title;
        side["model_name"]  = model.name;
        side["creator"]     = tone.user.username;
        side["creator_url"] = tone.user.url;
        side["license"]     = tone.license;
        side["url"]         = tone.url;
        side["gear"]        = tone.gear;
        side["format"]      = tone.format;
        side["image"]       = tone.imageUrl();
        side["downloaded_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QSaveFile sf(sidecarPath(path));
        if (sf.open(QIODevice::WriteOnly))
        {
            sf.write(QJsonDocument(side).toJson());
            sf.commit();
        }
        invalidateCache("/tones/downloaded");
        cb(true, path, {});
    });
}

// ---------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------

void Tone3000Client::fetchImage(const QString& url, const QSize& maxSize,
                                std::function<void(QPixmap)> cb)
{
    if (url.isEmpty()) { cb(QPixmap()); return; }
    const QString key = QString("t3k:%1:%2x%3").arg(url).arg(maxSize.width()).arg(maxSize.height());
    QPixmap cached;
    if (QPixmapCache::find(key, &cached)) { cb(cached); return; }

    QNetworkRequest nr{QUrl(url)};
    nr.setHeader(QNetworkRequest::UserAgentHeader, "NAMPlayer/1.0");
    nr.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
    QNetworkReply* reply = mNam->get(nr);
    connect(reply, &QNetworkReply::finished, this, [reply, key, maxSize, cb] {
        reply->deleteLater();
        QPixmap px;
        if (reply->error() == QNetworkReply::NoError)
        {
            const QByteArray bytes = reply->readAll();
            QImage img = QImage::fromData(bytes);
            if (!img.isNull())
            {
                if (img.width() > maxSize.width() || img.height() > maxSize.height())
                    img = img.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                px = QPixmap::fromImage(img);
                QPixmapCache::insert(key, px);
            }
        }
        cb(px);
    });
}

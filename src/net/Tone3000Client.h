#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;

// ---------------------------------------------------------------------------
// API data (the subset of the TONE3000 v1 schema the app shows)
// ---------------------------------------------------------------------------

struct T3kUser
{
    int     id = 0;
    QString username, displayName, avatarUrl, url;
    bool    isValid() const { return id != 0; }
    QString label() const { return displayName.isEmpty() ? username : displayName; }
};

struct T3kTone
{
    int         id = 0;
    QString     title, description, gear, format, license, url;
    QStringList images;
    T3kUser     user;
    QStringList makes, tags;
    int         modelsCount = 0, a1Count = 0, a2Count = 0, customCount = 0, irsCount = 0;
    int         downloads = 0, favorites = 0;
    bool        isFavorite = false;

    QString gearLabel() const;   // "amp-cab" → "Amp + Cab"
    QString formatLabel() const; // "nam" → "NAM", "ir" → "IR"
    QString licenseLabel() const;
    QString imageUrl() const { return images.isEmpty() ? QString() : images.first(); }
};

struct T3kModel
{
    int     id = 0, toneId = 0;
    QString name, modelUrl, size, architecture; // architecture "1" / "2" / "custom" / ""
};

// ---------------------------------------------------------------------------

/// TONE3000 API client: OAuth 2.0 + PKCE through the system browser and a
/// loopback callback server, Bearer requests with pre-emptive token refresh,
/// a short in-memory listing cache, 429 back-off, model downloads with a
/// `.t3k.json` sidecar, and image fetches through a disk cache. Every
/// callback runs on the GUI thread. Free non-commercial tier only: the
/// bounded list endpoints (trending / latest / favorited / downloaded /
/// created) and the OAuth prompt flows; no catalog search calls.
class Tone3000Client : public QObject
{
    Q_OBJECT
public:
    explicit Tone3000Client(QObject* parent = nullptr);
    ~Tone3000Client() override;

    // --- Configuration -----------------------------------------------------
    /// Publishable key (`t3k_pub_…`): QSettings `tone3000/clientId` overrides
    /// the build-time NAM_TONE3000_CLIENT_ID define.
    static QString clientId();
    static void    setClientId(const QString& id);
    static bool    hasClientId() { return !clientId().isEmpty(); }
    /// Where downloads land (QSettings `library/tone3000Dir`); always part of
    /// the library's model and IR roots.
    static QString downloadDir();

    static constexpr const char* kSiteUrl = "https://www.tone3000.com";
    /// "https://www.tone3000.com/api/v1", or NAM_TONE3000_API_BASE from the
    /// environment (development: point the client at a mock server).
    static QString apiBase();

    // --- Auth --------------------------------------------------------------
    bool           isSignedIn() const { return !mAccessToken.isEmpty(); }
    const T3kUser& user() const { return mUser; }

    enum class Flow { Standard, SelectTone };
    /// Opens the browser. Ends with authFinished(). `extra` are extra
    /// authorize-URL parameters (format, gears…).
    void startAuth(Flow flow, const QHash<QString, QString>& extra = {});
    bool authInProgress() const { return mAuthServer != nullptr; }
    void cancelAuth();
    void signOut();

    // --- Requests ----------------------------------------------------------
    struct Result
    {
        bool          ok = false;
        int           status = 0;
        bool          offline = false;     // no network at all
        bool          rateLimited = false; // 429 and the retry didn't help
        bool          authRequired = false;
        QString       error;               // human-readable
        QJsonDocument json;
        QJsonObject   object() const { return json.object(); }
    };
    using Callback = std::function<void(const Result&)>;

    /// Bounded lists. `page` is ignored by trending/latest (10 items, unpaged).
    enum class ListKind { Trending, Latest, Favorited, Downloaded, Created };
    static QString listPath(ListKind k);
    void listTones(ListKind kind, int page, Callback cb, bool useCache = true);
    void fetchTone(int toneId, Callback cb);
    /// All models of a tone (A2 + legacy sets merged, tone page order).
    void listModels(const T3kTone& tone, std::function<void(const Result&, QList<T3kModel>)> cb);
    void fetchModel(int modelId, Callback cb);
    void setFavorite(int toneId, bool on, Callback cb);
    void fetchCurrentUser(Callback cb);

    // --- Downloads ---------------------------------------------------------
    /// Saves the model file under downloadDir()/<creator>/<tone>/ with a
    /// sidecar; `cb(ok, path, error)`.
    void downloadModel(const T3kModel& model, const T3kTone& tone,
                       std::function<void(bool, QString, QString)> cb);
    /// Destination path a (tone, model) pair maps to — exists ⇒ downloaded.
    static QString localPathFor(const T3kModel& model, const T3kTone& tone);
    static QString sidecarPath(const QString& modelFile) { return modelFile + ".t3k.json"; }

    // --- Images ------------------------------------------------------------
    /// Fetches (disk-cached) and scales to fit `maxSize`; null pixmap on failure.
    void fetchImage(const QString& url, const QSize& maxSize, std::function<void(QPixmap)> cb);

    // --- Parsing -----------------------------------------------------------
    static T3kUser  parseUser(const QJsonObject& o);
    static T3kTone  parseTone(const QJsonObject& o);
    static T3kModel parseModel(const QJsonObject& o);

    void invalidateCache(const QString& pathPrefix = {});

signals:
    void authChanged();                                // signed in / out
    void userChanged();                                // profile arrived
    /// `toneId` > 0 when a select_tone flow picked a tone.
    void authFinished(bool ok, const QString& error, int toneId);
    void downloadProgress(int modelId, qint64 received, qint64 total);
    void status(const QString& message);               // transient UI notices

private:
    struct Request
    {
        QString  method;   // GET / PUT / DELETE / POST
        QUrl     url;
        QByteArray body;   // form-encoded for POST
        Callback cb;
        bool     useCache = true;
        bool     retried401 = false;
        bool     retried429 = false;
    };

    void   send(Request req);
    void   onReplyFinished(QNetworkReply* reply, Request req);
    void   ensureFreshToken(std::function<void(bool)> then);
    void   refreshToken(std::function<void(bool)> then);
    void   applyTokenResponse(const QJsonObject& o);
    void   loadTokens();
    void   saveTokens();
    void   clearTokens();
    void   downloadStep(QUrl url, bool withAuth, int hops, const T3kModel& model,
                        const T3kTone& tone, std::function<void(bool, QString, QString)> cb);

    // OAuth loopback
    void   onAuthConnection();
    void   finishAuth(bool ok, const QString& error, int toneId);
    void   exchangeCode(const QString& code, int toneId);

    QNetworkAccessManager* mNam = nullptr;
    QString mAccessToken, mRefreshToken;
    qint64  mExpiresAtMs = 0;
    T3kUser mUser;
    bool    mRefreshing = false;
    QList<std::function<void(bool)>> mWaitingForToken;

    struct CacheEntry { QJsonDocument doc; qint64 atMs = 0; };
    QHash<QString, CacheEntry> mCache;

    // In-flight auth flow
    QTcpServer* mAuthServer   = nullptr;
    QTcpServer* mAuthServerV6 = nullptr;
    QTimer*     mAuthTimeout  = nullptr;
    QString     mAuthState, mAuthVerifier, mAuthRedirect;
    Flow        mAuthFlow = Flow::Standard;
};

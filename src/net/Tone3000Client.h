#pragma once

#include <functional>

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include "Common.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;
class QTimer;

namespace npl
{

struct T3kUser
{
    int id = 0;
    QString username, display_name, avatar_url, url;
    bool is_valid() const {
        return id != 0;
    }
    QString label() const {
        return display_name.isEmpty() ? username : display_name;
    }
};

struct T3kTone
{
    int id = 0;
    QString title, description, gear, format, license, url;
    QStringList images;
    T3kUser user;
    QStringList makes, tags;
    int models_count = 0, a1_count = 0, a2_count = 0, custom_count = 0, irs_count = 0;
    int downloads = 0, favorites = 0;
    bool is_favorite = false;

    QString gear_label() const; // "amp-cab" → "Amp + Cab"
    QString format_label() const; // "nam" → "NAM", "ir" → "IR"
    QString license_label() const;
    QString image_url() const {
        return images.isEmpty() ? QString() : images.first();
    }
};

struct T3kModel
{
    int id = 0, tone_id = 0;
    QString name, model_url, size, architecture; // architecture "1" / "2" / "custom" / ""
};

// TONE3000 API client: OAuth 2.0 + PKCE through the system browser and a
// loopback callback server, Bearer requests with pre-emptive token refresh,
// a short in-memory listing cache, 429 back-off, model downloads with a
// `.t3k.json` sidecar, and image fetches through a disk cache. Every
// callback runs on the GUI thread. Free non-commercial tier only: the
// bounded list endpoints (trending / latest / favorited / downloaded /
// created) and the OAuth prompt flows; no catalog search calls.
class Tone3000Client : public QObject
{
    Q_OBJECT
public:
    explicit Tone3000Client(QObject* parent = nullptr);
    ~Tone3000Client() override;

    // Configuration
    // Publishable key (`t3k_pub_…`): QSettings `tone3000/clientId` overrides
    // the build-time NAM_TONE3000_CLIENT_ID define.
    static QString client_id();
    static void set_client_id(const QString& id);
    static bool has_client_id() {
        return !client_id().isEmpty();
    }
    // Where downloads land (QSettings `library/tone3000Dir`); always part of
    // the library's model and IR roots.
    static QString download_dir();

    static constexpr const char* SITE_URL = "https://www.tone3000.com";
    // "https://www.tone3000.com/api/v1", or NAM_TONE3000_API_BASE from the
    // environment (development: point the client at a mock server).
    static QString api_base();

    // Auth
    bool is_signed_in() const {
        return !access_token.isEmpty();
    }
    const T3kUser& get_user() const {
        return user;
    }

    enum class Flow
    {
        STANDARD,
        SELECT_TONE
    };
    // Opens the browser. Ends with authFinished(). `extra` are extra
    // authorize-URL parameters (format, gears…).
    void start_auth(Flow flow, const QHash<QString, QString>& extra = {});
    bool auth_in_progress() const {
        return auth_server != nullptr;
    }
    void cancel_auth();
    void sign_out();

    // Requests
    struct Result
    {
        bool ok = false;
        int status = 0;
        bool offline = false; // no network at all
        bool rate_limited = false; // 429 and the retry didn't help
        bool auth_required = false;
        QString error; // human-readable
        QJsonDocument json;
        QJsonObject object() const {
            return json.object();
        }
    };
    using Callback = std::function<void(const Result&)>;
    // (ok, path, error)
    using DownloadCallback = std::function<void(bool, QString, QString)>;

    // Bounded lists. `page` is ignored by trending/latest (10 items, unpaged).
    enum class ListKind
    {
        TRENDING,
        LATEST,
        FAVORITED,
        DOWNLOADED,
        CREATED
    };
    static QString list_path(ListKind k);
    void list_tones(ListKind kind, int page, Callback cb, bool use_cache = true);
    void fetch_tone(int tone_id, Callback cb);
    // All models of a tone (A2 + legacy sets merged, tone page order).
    void list_models(const T3kTone& tone, std::function<void(const Result&, QList<T3kModel>)> cb);
    void fetch_model(int model_id, Callback cb);
    void set_favorite(int tone_id, bool on, Callback cb);
    void fetch_current_user(Callback cb);

    // Downloads
    // Saves the model file under downloadDir()/<creator>/<tone>/ with a
    // sidecar; `cb(ok, path, error)`.
    void download_model(const T3kModel& model, const T3kTone& tone,
                        std::function<void(bool, QString, QString)> cb);
    // Destination path a (tone, model) pair maps to — exists ⇒ downloaded.
    static QString local_path_for(const T3kModel& model, const T3kTone& tone);
    static QString sidecar_path(const QString& model_file) {
        return model_file + ".t3k.json";
    }

    // Images
    // Fetches (disk-cached) and scales to fit `maxSize`; null pixmap on failure.
    void fetch_image(const QString& url, const QSize& max_size, std::function<void(QPixmap)> cb);

    // Parsing
    static T3kUser parse_user(const QJsonObject& o);
    static T3kTone parse_tone(const QJsonObject& o);
    static T3kModel parse_model(const QJsonObject& o);

    void invalidate_cache(const QString& path_prefix = {});

signals:
    void auth_changed(); // signed in / out
    void user_changed(); // profile arrived
    // `toneId` > 0 when a select_tone flow picked a tone.
    void auth_finished(bool ok, const QString& error, int tone_id);
    void download_progress(int model_id, qint64 received, qint64 total);
    void status(const QString& message); // transient UI notices

private:
    struct Request
    {
        QString method; // GET / PUT / DELETE / POST
        QUrl url;
        QByteArray body; // form-encoded for POST
        Callback cb;
        bool use_cache = true;
        bool retried401 = false;
        bool retried429 = false;
    };

    void send(Request req);
    void on_reply_finished(QNetworkReply* reply, Request req);
    void on_token_checked(const Request& req, bool fresh);
    void start_request(const Request& req);
    void retry_after_refresh(Request req, bool refreshed);
    void on_token_refreshed(const Result& r);
    void on_code_exchanged(const Result& r, int tone_id);
    void on_auth_request(QTcpSocket* sock);
    void on_favorite_set(const Result& r, const Callback& cb);
    void on_user_fetched(const Result& r, const Callback& cb);
    void start_download(bool fresh, const T3kModel& model, const T3kTone& tone,
                        const DownloadCallback& cb);
    // A download hop finished: follow the redirect, or save file + sidecar.
    void on_download_finished(QNetworkReply* reply, int hops, const T3kModel& model,
                              const T3kTone& tone, const DownloadCallback& cb);
    void ensure_fresh_token(std::function<void(bool)> then);
    void refresh_access_token(std::function<void(bool)> then);
    void apply_token_response(const QJsonObject& o);
    void load_tokens();
    void save_tokens();
    void clear_tokens();
    void download_step(QUrl url, bool with_auth, int hops, const T3kModel& model,
                       const T3kTone& tone, std::function<void(bool, QString, QString)> cb);

    // OAuth loopback
    void on_auth_connection();
    void finish_auth(bool ok, const QString& error, int tone_id);
    void exchange_code(const QString& code, int tone_id);

    QNetworkAccessManager* nam = nullptr;
    QString access_token, refresh_token;
    qint64 expires_at_ms = 0;
    T3kUser user;
    bool refreshing = false;
    QList<std::function<void(bool)>> waiting_for_token;

    struct CacheEntry
    {
        QJsonDocument doc;
        qint64 at_ms = 0;
    };
    QHash<QString, CacheEntry> cache;

    // In-flight auth flow
    QTcpServer* auth_server = nullptr;
    QTcpServer* auth_server_v6 = nullptr;
    QTimer* auth_timeout = nullptr;
    QString auth_state, auth_verifier, auth_redirect;
    Flow auth_flow = Flow::STANDARD;
};

} // namespace npl

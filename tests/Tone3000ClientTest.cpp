// End-to-end test of Tone3000Client against tests/mock_tone3000.py:
// PKCE sign-in through the loopback callback, Bearer requests, pre-emptive
// and 401-driven token refresh, 429 back-off, favorites, bounded lists,
// tone + models, and a model download with its sidecar (following a
// redirect to "storage" without leaking the Bearer header).
//
// Run:  NAM_TONE3000_API_BASE is set by the test itself; it spawns the mock
//       with python3 and a temporary QSettings scope, so it never touches
//       the user's real configuration.

#include "Tone3000Client.h"

#include <cstdio>
#include <cstdlib>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include "Common.h"

namespace npl
{

#ifndef MOCK_SERVER_PY
#define MOCK_SERVER_PY "mock_tone3000.py"
#endif

static int gFailures = 0;
#define CHECK(cond, msg) \
    do { \
        if (cond) \
            std::printf("  ok   %s\n", msg); \
        else { \
            std::printf("  FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
            ++gFailures; \
        } \
    } while (0)

static const int PORT = 8765;

// Runs the event loop until `done` is set or the timeout hits.
template <typename F>
static bool wait_for(F&& start, int timeout_ms = 8000) {
    QEventLoop loop;
    bool done = false;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeout_ms);
    start([&] {
        done = true;
        loop.quit();
    });
    if (!done) {
        loop.exec();
    }
    return done;
}

static QJsonDocument http_get(QNetworkAccessManager& nam, const QUrl& url) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* r = nam.get(req);
    QEventLoop loop;
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray body = r->readAll();
    r->deleteLater();
    return QJsonDocument::fromJson(body);
}

static QString authorize_url;
static void message_handler(QtMsgType, const QMessageLogContext&, const QString& msg) {
    static const QRegularExpression RE("TONE3000 authorize URL: \"?(\\S+?)\"?$");
    const QRegularExpressionMatch m = RE.match(msg);
    if (m.hasMatch()) {
        authorize_url = m.captured(1);
    }
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

int run(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("NAM_TONE3000_API_BASE", QString("http://127.0.0.1:%1/api/v1").arg(PORT).toUtf8());
    qputenv("NAM_TONE3000_NO_BROWSER", "1");
    QGuiApplication app(argc, argv); // QPixmap needs a GUI application
    app.setOrganizationName("pichulero.cl");
    app.setApplicationName("NAM Player Test");

    // Isolated settings + download dir.
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path() + "/cfg");
    QSettings().clear();
    QSettings().setValue("library/tone3000Dir", tmp.path() + "/dl");
    Tone3000Client::set_client_id("t3k_pub_test");

    // Mock server.
    QProcess mock;
    mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    mock.start("python3", {QString::fromUtf8(MOCK_SERVER_PY), QString::number(PORT)});
    if (!mock.waitForStarted(5000)) {
        std::printf("cannot start python3 mock\n");
        return 2;
    }
    mock.waitForReadyRead(5000); // "mock tone3000 on …"

    qInstallMessageHandler(message_handler);
    QNetworkAccessManager nam;
    Tone3000Client client;

    std::printf("sign-in (select flow)\n");
    CHECK(!client.is_signed_in(), "starts signed out");
    bool auth_ok = false;
    int picked_tone = 0;
    QString auth_err;
    const bool finished = wait_for([&](auto done) {
        QObject::connect(&client, &Tone3000Client::auth_finished, &client,
                         [&, done](bool ok, const QString& err, int tone_id) {
                             auth_ok = ok;
                             auth_err = err;
                             picked_tone = tone_id;
                             done();
                         });
        client.start_auth(Tone3000Client::Flow::SELECT_TONE);
        // "Browser": GET the authorize URL; the mock redirects to our loopback.
        QTimer::singleShot(100, &client, [&] {
            CHECK(!authorize_url.isEmpty(), "authorize URL logged (NO_BROWSER mode)");
            CHECK(authorize_url.contains("prompt=select_tone"), "select_tone prompt in URL");
            CHECK(authorize_url.contains("code_challenge_method=S256"), "PKCE S256 in URL");
            http_get(nam, QUrl(authorize_url));
        });
    });
    CHECK(finished, "auth flow finished");
    CHECK(auth_ok, qPrintable("auth ok: " + auth_err));
    CHECK(picked_tone == 1, "tone_id from select flow");
    CHECK(client.is_signed_in(), "signed in");
    CHECK(client.get_user().username == "tester", "current user fetched");
    CHECK(QSettings().value("tone3000/accessToken").toString() == "tok-1", "tokens persisted");

    std::printf("lists\n");
    {
        QList<T3kTone> tones;
        wait_for([&](auto done) {
            client.list_tones(Tone3000Client::ListKind::TRENDING, 1,
                              [&, done](const Tone3000Client::Result& r) {
                                  CHECK(r.ok, "trending ok");
                                  for (const QJsonValue v : r.object().value("data").toArray()) {
                                      tones << Tone3000Client::parse_tone(v.toObject());
                                  }
                                  done();
                              });
        });
        CHECK(tones.size() == 3, "3 trending tones");
        CHECK(tones[0].gear_label() == "Amp + Cab" && tones[0].format_label() == "NAM", "labels");
        CHECK(tones[2].format == "ir", "IR tone parsed");
        // Cached: a second call must not hit the server.
        const int before = http_get(nam, QUrl(QString("http://127.0.0.1:%1/control").arg(PORT)))
                               .object()
                               .value("requests")
                               .toInt();
        wait_for([&](auto done) {
            client.list_tones(Tone3000Client::ListKind::TRENDING, 1,
                              [done](const Tone3000Client::Result& r) {
                                  CHECK(r.ok, "trending (cached) ok");
                                  done();
                              });
        });
        const int after = http_get(nam, QUrl(QString("http://127.0.0.1:%1/control").arg(PORT)))
                              .object()
                              .value("requests")
                              .toInt();
        CHECK(after == before + 1,
              "listing cache: no API request for the repeat"); // +1 = the /control GET itself
    }

    std::printf("favorites\n");
    wait_for([&](auto done) {
        client.set_favorite(2, true, [done](const Tone3000Client::Result& r) {
            CHECK(r.ok, "PUT favorite");
            done();
        });
    });
    {
        QList<T3kTone> favs;
        wait_for([&](auto done) {
            client.list_tones(Tone3000Client::ListKind::FAVORITED, 1,
                              [&, done](const Tone3000Client::Result& r) {
                                  for (const QJsonValue v : r.object().value("data").toArray()) {
                                      favs << Tone3000Client::parse_tone(v.toObject());
                                  }
                                  done();
                              });
        });
        CHECK(favs.size() == 1 && favs[0].id == 2 && favs[0].is_favorite,
              "favorited list reflects PUT");
    }
    wait_for([&](auto done) {
        client.set_favorite(2, false, [done](const Tone3000Client::Result& r) {
            CHECK(r.ok, "DELETE favorite");
            done();
        });
    });

    std::printf("token refresh (401 → refresh → retry)\n");
    http_get(nam, QUrl(QString("http://127.0.0.1:%1/control?expire=1").arg(PORT)));
    {
        T3kTone t;
        wait_for([&](auto done) {
            client.fetch_tone(1, [&, done](const Tone3000Client::Result& r) {
                CHECK(r.ok, "fetchTone after forced 401");
                t = Tone3000Client::parse_tone(r.object());
                done();
            });
        });
        CHECK(t.id == 1 && t.a2_count == 2 && t.a1_count == 1, "tone parsed");
        CHECK(QSettings().value("tone3000/accessToken").toString() == "tok-2",
              "rotated token persisted");
    }

    std::printf("rate limit (429 + Retry-After → one retry)\n");
    http_get(nam, QUrl(QString("http://127.0.0.1:%1/control?ratelimit=1").arg(PORT)));
    {
        bool status_seen = false;
        QObject::connect(&client, &Tone3000Client::status, &client, [&](const QString&) {
            status_seen = true;
        });
        wait_for([&](auto done) {
            client.fetch_tone(2, [done](const Tone3000Client::Result& r) {
                CHECK(r.ok, "fetchTone after 429");
                done();
            });
        });
        CHECK(status_seen, "status notice emitted for the back-off");
    }

    std::printf("models + download\n");
    {
        T3kTone tone;
        wait_for([&](auto done) {
            client.fetch_tone(1, [&, done](const Tone3000Client::Result& r) {
                tone = Tone3000Client::parse_tone(r.object());
                done();
            });
        });
        QList<T3kModel> models;
        wait_for([&](auto done) {
            client.list_models(tone, [&, done](const Tone3000Client::Result& r, QList<T3kModel> m) {
                CHECK(r.ok, "listModels ok");
                models = m;
                done();
            });
        });
        CHECK(models.size() == 3, "A2 + A1 sets merged (3 models)");

        bool ok = false;
        QString path, err;
        bool progress = false;
        QObject::connect(&client, &Tone3000Client::download_progress, &client,
                         [&](int, qint64, qint64) {
                             progress = true;
                         });
        wait_for([&](auto done) {
            client.download_model(models[0], tone, [&, done](bool o, QString p, QString e) {
                ok = o;
                path = p;
                err = e;
                done();
            });
        });
        CHECK(ok, qPrintable("download ok: " + err));
        CHECK(QFile::exists(path), "model file written");
        CHECK(path.startsWith(tmp.path() + "/dl/ripper/Dual Rec Rhythm/"),
              "creator/tone folder layout");
        CHECK(QFile::exists(Tone3000Client::sidecar_path(path)), "sidecar written");
        {
            QFile f(Tone3000Client::sidecar_path(path));
            const QJsonObject side = f.open(QIODevice::ReadOnly)
                                         ? QJsonDocument::fromJson(f.readAll()).object()
                                         : QJsonObject();
            CHECK(side.value("tone_id").toInt() == 1 &&
                      side.value("model_id").toInt() == models[0].id,
                  "sidecar ids");
            CHECK(side.value("creator").toString() == "ripper" &&
                      side.value("license").toString() == "cc-by",
                  "sidecar creator/license");
        }
        CHECK(Tone3000Client::local_path_for(models[0], tone) == path, "localPathFor matches");
        // The storage hop must not carry Authorization (the mock rejects it).
        const QJsonArray reqs =
            http_get(nam, QUrl(QString("http://127.0.0.1:%1/requests").arg(PORT))).array();
        bool storage_clean = false, api_authed = false;
        for (const QJsonValue v : reqs) {
            const QJsonObject o = v.toObject();
            if (o.value("p").toString().startsWith("/storage/")) {
                storage_clean = o.value("auth").toString().isEmpty();
            }
            if (o.value("p").toString() == "/api/v1/files/model0.nam") {
                api_authed = o.value("auth").toString().startsWith("Bearer ");
            }
        }
        CHECK(api_authed, "model_url fetched with Bearer");
        CHECK(storage_clean, "redirect target fetched without Bearer");
        (void)progress;
    }

    std::printf("images\n");
    {
        QPixmap px;
        wait_for([&](auto done) {
            client.fetch_image(QString("http://127.0.0.1:%1/img/tone1.png").arg(PORT),
                               QSize(48, 48), [&, done](QPixmap p) {
                                   px = p;
                                   done();
                               });
        });
        CHECK(!px.isNull() && px.width() <= 48, "image fetched and scaled");
    }

    std::printf("sign out\n");
    client.sign_out();
    CHECK(!client.is_signed_in() && QSettings().value("tone3000/accessToken").toString().isEmpty(),
          "tokens cleared");
    {
        bool auth_required = false;
        wait_for([&](auto done) {
            client.fetch_tone(1, [&, done](const Tone3000Client::Result& r) {
                auth_required = r.auth_required;
                done();
            });
        });
        CHECK(auth_required, "requests report authRequired when signed out");
    }

    mock.terminate();
    mock.waitForFinished(3000);
    std::printf("%s (%d failures)\n", gFailures ? "FAILED" : "PASSED", gFailures);
    return gFailures ? 1 : 0;
}

} // namespace npl

int main(int argc, char** argv) {
    return npl::run(argc, argv);
}

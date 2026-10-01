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

static const int kPort = 8765;

// Runs the event loop until `done` is set or the timeout hits.
template <typename F>
static bool waitFor(F&& start, int timeoutMs = 8000) {
    QEventLoop loop;
    bool done = false;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(timeoutMs);
    start([&] {
        done = true;
        loop.quit();
    });
    if (!done) {
        loop.exec();
    }
    return done;
}

static QJsonDocument httpGet(QNetworkAccessManager& nam, const QUrl& url) {
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

static QString gAuthorizeUrl;
static void messageHandler(QtMsgType, const QMessageLogContext&, const QString& msg) {
    static const QRegularExpression re("TONE3000 authorize URL: \"?(\\S+?)\"?$");
    const auto m = re.match(msg);
    if (m.hasMatch()) {
        gAuthorizeUrl = m.captured(1);
    }
    std::fprintf(stderr, "%s\n", qPrintable(msg));
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("NAM_TONE3000_API_BASE", QString("http://127.0.0.1:%1/api/v1").arg(kPort).toUtf8());
    qputenv("NAM_TONE3000_NO_BROWSER", "1");
    QGuiApplication app(argc, argv); // QPixmap needs a GUI application
    app.setOrganizationName("pichulero.cl");
    app.setApplicationName("NAM Player Test");

    // Isolated settings + download dir.
    QTemporaryDir tmp;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, tmp.path() + "/cfg");
    QSettings().clear();
    QSettings().setValue("library/tone3000Dir", tmp.path() + "/dl");
    Tone3000Client::setClientId("t3k_pub_test");

    // Mock server.
    QProcess mock;
    mock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    mock.start("python3", {QString::fromUtf8(MOCK_SERVER_PY), QString::number(kPort)});
    if (!mock.waitForStarted(5000)) {
        std::printf("cannot start python3 mock\n");
        return 2;
    }
    mock.waitForReadyRead(5000); // "mock tone3000 on …"

    qInstallMessageHandler(messageHandler);
    QNetworkAccessManager nam;
    Tone3000Client client;

    std::printf("sign-in (select flow)\n");
    CHECK(!client.isSignedIn(), "starts signed out");
    bool authOk = false;
    int pickedTone = 0;
    QString authErr;
    const bool finished = waitFor([&](auto done) {
        QObject::connect(&client, &Tone3000Client::authFinished, &client,
                         [&, done](bool ok, const QString& err, int toneId) {
                             authOk = ok;
                             authErr = err;
                             pickedTone = toneId;
                             done();
                         });
        client.startAuth(Tone3000Client::Flow::SelectTone);
        // "Browser": GET the authorize URL; the mock redirects to our loopback.
        QTimer::singleShot(100, &client, [&] {
            CHECK(!gAuthorizeUrl.isEmpty(), "authorize URL logged (NO_BROWSER mode)");
            CHECK(gAuthorizeUrl.contains("prompt=select_tone"), "select_tone prompt in URL");
            CHECK(gAuthorizeUrl.contains("code_challenge_method=S256"), "PKCE S256 in URL");
            httpGet(nam, QUrl(gAuthorizeUrl));
        });
    });
    CHECK(finished, "auth flow finished");
    CHECK(authOk, qPrintable("auth ok: " + authErr));
    CHECK(pickedTone == 1, "tone_id from select flow");
    CHECK(client.isSignedIn(), "signed in");
    CHECK(client.user().username == "tester", "current user fetched");
    CHECK(QSettings().value("tone3000/accessToken").toString() == "tok-1", "tokens persisted");

    std::printf("lists\n");
    {
        QList<T3kTone> tones;
        waitFor([&](auto done) {
            client.listTones(Tone3000Client::ListKind::Trending, 1,
                             [&, done](const Tone3000Client::Result& r) {
                                 CHECK(r.ok, "trending ok");
                                 for (const QJsonValue v : r.object().value("data").toArray()) {
                                     tones << Tone3000Client::parseTone(v.toObject());
                                 }
                                 done();
                             });
        });
        CHECK(tones.size() == 3, "3 trending tones");
        CHECK(tones[0].gearLabel() == "Amp + Cab" && tones[0].formatLabel() == "NAM", "labels");
        CHECK(tones[2].format == "ir", "IR tone parsed");
        // Cached: a second call must not hit the server.
        const int before = httpGet(nam, QUrl(QString("http://127.0.0.1:%1/control").arg(kPort)))
                               .object()
                               .value("requests")
                               .toInt();
        waitFor([&](auto done) {
            client.listTones(Tone3000Client::ListKind::Trending, 1,
                             [done](const Tone3000Client::Result& r) {
                                 CHECK(r.ok, "trending (cached) ok");
                                 done();
                             });
        });
        const int after = httpGet(nam, QUrl(QString("http://127.0.0.1:%1/control").arg(kPort)))
                              .object()
                              .value("requests")
                              .toInt();
        CHECK(after == before + 1,
              "listing cache: no API request for the repeat"); // +1 = the /control GET itself
    }

    std::printf("favorites\n");
    waitFor([&](auto done) {
        client.setFavorite(2, true, [done](const Tone3000Client::Result& r) {
            CHECK(r.ok, "PUT favorite");
            done();
        });
    });
    {
        QList<T3kTone> favs;
        waitFor([&](auto done) {
            client.listTones(Tone3000Client::ListKind::Favorited, 1,
                             [&, done](const Tone3000Client::Result& r) {
                                 for (const QJsonValue v : r.object().value("data").toArray()) {
                                     favs << Tone3000Client::parseTone(v.toObject());
                                 }
                                 done();
                             });
        });
        CHECK(favs.size() == 1 && favs[0].id == 2 && favs[0].isFavorite,
              "favorited list reflects PUT");
    }
    waitFor([&](auto done) {
        client.setFavorite(2, false, [done](const Tone3000Client::Result& r) {
            CHECK(r.ok, "DELETE favorite");
            done();
        });
    });

    std::printf("token refresh (401 → refresh → retry)\n");
    httpGet(nam, QUrl(QString("http://127.0.0.1:%1/control?expire=1").arg(kPort)));
    {
        T3kTone t;
        waitFor([&](auto done) {
            client.fetchTone(1, [&, done](const Tone3000Client::Result& r) {
                CHECK(r.ok, "fetchTone after forced 401");
                t = Tone3000Client::parseTone(r.object());
                done();
            });
        });
        CHECK(t.id == 1 && t.a2Count == 2 && t.a1Count == 1, "tone parsed");
        CHECK(QSettings().value("tone3000/accessToken").toString() == "tok-2",
              "rotated token persisted");
    }

    std::printf("rate limit (429 + Retry-After → one retry)\n");
    httpGet(nam, QUrl(QString("http://127.0.0.1:%1/control?ratelimit=1").arg(kPort)));
    {
        bool statusSeen = false;
        QObject::connect(&client, &Tone3000Client::status, &client, [&](const QString&) {
            statusSeen = true;
        });
        waitFor([&](auto done) {
            client.fetchTone(2, [done](const Tone3000Client::Result& r) {
                CHECK(r.ok, "fetchTone after 429");
                done();
            });
        });
        CHECK(statusSeen, "status notice emitted for the back-off");
    }

    std::printf("models + download\n");
    {
        T3kTone tone;
        waitFor([&](auto done) {
            client.fetchTone(1, [&, done](const Tone3000Client::Result& r) {
                tone = Tone3000Client::parseTone(r.object());
                done();
            });
        });
        QList<T3kModel> models;
        waitFor([&](auto done) {
            client.listModels(tone, [&, done](const Tone3000Client::Result& r, QList<T3kModel> m) {
                CHECK(r.ok, "listModels ok");
                models = m;
                done();
            });
        });
        CHECK(models.size() == 3, "A2 + A1 sets merged (3 models)");

        bool ok = false;
        QString path, err;
        bool progress = false;
        QObject::connect(&client, &Tone3000Client::downloadProgress, &client,
                         [&](int, qint64, qint64) {
                             progress = true;
                         });
        waitFor([&](auto done) {
            client.downloadModel(models[0], tone, [&, done](bool o, QString p, QString e) {
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
        CHECK(QFile::exists(Tone3000Client::sidecarPath(path)), "sidecar written");
        {
            QFile f(Tone3000Client::sidecarPath(path));
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
        CHECK(Tone3000Client::localPathFor(models[0], tone) == path, "localPathFor matches");
        // The storage hop must not carry Authorization (the mock rejects it).
        const QJsonArray reqs =
            httpGet(nam, QUrl(QString("http://127.0.0.1:%1/requests").arg(kPort))).array();
        bool storageClean = false, apiAuthed = false;
        for (const QJsonValue v : reqs) {
            const QJsonObject o = v.toObject();
            if (o.value("p").toString().startsWith("/storage/")) {
                storageClean = o.value("auth").toString().isEmpty();
            }
            if (o.value("p").toString() == "/api/v1/files/model0.nam") {
                apiAuthed = o.value("auth").toString().startsWith("Bearer ");
            }
        }
        CHECK(apiAuthed, "model_url fetched with Bearer");
        CHECK(storageClean, "redirect target fetched without Bearer");
        (void)progress;
    }

    std::printf("images\n");
    {
        QPixmap px;
        waitFor([&](auto done) {
            client.fetchImage(QString("http://127.0.0.1:%1/img/tone1.png").arg(kPort),
                              QSize(48, 48), [&, done](QPixmap p) {
                                  px = p;
                                  done();
                              });
        });
        CHECK(!px.isNull() && px.width() <= 48, "image fetched and scaled");
    }

    std::printf("sign out\n");
    client.signOut();
    CHECK(!client.isSignedIn() && QSettings().value("tone3000/accessToken").toString().isEmpty(),
          "tokens cleared");
    {
        bool authRequired = false;
        waitFor([&](auto done) {
            client.fetchTone(1, [&, done](const Tone3000Client::Result& r) {
                authRequired = r.authRequired;
                done();
            });
        });
        CHECK(authRequired, "requests report authRequired when signed out");
    }

    mock.terminate();
    mock.waitForFinished(3000);
    std::printf("%s (%d failures)\n", gFailures ? "FAILED" : "PASSED", gFailures);
    return gFailures ? 1 : 0;
}

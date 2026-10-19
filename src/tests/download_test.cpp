#include "downloadmanager.h"
#include "downloadpaths.h"
#include "ncmclient.h"
#include "sdk/download.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>

using namespace ncm::sdk;

namespace {

// 一个本地假接口：同时扮演 music.163.com（POST 下载地址、GET 歌曲详情）与 CDN。
// 慢速音频由测试自己控制写包时间，用来观察 downloadProgress 与取消。
class FakeApi : public QTcpServer {
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray query;
        QByteArray body;
    };

    explicit FakeApi(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *sock = nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer] {
                    buffer->append(sock->readAll());
                    const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                    if (headerEnd < 0)
                        return;
                    const QByteArray head = buffer->left(headerEnd);
                    const QByteArray rest = buffer->mid(headerEnd + 4);
                    int contentLength = 0;
                    for (const QByteArray &line : head.split('\n')) {
                        const QByteArray text = line.trimmed();
                        if (text.toLower().startsWith("content-length:"))
                            contentLength = text.mid(15).trimmed().toInt();
                    }
                    if (rest.size() < contentLength)
                        return;
                    buffer->clear();

                    Request request;
                    const QByteArray firstLine = head.left(head.indexOf('\n')).trimmed();
                    const QList<QByteArray> first = firstLine.split(' ');
                    request.method = first.value(0);
                    const QByteArray target = first.value(1);
                    const qsizetype q = target.indexOf('?');
                    request.path = q < 0 ? target : target.left(q);
                    request.query = q < 0 ? QByteArray() : target.mid(q + 1);
                    request.body = rest.left(contentLength);
                    requests.append(request);

                    if (handler)
                        handler(request, sock);
                    else
                        respond(sock, 404, "not found", "text/plain");
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
                connect(sock, &QTcpSocket::disconnected, this, [buffer] { delete buffer; });
            }
        });
    }

    static void respond(QTcpSocket *sock, int status, const QByteArray &body,
                        const QByteArray &type = "application/json")
    {
        if (!sock)
            return;
        sock->write("HTTP/1.1 " + QByteArray::number(status)
                    + " x\r\nContent-Type: " + type
                    + "\r\nContent-Length: " + QByteArray::number(body.size())
                    + "\r\nConnection: close\r\n\r\n" + body);
        sock->flush();
        sock->disconnectFromHost();
    }

    static void respondJson(QTcpSocket *sock, int status, const QJsonObject &object)
    {
        respond(sock, status,
                QJsonDocument(object).toJson(QJsonDocument::Compact));
    }

    std::function<void(const Request &, QTcpSocket *)> handler;
    QList<Request> requests;
};

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!condition() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return condition();
}

} // namespace

class DownloadTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_home;
    FakeApi *m_api = nullptr;
    NcmClient *m_client = nullptr;

    static QString audioUrl(const FakeApi &api, double songId)
    {
        return QStringLiteral("http://127.0.0.1:%1/audio/%2.flac")
            .arg(api.serverPort())
            .arg(songId, 0, 'f', 0);
    }

    static QJsonObject downloadUrl(double songId, const QString &url)
    {
        return QJsonObject{
            {"code", 200},
            {"data", QJsonArray{QJsonObject{
                {"id", songId},
                {"url", url},
                {"size", 4},
                {"type", "flac"},
                {"level", "lossless"},
            }}},
        };
    }

private slots:
    void initTestCase()
    {
        qputenv("XDG_DATA_HOME", m_home.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_home.filePath("cache").toUtf8());
        qputenv("YUNTING_KEYCHAIN_SERVICE", "yunting-test");
    }

    void init()
    {
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        QDir(yuntingDownloadsDir()).removeRecursively();

        m_api = new FakeApi(this);
        QVERIFY(m_api->listen(QHostAddress::LocalHost));
        m_client = new NcmClient(nullptr, false);
        m_client->setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(m_api->serverPort()));
    }

    void cleanup()
    {
        delete m_client;
        m_client = nullptr;
        delete m_api;
        m_api = nullptr;
    }

    void storesAndReloadsDuration()
    {
        const double songId = 910001;
        const QString url = audioUrl(*m_api, songId);
        m_api->handler = [songId, url](const FakeApi::Request &request, QTcpSocket *sock) {
            if (request.path == "/api/song/enhance/download/url/v1")
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            else if (request.path.startsWith("/audio/"))
                FakeApi::respond(sock, 200, "abcd", "audio/flac");
            else
                FakeApi::respond(sock, 404, "not found", "text/plain");
        };

        DownloadManager first(m_client);
        QSignalSpy finished(&first, &DownloadManager::finished);
        first.downloadTrack(QVariantMap{
                                {"id", songId},
                                {"name", "下载曲"},
                                {"artist", "下载歌手"},
                                {"dur", 269.0},
                            },
                            QStringLiteral("lossless"));
        QVERIFY(waitFor([&] { return finished.count() > 0; }));
        QCOMPARE(first.downloads().size(), 1);
        const QVariantMap record = first.downloads().at(0).toMap();
        QCOMPARE(record.value("id").toDouble(), songId);
        QCOMPARE(record.value("dur").toDouble(), 269.0);
        QVERIFY(first.active().isEmpty());

        DownloadManager reloaded(m_client);
        QCOMPARE(reloaded.downloads().size(), 1);
        QCOMPARE(reloaded.downloads().at(0).toMap().value("dur").toDouble(), 269.0);
    }

    void legacyRecordWithoutDurationLoadsZero()
    {
        const double songId = 910002;
        QVERIFY(QDir().mkpath(yuntingDownloadsDir()));
        QFile file(yuntingDownloadsDir() + QStringLiteral("/downloads.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonArray{QJsonObject{
                                     {"id", songId},
                                     {"name", "旧记录"},
                                 }})
                       .toJson(QJsonDocument::Compact));
        file.close();

        DownloadManager manager(m_client);
        QCOMPARE(manager.downloads().size(), 1);
        const QVariantMap record = manager.downloads().at(0).toMap();
        QVERIFY(record.contains(QStringLiteral("dur")));
        QCOMPARE(record.value("dur").toDouble(), 0.0);
    }

    void idOnlyDownloadUsesSongDetailDuration()
    {
        const double songId = 910003;
        const QString url = audioUrl(*m_api, songId);
        int detailRequests = 0;
        m_api->handler = [&detailRequests, songId, url](const FakeApi::Request &request,
                                                        QTcpSocket *sock) {
            if (request.path == "/api/v3/song/detail") {
                ++detailRequests;
                FakeApi::respondJson(sock, 200, QJsonObject{
                    {"code", 200},
                    {"songs", QJsonArray{QJsonObject{
                        {"id", songId},
                        {"name", "详情歌"},
                        {"dt", 215000.0},
                        {"ar", QJsonArray{QJsonObject{{"id", 7}, {"name", "详情歌手"}}}},
                        {"al", QJsonObject{{"id", 8}, {"name", "详情专辑"}}},
                    }}},
                    {"privileges", QJsonArray{}},
                });
            } else if (request.path == "/api/song/enhance/download/url/v1") {
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            } else if (request.path.startsWith("/audio/")) {
                FakeApi::respond(sock, 200, "abcd", "audio/flac");
            } else {
                FakeApi::respond(sock, 404, "not found", "text/plain");
            }
        };

        DownloadManager manager(m_client);
        QSignalSpy finished(&manager, &DownloadManager::finished);
        manager.download(songId);
        QVERIFY(waitFor([&] { return finished.count() > 0; }));

        QCOMPARE(detailRequests, 1);
        QCOMPARE(manager.downloads().size(), 1);
        const QVariantMap record = manager.downloads().at(0).toMap();
        QCOMPARE(record.value("dur").toDouble(), 215.0);
        QCOMPARE(record.value("name").toString(), QStringLiteral("详情歌"));
        QCOMPARE(record.value("artist").toString(), QStringLiteral("详情歌手"));
    }

    void idOnlyDownloadSurvivesDetailFailure()
    {
        const double songId = 910007;
        const QString url = audioUrl(*m_api, songId);
        m_api->handler = [songId, url](const FakeApi::Request &request, QTcpSocket *sock) {
            if (request.path == "/api/v3/song/detail")
                FakeApi::respond(sock, 500, "no", "text/plain");
            else if (request.path == "/api/song/enhance/download/url/v1")
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            else if (request.path.startsWith("/audio/"))
                FakeApi::respond(sock, 200, "abcd", "audio/flac");
            else
                FakeApi::respond(sock, 404, "not found", "text/plain");
        };

        DownloadManager manager(m_client);
        QSignalSpy finished(&manager, &DownloadManager::finished);
        manager.download(songId);
        QVERIFY(waitFor([&] { return finished.count() > 0; }));
        QCOMPARE(manager.downloads().size(), 1);
        QCOMPARE(manager.downloads().at(0).toMap().value("dur").toDouble(), 0.0);
    }

    void activeListTracksProgressAndClearsOnFinish()
    {
        const double songId = 910004;
        const QString url = audioUrl(*m_api, songId);
        m_api->handler = [this, songId, url](const FakeApi::Request &request, QTcpSocket *sock) {
            if (request.path == "/api/song/enhance/download/url/v1") {
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            } else if (request.path.startsWith("/audio/")) {
                // 分三块慢慢写：中间态 progress 必须落在 (0,1) 而不是 0 或 1
                sock->write("HTTP/1.1 200 OK\r\nContent-Type: audio/flac\r\n"
                            "Content-Length: 24576\r\nConnection: close\r\n\r\n");
                sock->write(QByteArray(8192, 'a'));
                sock->flush();
                QPointer<QTcpSocket> guard(sock);
                QTimer::singleShot(100, this, [guard] {
                    if (guard) {
                        guard->write(QByteArray(8192, 'b'));
                        guard->flush();
                    }
                });
                QTimer::singleShot(200, this, [guard] {
                    if (!guard)
                        return;
                    guard->write(QByteArray(8192, 'c'));
                    guard->flush();
                    guard->disconnectFromHost();
                });
            } else {
                FakeApi::respond(sock, 404, "not found", "text/plain");
            }
        };

        DownloadManager manager(m_client);
        QSignalSpy activeChanged(&manager, &DownloadManager::activeChanged);
        QSignalSpy finished(&manager, &DownloadManager::finished);
        manager.downloadTrack(QVariantMap{
                                  {"id", songId},
                                  {"name", "进度曲"},
                                  {"artist", "进度歌手"},
                                  {"dur", 3.0},
                              },
                              QStringLiteral("lossless"));

        QVERIFY(waitFor([&] { return !manager.active().isEmpty(); }));
        const QVariantMap active = manager.active().at(0).toMap();
        QCOMPARE(active.value("id").toDouble(), songId);
        QCOMPARE(active.value("name").toString(), QStringLiteral("进度曲"));
        QCOMPARE(active.value("artist").toString(), QStringLiteral("进度歌手"));
        QCOMPARE(active.value("level").toString(), QStringLiteral("lossless"));
        QVERIFY(active.value("progress").toDouble() >= 0.0);
        QVERIFY(active.value("progress").toDouble() <= 1.0);

        QVERIFY(waitFor([&] {
            const QVariantList items = manager.active();
            if (items.isEmpty())
                return false;
            const double progress = items.at(0).toMap().value("progress").toDouble();
            return progress > 0.0 && progress < 1.0;
        }));
        QVERIFY(activeChanged.count() >= 2);

        QVERIFY(waitFor([&] { return finished.count() > 0; }));
        QVERIFY(manager.active().isEmpty());
        QCOMPARE(manager.downloads().size(), 1);
    }

    void activeListClearsOnFailure()
    {
        const double songId = 910005;
        const QString url = audioUrl(*m_api, songId);
        m_api->handler = [this, songId, url](const FakeApi::Request &request, QTcpSocket *sock) {
            if (request.path == "/api/song/enhance/download/url/v1") {
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            } else if (request.path.startsWith("/audio/")) {
                QPointer<QTcpSocket> guard(sock);
                QTimer::singleShot(150, this, [guard] {
                    FakeApi::respond(guard, 500, "no", "text/plain");
                });
            } else {
                FakeApi::respond(sock, 404, "not found", "text/plain");
            }
        };

        DownloadManager manager(m_client);
        QSignalSpy failed(&manager, &DownloadManager::failed);
        manager.downloadTrack(QVariantMap{
                                  {"id", songId},
                                  {"name", "失败曲"},
                                  {"artist", "失败歌手"},
                                  {"dur", 3.0},
                              },
                              QStringLiteral("lossless"));

        QVERIFY(waitFor([&] { return !manager.active().isEmpty(); }));
        QVERIFY(waitFor([&] { return failed.count() > 0; }));
        QVERIFY(manager.active().isEmpty());
        QVERIFY(manager.downloads().isEmpty());
    }

    void activeListClearsOnCancel()
    {
        const double songId = 910006;
        const QString url = audioUrl(*m_api, songId);
        m_api->handler = [songId, url](const FakeApi::Request &request, QTcpSocket *sock) {
            if (request.path == "/api/song/enhance/download/url/v1") {
                FakeApi::respondJson(sock, 200, downloadUrl(songId, url));
            } else if (request.path.startsWith("/audio/")) {
                sock->write("HTTP/1.1 200 OK\r\nContent-Type: audio/flac\r\n"
                            "Content-Length: 4\r\nConnection: close\r\n\r\nab");
                sock->flush();
            } else {
                FakeApi::respond(sock, 404, "not found", "text/plain");
            }
        };

        DownloadManager manager(m_client);
        QSignalSpy failed(&manager, &DownloadManager::failed);
        manager.downloadTrack(QVariantMap{
                                  {"id", songId},
                                  {"name", "取消曲"},
                                  {"artist", "取消歌手"},
                                  {"dur", 3.0},
                              },
                              QStringLiteral("lossless"));

        QVERIFY(waitFor([&] { return !manager.active().isEmpty(); }));
        manager.cancel(songId);
        QVERIFY(manager.active().isEmpty());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("下载已取消"));
    }

    void parsesDownload()
    {
        const QJsonObject root{
            {"code", 200},
            {"data", QJsonArray{QJsonObject{
                {"id", 42}, {"url", "https://cdn.example/a.flac"}, {"size", 1234},
                {"br", 999000}, {"md5", "abc"}, {"type", "flac"}, {"level", "lossless"},
                {"expi", 1200},
            }}},
        };
        const auto parsed = parseSongDownload(root);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->id, qint64(42));
        QCOMPARE(parsed.value->url, QStringLiteral("https://cdn.example/a.flac"));
        QCOMPARE(parsed.value->size, qint64(1234));
        QCOMPARE(parsed.value->type, QStringLiteral("flac"));
        QCOMPARE(parsed.value->level, QStringLiteral("lossless"));
        QCOMPARE(parsed.value->expires, qint64(1200));
    }

    void parsesObjectData()
    {
        const auto parsed = parseSongDownload(QJsonObject{
            {"data", QJsonObject{{"id", 7}, {"url", "https://cdn.example/a.mp3"},
                                 {"size", 1}, {"type", "mp3"}}},
        });
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->id, qint64(7));
        QCOMPARE(parsed.value->type, QStringLiteral("mp3"));
    }

    void emptyDataIsParseError()
    {
        const auto parsed = parseSongDownload(QJsonObject{{"data", QJsonArray{}}});
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
    }

    void wrongTypeIsParseError()
    {
        const auto parsed = parseSongDownload(QJsonObject{
            {"data", QJsonArray{QJsonObject{{"id", 42}, {"url", 7}}}},
        });
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.error.kind, ErrorKind::Parse);
        QVERIFY(parsed.error.path.contains(QStringLiteral("data[0]")));
    }
};

QTEST_GUILESS_MAIN(DownloadTest)
#include "download_test.moc"

// Store 的行为测试：seam 是本地 QTcpServer（扮演 music.163.com），不 mock Store/NcmClient 内部。
// 覆盖：业务码错误不写缓存、歌单写后缓存失效、连点红心不丢最后一次意图、迟到的详情不把
// 已取消的红心插回列表。
#include "../store.h"
#include "../ncmclient.h"
#include "../session.h"

#include <QDateTime>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <utility>

class FakeApi : public QTcpServer {
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QByteArray query;
        QByteArray body;
        QByteArray headers;
    };
    using Responder = std::function<void(int status, const QByteArray &body)>;

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

                    Request req;
                    const QByteArray firstLine = head.left(head.indexOf('\n')).trimmed();
                    const QList<QByteArray> first = firstLine.split(' ');
                    req.method = first.value(0);
                    const QByteArray target = first.value(1);
                    const qsizetype q = target.indexOf('?');
                    req.path = q < 0 ? target : target.left(q);
                    req.query = q < 0 ? QByteArray() : target.mid(q + 1);
                    req.body = rest.left(contentLength);
                    req.headers = head;
                    requests.append(req);

                    QPointer<QTcpSocket> guard(sock);
                    // status <= 0 表示不回包直接断开：客户端看到的是连接层失败，而不是 HTTP 错误
                    const Responder respond = [guard](int status, const QByteArray &body) {
                        if (!guard)
                            return;
                        if (status > 0) {
                            guard->write("HTTP/1.1 " + QByteArray::number(status) + " x\r\n"
                                         "Content-Type: application/json\r\nContent-Length: "
                                         + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                                         + body);
                            guard->flush();
                        }
                        guard->disconnectFromHost();
                    };
                    if (handler)
                        handler(req, respond);
                    else
                        respond(200, "{\"code\":200}");
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
                connect(sock, &QTcpSocket::disconnected, this, [buffer] { delete buffer; });
            }
        });
    }

    std::function<void(const Request &, Responder)> handler;
    QList<Request> requests;
};

class StoreTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeApi *m_api = nullptr;
    NcmClient *m_client = nullptr;
    Store *m_store = nullptr;

    static QByteArray json(const QJsonObject &object)
    {
        return QJsonDocument(object).toJson(QJsonDocument::Compact);
    }

    static bool waitUntil(const std::function<bool()> &done, int timeoutMs = 5000)
    {
        QDeadlineTimer deadline(timeoutMs);
        while (!done() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    }

    // 一首歌的歌词条目 { lines, pureMusic }；还没取到时为空
    QVariantMap lyricsOf(double songId) const
    {
        return m_store->data().value("lyrics").toMap().value(QString::number(qlonglong(songId))).toMap();
    }

    static bool likedIdsContain(const Store *store, double songId)
    {
        for (const QVariant &v : store->personal().value("likedIds").toList())
            if (v.toDouble() == songId)
                return true;
        return false;
    }

    static QJsonObject radioObject()
    {
        return QJsonObject{
            {"id", 7}, {"name", "电台"}, {"picUrl", "http://cover/radio.jpg"},
            {"desc", "电台简介"}, {"playCount", 12}, {"programCount", 3}, {"subCount", 4},
            {"dj", QJsonObject{{"userId", 99}, {"nickname", "主播"},
                               {"avatarUrl", "http://cover/dj.jpg"}}}};
    }

    static QJsonObject programObject(int mainSongId = 55)
    {
        QJsonObject mainSong{{"name", "节目歌"}, {"duration", 65000}, {"fee", 0},
                             {"artists", QJsonArray{QJsonObject{{"id", 9}, {"name", "歌手"}}}}};
        if (mainSongId > 0)
            mainSong.insert("id", mainSongId);
        return QJsonObject{
            {"id", 66}, {"name", "节目"}, {"description", "节目简介"},
            {"createTime", 1700000000000.0}, {"duration", 794000}, {"serialNum", 31},
            {"coverUrl", "http://cover/program.jpg"}, {"mainSong", mainSong},
            {"radio", radioObject()}};
    }

private slots:
    void init()
    {
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("XDG_DATA_HOME", m_dir.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_dir.filePath("cache").toUtf8());
        qputenv("YUNTING_KEYCHAIN_SERVICE", "yunting-test");

        m_api = new FakeApi(this);
        QVERIFY(m_api->listen(QHostAddress::LocalHost));
        m_client = new NcmClient(nullptr, false);
        m_client->setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(m_api->serverPort()));
        m_client->adoptCookies(QJsonDocument(QJsonArray{
            QJsonObject{{"MUSIC_U", "test"}, {"__csrf", "csrf"}}}));
        m_store = new Store(m_client);
    }

    void cleanup()
    {
        delete m_store;
        m_store = nullptr;
        delete m_client;
        m_client = nullptr;
        delete m_api;
        m_api = nullptr;
    }

    // 业务码 -460 的歌词回包不能当成「没有歌词」永久缓存
    void businessErrorIsNotCachedAsFresh()
    {
        int lyricRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric") {
                ++lyricRequests;
                if (lyricRequests == 1) {
                    respond(200, "{\"code\":-460}");
                    return;
                }
                respond(200, "{\"code\":200,\"lrc\":{\"lyric\":\"[00:01.00]hello\"},"
                             "\"tlyric\":{\"lyric\":\"\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyrics:9"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 1; }));
        QTest::qWait(100);
        QVERIFY(lyricsOf(9).isEmpty());

        m_store->request(QStringLiteral("lyrics:9"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 2; }));
        QVERIFY(waitUntil([&] { return lyricsOf(9).value("lines").toList().size() == 1; }));
    }

    // 几处同时要不同歌的歌词（正在播放页要正在放的，分享图要被分享的）：各存各的，谁也不覆盖谁。
    // 单槽时两边会互相换掉对方、再各自重要，无限来回，应用卡死、请求成百上千
    void lyricsOfDifferentSongsCoexist()
    {
        QHash<QString, int> lyricRequests;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric") {
                const QString id = QUrlQuery(QString::fromLatin1(req.query)).queryItemValue(QStringLiteral("id"));
                ++lyricRequests[id];
                respond(200, "{\"code\":200,\"lrc\":{\"lyric\":\"[00:01.00]" + id.toLatin1() + "\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyrics:21"));
        m_store->request(QStringLiteral("lyrics:22"));
        QVERIFY(waitUntil([&] { return !lyricsOf(21).isEmpty() && !lyricsOf(22).isEmpty(); }));
        QCOMPARE(lyricsOf(21).value("lines").toList().first().toMap().value("text").toString(), QStringLiteral("21"));
        QCOMPARE(lyricsOf(22).value("lines").toList().first().toMap().value("text").toString(), QStringLiteral("22"));

        // 再要一次：两首都还在，不会再发请求
        m_store->request(QStringLiteral("lyrics:21"));
        m_store->request(QStringLiteral("lyrics:22"));
        QTest::qWait(150);
        QCOMPARE(lyricRequests.value(QStringLiteral("21")), 1);
        QCOMPARE(lyricRequests.value(QStringLiteral("22")), 1);

        // 只留最近 8 首：再要 7 首之后，最早的 21 被挤掉，22 还在；最近要过的排在后面（22 刚又要过）
        for (int id = 31; id <= 37; ++id)
            m_store->request(QStringLiteral("lyrics:%1").arg(id));
        QVERIFY(waitUntil([&] { return !lyricsOf(37).isEmpty(); }));
        QCOMPARE(m_store->data().value("lyrics").toMap().size(), 8);
        QVERIFY(lyricsOf(21).isEmpty());
        QVERIFY(!lyricsOf(22).isEmpty());
    }

    // 两个歌手页同时开着：各自的详情按 id 存，互不覆盖；已经取到的 id 再要不会再发请求
    void artistSlotsCoexistByArtistId()
    {
        QHash<QString, int> requests;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            const QString path = QString::fromLatin1(req.path);
            if (path.startsWith(QStringLiteral("/api/v1/artist/"))) {
                ++requests[path];
                const int id = path.section(u'/', -1).toInt();
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"artist", QJsonObject{{"id", id}, {"name", QStringLiteral("A%1").arg(id)},
                                           {"alias", QJsonArray{}}, {"picUrl", ""},
                                           {"briefDesc", ""}, {"followed", false},
                                           {"musicSize", 0}, {"albumSize", 0}}},
                    {"hotSongs", QJsonArray{}}}));
                return;
            }
            if (path.startsWith(QStringLiteral("/api/artist/albums/"))) {
                ++requests[path];
                respond(200, json(QJsonObject{{"code", 200}, {"hotAlbums", QJsonArray{}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto entry = [this](const QString &id) {
            return m_store->data().value("artist").toMap().value(id).toMap();
        };
        m_store->request(QStringLiteral("artist:1"));
        m_store->request(QStringLiteral("artist:2"));
        QVERIFY(waitUntil([&] {
            return entry(QStringLiteral("1")).value("name").toString() == QLatin1String("A1")
                && entry(QStringLiteral("2")).value("name").toString() == QLatin1String("A2");
        }));

        const int before = requests.value(QStringLiteral("/api/v1/artist/1"));
        QCOMPARE(before, 1);
        m_store->request(QStringLiteral("artist:1"));
        QTest::qWait(150);
        QCOMPARE(requests.value(QStringLiteral("/api/v1/artist/1")), before);
        QCOMPARE(entry(QStringLiteral("2")).value("name").toString(), QStringLiteral("A2"));
    }

    // 两个专辑页同时开着：各自的详情按 id 存；已经取到的 id 再要不会再发请求
    void albumSlotsCoexistByAlbumId()
    {
        QHash<QString, int> requests;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            const QString path = QString::fromLatin1(req.path);
            if (path.startsWith(QStringLiteral("/api/v1/album/"))) {
                ++requests[path];
                const int id = path.section(u'/', -1).toInt();
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"album", QJsonObject{{"id", id}, {"name", QStringLiteral("B%1").arg(id)},
                                          {"artists", QJsonArray{}}, {"publishTime", 0}}},
                    {"songs", QJsonArray{}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto entry = [this](const QString &id) {
            return m_store->data().value("album").toMap().value(id).toMap();
        };
        m_store->request(QStringLiteral("album:1"));
        m_store->request(QStringLiteral("album:2"));
        QVERIFY(waitUntil([&] {
            return entry(QStringLiteral("1")).value("name").toString() == QLatin1String("B1")
                && entry(QStringLiteral("2")).value("name").toString() == QLatin1String("B2");
        }));

        const int before = requests.value(QStringLiteral("/api/v1/album/1"));
        QCOMPARE(before, 1);
        m_store->request(QStringLiteral("album:1"));
        QTest::qWait(150);
        QCOMPARE(requests.value(QStringLiteral("/api/v1/album/1")), before);
        QCOMPARE(entry(QStringLiteral("2")).value("name").toString(), QStringLiteral("B2"));
    }

    // 两个用户主页同时开着：资料按 id 存；已经取到的 id 再要不会再发请求
    void userSlotsCoexistByUserId()
    {
        QHash<QString, int> requests;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            const QString path = QString::fromLatin1(req.path);
            if (path.startsWith(QStringLiteral("/weapi/v1/user/detail/"))) {
                ++requests[path];
                const int id = path.section(u'/', -1).toInt();
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", id},
                                            {"nickname", QStringLiteral("U%1").arg(id)},
                                            {"avatarUrl", ""}}}}));
                return;
            }
            if (path == QLatin1String("/weapi/user/playlist")) {
                respond(200, json(QJsonObject{{"code", 200}, {"playlist", QJsonArray{}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto entry = [this](const QString &id) {
            return m_store->data().value("user").toMap().value(id).toMap();
        };
        m_store->request(QStringLiteral("user:1"));
        m_store->request(QStringLiteral("user:2"));
        QVERIFY(waitUntil([&] {
            return entry(QStringLiteral("1")).value("name").toString() == QLatin1String("U1")
                && entry(QStringLiteral("2")).value("name").toString() == QLatin1String("U2");
        }));

        const int before = requests.value(QStringLiteral("/weapi/v1/user/detail/1"));
        QCOMPARE(before, 1);
        m_store->request(QStringLiteral("user:1"));
        QTest::qWait(150);
        QCOMPARE(requests.value(QStringLiteral("/weapi/v1/user/detail/1")), before);
        QCOMPARE(entry(QStringLiteral("2")).value("name").toString(), QStringLiteral("U2"));
    }

    // 两个 MV 页同时开着：详情按 id 存；已取到的详情不再重取（签名地址仍按设计每次打开重取）
    void mvSlotsCoexistByMvId()
    {
        int detailRequests = 0;
        int urlRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/mv/detail") {
                const double id = ++detailRequests == 1 ? 7.0 : 8.0;
                respond(200, json(QJsonObject{
                    {"code", 200}, {"subed", false},
                    {"data", QJsonObject{
                        {"id", id}, {"name", QStringLiteral("MV%1").arg(qlonglong(id))},
                        {"cover", "http://cover"}, {"artistName", "A"}, {"artistId", 9},
                        {"duration", 120000}, {"playCount", 1}, {"subCount", 2},
                        {"shareCount", 3}, {"commentCount", 4},
                        {"publishTime", "2020-01-01"}, {"desc", "d"}}}}));
                return;
            }
            if (req.path == "/weapi/song/enhance/play/mv/url") {
                ++urlRequests;
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"data", QJsonObject{{"code", 200},
                                         {"url", QStringLiteral("http://media/%1").arg(urlRequests)},
                                         {"r", 1080}, {"size", 100}, {"expi", 1}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto entry = [this](const QString &id) {
            return m_store->data().value("mv").toMap().value(id).toMap();
        };
        m_store->request(QStringLiteral("mv:7"));
        QVERIFY(waitUntil([&] { return !entry(QStringLiteral("7")).value("url").toString().isEmpty(); }));
        m_store->request(QStringLiteral("mv:8"));
        QVERIFY(waitUntil([&] { return !entry(QStringLiteral("8")).value("url").toString().isEmpty(); }));
        QCOMPARE(detailRequests, 2);
        QCOMPARE(entry(QStringLiteral("7")).value("name").toString(), QStringLiteral("MV7"));
        QCOMPARE(entry(QStringLiteral("8")).value("name").toString(), QStringLiteral("MV8"));

        m_store->request(QStringLiteral("mv:7"));
        QVERIFY(waitUntil([&] { return urlRequests == 3; }));
        QCOMPARE(detailRequests, 2);   // 详情仍是缓存里的那份
        QCOMPARE(entry(QStringLiteral("8")).value("name").toString(), QStringLiteral("MV8"));
    }

    // 两首歌的百科同时开着：各按 songId 存；已取到的 id 再要不会再发请求
    void wikiSlotsCoexistBySongId()
    {
        int wikiRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/link/page/parent/relation/construct/info") {
                ++wikiRequests;
                respond(200, json(QJsonObject{{"code", 200},
                                              {"data", QJsonObject{{"blocks", QJsonArray{}}}}}));
                return;
            }
            if (req.path == "/api/song/creators" || req.path == "/api/song/chorus") {
                respond(200, "{\"code\":200}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto entry = [this](const QString &id) {
            return m_store->data().value("wiki").toMap().value(id).toMap();
        };
        m_store->request(QStringLiteral("wiki:1"));
        QVERIFY(waitUntil([&] { return entry(QStringLiteral("1")).value("loaded").toBool(); }));
        m_store->request(QStringLiteral("wiki:2"));
        QVERIFY(waitUntil([&] { return entry(QStringLiteral("2")).value("loaded").toBool(); }));
        QCOMPARE(wikiRequests, 2);

        m_store->request(QStringLiteral("wiki:1"));
        QTest::qWait(150);
        QCOMPARE(wikiRequests, 2);
        QCOMPARE(entry(QStringLiteral("2")).value("songId").toDouble(), 2.0);
    }

    // 没有 pureMusic/nolyric 标记的空歌词可能是瞬时空包：不能永久缓存成「暂无歌词」
    void unexplainedEmptyLyricsIsNotCached()
    {
        int lyricRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric") {
                ++lyricRequests;
                if (lyricRequests == 1) {
                    respond(200, "{\"code\":200,\"lrc\":{\"lyric\":\"\"},"
                                 "\"tlyric\":{\"lyric\":\"\"}}");
                    return;
                }
                respond(200, "{\"code\":200,\"lrc\":{\"lyric\":\"[00:01.00]hello\"},"
                             "\"tlyric\":{\"lyric\":\"\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyrics:10"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 1; }));
        QTest::qWait(100);
        QVERIFY(lyricsOf(10).isEmpty());

        m_store->request(QStringLiteral("lyrics:10"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 2; }));
        QVERIFY(waitUntil([&] { return lyricsOf(10).value("lines").toList().size() == 1; }));
        QVERIFY(!lyricsOf(10).value("pureMusic").toBool());
    }

    // 明确的纯音乐：空歌词是正确结果，永久缓存后第二次不再请求
    void pureMusicEmptyLyricsIsCachedForever()
    {
        int lyricRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric") {
                ++lyricRequests;
                respond(200, "{\"code\":200,\"pureMusic\":true,\"lrc\":{\"lyric\":\"\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyrics:11"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 1; }));
        QVERIFY(waitUntil([&] { return !lyricsOf(11).isEmpty(); }));
        QVERIFY(lyricsOf(11).value("pureMusic").toBool());
        m_store->request(QStringLiteral("lyrics:11"));
        QTest::qWait(150);
        QCOMPARE(lyricRequests, 1);   // 命中的是永久缓存
        QVERIFY(lyricsOf(11).value("lines").toList().isEmpty());
        QVERIFY(lyricsOf(11).value("pureMusic").toBool());   // 标记也在缓存里
    }

    // 旧接口用 nolyric 表示同一件事：歌曲本身没有歌词
    void nolyricEmptyLyricsIsPureMusic()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric") {
                respond(200, "{\"code\":200,\"nolyric\":true,\"lrc\":{\"lyric\":\"\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyrics:12"));
        QVERIFY(waitUntil([&] { return lyricsOf(12).value("pureMusic").toBool(); }));
        QVERIFY(lyricsOf(12).value("lines").toList().isEmpty());
    }

    // 对方设置隐私是预期状态：页面内显示，不发 requestFailed（否则会弹全局错误）
    void followersPrivacyIsInlineRestriction()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/user/getfolloweds/4242") {
                respond(200, "{\"code\":-460,\"message\":\"用户隐私无权限查看\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy restricted(m_store, &Store::requestRestricted);
        QSignalSpy failed(m_store, &Store::requestFailed);

        m_store->request(QStringLiteral("userFolloweds:4242"));
        QVERIFY(waitUntil([&] { return restricted.count() == 1; }));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(restricted.first().at(0).toString(), QStringLiteral("userFolloweds:4242"));
        const QVariantMap page = m_store->data().value("userFolloweds").toMap();
        QCOMPARE(page.value("userId").toDouble(), 4242.0);
        QCOMPARE(page.value("restricted").toBool(), true);
        QCOMPARE(page.value("message").toString(), QStringLiteral("用户隐私无权限查看"));
    }

    // 用户主页本身被隐私挡住：页面内说明，不当作网络失败。
    void privateUserProfileIsInlineRestriction()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/user/detail/9") {
                respond(200, "{\"code\":-460,\"message\":\"用户隐私无权限查看\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy restricted(m_store, &Store::requestRestricted);
        QSignalSpy failed(m_store, &Store::requestFailed);

        m_store->request(QStringLiteral("user:9"));
        QVERIFY(waitUntil([&] { return restricted.count() == 1; }));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(restricted.first().at(0).toString(), QStringLiteral("user:9"));
        const QVariantMap user = m_store->data().value("user").toMap().value("9").toMap();
        QCOMPARE(user.value("id").toDouble(), 9.0);
        QCOMPARE(user.value("restricted").toBool(), true);
        QCOMPARE(user.value("message").toString(), QStringLiteral("用户隐私无权限查看"));
    }

    // 扫码 803 的 cookie 在新版接口里是字符串；不能只认 cookieSet，否则扫码后永远没有 MUSIC_U。
    void qrLoginAcceptsCookieString()
    {
        bool keyHasType = false;
        bool pollHasType = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/login/qrcode/unikey") {
                keyHasType = req.query.contains("type=3");
                respond(200, "{\"code\":200,\"unikey\":\"qr-key\"}");
                return;
            }
            if (req.path == "/api/login/qrcode/client/login") {
                pollHasType = req.query.contains("type=3");
                respond(200, "{\"code\":803,\"cookie\":\"MUSIC_U=session-cookie; __csrf=session-csrf; NMTID=session-nmtid\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        Session session(m_client, m_store);
        session.start();
        QVERIFY(waitUntil([&] { return session.state() == "done"; }, 5000));
        QVERIFY(keyHasType);
        QVERIFY(pollHasType);
        QCOMPARE(m_client->cookieJar().value("MUSIC_U"), QStringLiteral("session-cookie"));
        QCOMPARE(m_client->cookieJar().value("__csrf"), QStringLiteral("session-csrf"));
    }

    // 页面退栈时 id 会短暂归零：这些 key 不能发网络请求，也不能报失败。
    void zeroIdsNeverHitNetwork()
    {
        int requests = 0;
        m_api->handler = [&](const FakeApi::Request &, const FakeApi::Responder &respond) {
            ++requests;
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::requestFailed);
        const QStringList keys{
            QStringLiteral("user:0"), QStringLiteral("artist:0"), QStringLiteral("album:0"),
            QStringLiteral("mv:0"), QStringLiteral("playlist:0"), QStringLiteral("artistSongs:0:0"),
            QStringLiteral("lyrics:0"), QStringLiteral("comments:0"), QStringLiteral("comments:0:0"),
            QStringLiteral("commentReplies:0:0"), QStringLiteral("radio:0"),
            QStringLiteral("radio:0:30"),
        };
        for (const QString &key : keys)
            m_store->request(key);
        QTest::qWait(200);
        QCOMPARE(requests, 0);
        QCOMPARE(failed.count(), 0);
    }

    // 歌单写成功后，下一次 playlist:<id> 请求必须重新取，不能拿写前的详情
    void playlistWriteInvalidatesCachedDetail()
    {
        bool removed = false;
        int detailRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v6/playlist/detail") {
                ++detailRequests;
                QJsonArray tracks;
                if (!removed)
                    tracks.append(QJsonObject{{"id", 7}, {"name", "song"},
                                              {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}});
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{
                        {"id", 501}, {"name", "P"}, {"coverImgUrl", ""}, {"description", ""},
                        {"trackCount", removed ? 0 : 1}, {"tracks", tracks},
                        {"trackIds", removed ? QJsonArray{} : QJsonArray{QJsonObject{{"id", 7}}}}}}}));
                return;
            }
            if (req.path == "/weapi/playlist/manipulate/tracks") {
                removed = true;
                respond(200, "{\"code\":200}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlist:501"));
        QVERIFY(waitUntil([&] { return detailRequests >= 1; }));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("playlistDetails").toMap().contains(QStringLiteral("501"));
        }));

        m_store->request(QStringLiteral("playlist:501"));
        QTest::qWait(100);
        QCOMPARE(detailRequests, 1);   // 新鲜期内不重复取

        m_store->removeSongFromPlaylist(501, 7);
        QVERIFY(waitUntil([&] {
            const QVariantMap detail = m_store->personal().value("playlistDetails").toMap()
                                           .value(QStringLiteral("501")).toMap();
            return detail.value("tracks").toList().isEmpty();
        }));

        m_store->request(QStringLiteral("playlist:501"));
        QVERIFY(waitUntil([&] { return detailRequests >= 2; }));
    }

    // 请求在途时再点一次不能被丢掉：回包落地后要追发最后一次意图
    void secondLikeClickIsQueued()
    {
        int likeRequests = 0;
        QList<FakeApi::Responder> held;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/radio/like") {
                ++likeRequests;
                held.append(respond);
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->setLiked(5, true);
        QVERIFY(waitUntil([&] { return likeRequests == 1; }));
        QVERIFY(likedIdsContain(m_store, 5));

        m_store->setLiked(5, false);   // 第一次还没回来
        QCOMPARE(likeRequests, 1);     // 但不丢意图，也不并发发请求
        QVERIFY(!likedIdsContain(m_store, 5));

        held.takeFirst()(200, "{\"code\":200}");
        QVERIFY(waitUntil([&] { return likeRequests == 2; }));
        held.takeFirst()(200, "{\"code\":200}");
        QVERIFY(waitUntil([&] { return held.isEmpty(); }));
        QVERIFY(waitUntil([&] { return !likedIdsContain(m_store, 5); }));
        QCOMPARE(likeRequests, 2);
    }

    // 红心详情晚到：期间已取消红心，就不能把曲目再插回 liked.tracks
    void lateLikeDetailDoesNotReaddUnlikedSong()
    {
        bool detailHeld = false;
        FakeApi::Responder heldDetail;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/nuser/account/get") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", 42}, {"nickname", "same"}}}}));
            } else if (req.path == "/api/user/playlist") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonArray{QJsonObject{
                        {"id", 100}, {"trackCount", 0},
                        {"creator", QJsonObject{{"userId", 42}}}}}}}));
            } else if (req.path == "/api/song/like/get") {
                respond(200, "{\"code\":200,\"ids\":[]}");
            } else if (req.path == "/api/v6/playlist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{{"id", 100}, {"name", "liked"}, {"trackCount", 0},
                                             {"tracks", QJsonArray{}}, {"trackIds", QJsonArray{}}}}}));
            } else if (req.path == "/api/v3/song/detail") {
                detailHeld = true;
                heldDetail = respond;
            } else if (req.path == "/weapi/radio/like") {
                respond(200, "{\"code\":200}");
            } else {
                respond(200, "{\"code\":200}");
            }
        };

        m_store->refresh();
        QVERIFY(waitUntil([&] {
            const QVariant liked = m_store->personal().value("liked");
            return liked.isValid() && !liked.isNull();
        }));

        m_store->setLiked(77, true);
        QVERIFY(waitUntil([&] { return detailHeld; }));

        m_store->setLiked(77, false);
        QVERIFY(waitUntil([&] { return !likedIdsContain(m_store, 77); }));

        heldDetail(200, json(QJsonObject{
            {"code", 200},
            {"songs", QJsonArray{QJsonObject{{"id", 77}, {"name", "song"},
                                             {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}}}}}));
        QTest::qWait(100);
        QVERIFY(m_store->personal().value("liked").toMap().value("tracks").toList().isEmpty());
    }

private:
    // 心动模式用的假服务器：红心歌单 [7]，点红心的详情是 77；推荐的第一首 = 种子 × 10，看得出是哪一次请求的结果。
    // hold(songId, respond) 返回 true 时由测试自己决定何时回包
    void serveHeart(std::function<bool(double, const FakeApi::Responder &)> hold = {})
    {
        const auto song = [](double id) {
            return QJsonObject{{"id", id}, {"name", "song"},
                               {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}};
        };
        m_api->handler = [song, hold](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/nuser/account/get") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", 42}, {"nickname", "n"}}}}));
            } else if (req.path == "/api/user/playlist") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonArray{QJsonObject{
                        {"id", 100}, {"trackCount", 1},
                        {"creator", QJsonObject{{"userId", 42}}}}}}}));
            } else if (req.path == "/api/song/like/get") {
                respond(200, "{\"code\":200,\"ids\":[7]}");
            } else if (req.path == "/api/v6/playlist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{{"id", 100}, {"name", "liked"}, {"trackCount", 1},
                                             {"tracks", QJsonArray{song(7)}},
                                             {"trackIds", QJsonArray{QJsonObject{{"id", 7}}}}}}}));
            } else if (req.path == "/api/v3/song/detail") {
                respond(200, json(QJsonObject{{"code", 200}, {"songs", QJsonArray{song(77)}}}));
            } else if (req.path == "/api/playmode/intelligence/list") {
                const QUrlQuery query(QString::fromLatin1(req.query));
                const double seed = query.queryItemValue(QStringLiteral("songId")).toDouble();
                const FakeApi::Responder answer = [respond, song, seed](int status, const QByteArray &) {
                    respond(status, json(QJsonObject{
                        {"code", 200},
                        {"data", QJsonArray{QJsonObject{{"songInfo", song(seed * 10)}}}}}));
                };
                if (!hold || !hold(seed, answer))
                    answer(200, {});
            } else {
                respond(200, "{\"code\":200}");
            }
        };
    }
    bool heartIs(double seed) const
    {
        const QVariantMap heart = m_store->personal().value("heart").toMap();
        const QVariantList tracks = heart.value("tracks").toList();
        return heart.value("seed").toMap().value("id").toDouble() == seed
            && !tracks.isEmpty() && tracks.first().toMap().value("id").toDouble() == seed * 10;
    }
    QList<double> heartTrackIds() const
    {
        QList<double> ids;
        for (const QVariant &t : m_store->personal().value("heart").toMap().value("tracks").toList())
            ids.append(t.toMap().value("id").toDouble());
        return ids;
    }

private slots:
    // 心动模式以红心第一首为种子：新点的红心排到第一，心动模式跟着换；取消后换回原来的第一首
    void heartFollowsTheFirstLikedSong()
    {
        serveHeart();
        m_store->refresh();
        QVERIFY(waitUntil([&] { return heartIs(7); }));

        m_store->setLiked(77, true);
        QVERIFY(waitUntil([&] { return heartIs(77); }));

        m_store->setLiked(77, false);
        QVERIFY(waitUntil([&] { return heartIs(7); }));

        // 流的续取：从正在放的这首（70）接着推荐，接在后面、不重复，种子不变
        m_store->request(QStringLiteral("heart:next:70"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("heart").toMap().value("tracks").toList().size() == 2;
        }));
        const QVariantMap heart = m_store->personal().value("heart").toMap();
        QCOMPARE(heart.value("seed").toMap().value("id").toDouble(), 7.0);
        QCOMPARE(heart.value("tracks").toList().at(1).toMap().value("id").toDouble(), 700.0);
        m_store->request(QStringLiteral("heart:next:70"));
        QTest::qWait(100);
        QCOMPARE(m_store->personal().value("heart").toMap().value("tracks").toList().size(), 2);
    }

    // 续取：同一条流同时只续一次；续取在途时换了种子（点了红心），回来的那批属于旧的流，丢掉。
    // 曾经两种写法互不知道，旧种子的推荐会接到新的流后面
    void heartRefillIsSerialAndDropsStaleBatches()
    {
        QList<FakeApi::Responder> held;
        int refills = 0;
        serveHeart([&](double seed, const FakeApi::Responder &answer) {
            if (seed != 70 && seed != 71)
                return false;
            ++refills;
            held.append(answer);
            return true;
        });
        m_store->refresh();
        QVERIFY(waitUntil([&] { return heartIs(7); }));

        m_store->request(QStringLiteral("heart:next:70"));
        QVERIFY(waitUntil([&] { return refills == 1; }));
        m_store->request(QStringLiteral("heart:next:71"));   // 上一次还没回：不再发
        QTest::qWait(100);
        QCOMPARE(refills, 1);

        m_store->setLiked(77, true);
        QVERIFY(waitUntil([&] { return heartIs(77); }));
        held.takeFirst()(200, {});
        QTest::qWait(100);
        QCOMPARE(heartTrackIds(), QList<double>{770});   // 旧流的 700 没接上来

        m_store->request(QStringLiteral("heart:next:71"));   // 上一次已结束，可以再续
        QVERIFY(waitUntil([&] { return refills == 2; }));
        held.takeFirst()(200, {});
        QVERIFY(waitUntil([&] { return heartTrackIds() == QList<double>({770, 710}); }));
    }

    // 续取失败（这里是风控拒绝）也要结束这次续取：下一次快放完时照常再要。
    // 曾经 fetch 失败不回调，「正在续取」永远不清，私人漫游和心动模式从此再也续不上
    void failedRefillDoesNotStopTheStream()
    {
        serveHeart();
        const auto base = m_api->handler;
        int heartRefills = 0;
        int fmCalls = 0;
        bool rejectNextFm = false;
        m_api->handler = [&, base](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            const QString songId = QUrlQuery(QString::fromLatin1(req.query)).queryItemValue(QStringLiteral("songId"));
            if (req.path == "/api/playmode/intelligence/list" && songId != QLatin1String("7")
                && ++heartRefills == 1) {
                respond(200, "{\"code\":-460,\"message\":\"拒绝\"}");
                return;
            }
            if (req.path == "/api/v1/radio/get") {
                ++fmCalls;
                if (std::exchange(rejectNextFm, false)) {
                    respond(200, "{\"code\":-460,\"message\":\"拒绝\"}");
                    return;
                }
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonArray{QJsonObject{
                    {"id", 500 + fmCalls}, {"name", "fm"}, {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}}}}}));
                return;
            }
            base(req, respond);
        };
        const auto fmCount = [&] { return m_store->personal().value("fm").toMap().value("tracks").toList().size(); };

        m_store->refresh();
        QVERIFY(waitUntil([&] { return heartIs(7) && fmCount() == 1; }));
        // 刷新时私人漫游取了不止一次：等它们落地，并等过流量计的 1 秒窗口
        // （同一个请求 1 秒内第 4 次会被拦下，测试把真实里隔几首歌的续取压在了一起）
        QTest::qWait(1100);
        const int startupCalls = fmCalls;

        rejectNextFm = true;
        m_store->request(QStringLiteral("fm:next"));   // 被拒
        QVERIFY(waitUntil([&] { return fmCalls == startupCalls + 1; }));
        QTest::qWait(100);
        m_store->request(QStringLiteral("fm:next"));   // 仍然要得出去
        QVERIFY(waitUntil([&] { return fmCount() == 2; }));

        m_store->request(QStringLiteral("heart:next:70"));   // 被拒
        QVERIFY(waitUntil([&] { return heartRefills == 1; }));
        QTest::qWait(100);
        m_store->request(QStringLiteral("heart:next:71"));
        QVERIFY(waitUntil([&] { return heartTrackIds() == QList<double>({70, 710}); }));
    }

    // 搜索的「用户」栏是单槽：晚到的旧词结果不能盖掉新词的
    void lateSearchUsersResultIsDropped()
    {
        FakeApi::Responder heldA;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            const QString s = QUrlQuery(QString::fromLatin1(req.query)).queryItemValue(QStringLiteral("s"));
            const QByteArray reply = "{\"code\":200,\"result\":{\"userprofileCount\":1,\"userprofiles\":"
                                     "[{\"userId\":1,\"nickname\":\"" + s.toUtf8() + "\"}]}}";
            if (s == QLatin1String("a")) {
                heldA = [respond, reply](int status, const QByteArray &) { respond(status, reply); };
                return;
            }
            respond(200, reply);
        };
        m_store->request(QStringLiteral("searchUsers:a"));
        QVERIFY(waitUntil([&] { return bool(heldA); }));
        m_store->request(QStringLiteral("searchUsers:ab"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("searchUsers").toMap().value("query").toString() == QLatin1String("ab");
        }));
        heldA(200, {});
        QTest::qWait(100);
        QCOMPARE(m_store->data().value("searchUsers").toMap().value("query").toString(), QStringLiteral("ab"));
    }

    // 从排行榜收藏：卡片不在 personal/搜索结果里，也要用排行榜已有的名字与封面
    void subscribingChartKeepsItsName()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/toplist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"list", QJsonArray{QJsonObject{
                        {"id", 777}, {"name", "Chart"}, {"coverImgUrl", "http://cover"},
                        {"trackCount", 5}, {"updateFrequency", "weekly"},
                        {"tracks", QJsonArray{}}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->waitForInitial();
        QVERIFY(!m_store->data().value("toplists").toList().isEmpty());

        QSignalSpy changed(m_store, &Store::playlistChanged);
        m_store->setPlaylistSubscribed(777, true);
        QVERIFY(waitUntil([&] { return changed.count() > 0; }));

        const QVariantList subscribed = m_store->personal().value("subscribed").toList();
        QCOMPARE(subscribed.size(), 1);
        QCOMPARE(subscribed.first().toMap().value("name").toString(), QStringLiteral("Chart"));
    }

    // 已存在的歌再加一次：列表与计数都不能变（服务器的幂等请求不代表本地新增）
    void addingPresentSongDoesNotInflateCount()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/nuser/account/get") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", 42}, {"nickname", "n"}}}}));
            } else if (req.path == "/api/user/playlist") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonArray{
                        QJsonObject{{"id", 100}, {"trackCount", 0},
                                    {"creator", QJsonObject{{"userId", 42}}}},
                        QJsonObject{{"id", 101}, {"name", "Mine"}, {"trackCount", 1},
                                    {"creator", QJsonObject{{"userId", 42}}}}}}}));
            } else if (req.path == "/api/song/like/get") {
                respond(200, "{\"code\":200,\"ids\":[]}");
            } else if (req.path == "/api/v6/playlist/detail") {
                const QUrlQuery query(QString::fromLatin1(req.query));
                const double id = query.queryItemValue(QStringLiteral("id")).toDouble();
                QJsonArray tracks;
                QJsonArray trackIds;
                if (id == 101) {
                    tracks.append(QJsonObject{{"id", 7}, {"name", "song"},
                                              {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}});
                    trackIds.append(QJsonObject{{"id", 7}});
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{
                        {"id", id}, {"name", "P"}, {"trackCount", id == 101 ? 1 : 0},
                        {"tracks", tracks}, {"trackIds", trackIds}}}}));
            } else {
                respond(200, "{\"code\":200}");
            }
        };

        m_store->refresh();
        QVERIFY(waitUntil([&] {
            const QVariant liked = m_store->personal().value("liked");
            return liked.isValid() && !liked.isNull();
        }));

        m_store->request(QStringLiteral("playlist:101"));
        QVERIFY(waitUntil([&] {
            const QVariant detail = m_store->personal().value("playlistDetails").toMap()
                                        .value(QStringLiteral("101"));
            return detail.toMap().value("tracks").toList().size() == 1;
        }));

        QSignalSpy changed(m_store, &Store::playlistChanged);
        m_store->addSongToPlaylist(101, 7);
        QVERIFY(waitUntil([&] { return changed.count() > 0; }));
        QTest::qWait(100);

        const QVariantMap detail = m_store->personal().value("playlistDetails").toMap()
                                       .value(QStringLiteral("101")).toMap();
        QCOMPARE(detail.value("count").toDouble(), 1.0);
        QCOMPARE(detail.value("tracks").toList().size(), 1);
    }

    // 歌单内排序：服务器成功后，本地详情按提交的完整顺序重排；列表不丢曲目
    void reorderTracksUpdatesLocalOrder()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v6/playlist/detail") {
                QJsonArray tracks;
                QJsonArray ids;
                for (int id : {1, 2, 3}) {
                    tracks.append(QJsonObject{{"id", id}, {"name", QStringLiteral("song%1").arg(id)},
                                              {"ar", QJsonArray{}}, {"al", QJsonObject{}}, {"dt", 1000}});
                    ids.append(QJsonObject{{"id", id}});
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{{"id", 501}, {"name", "P"}, {"trackCount", 3},
                                             {"tracks", tracks}, {"trackIds", ids}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlist:501"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("playlistDetails").toMap()
                .value(QStringLiteral("501")).toMap().value("tracks").toList().size() == 3;
        }));

        QSignalSpy changed(m_store, &Store::playlistChanged);
        m_store->reorderPlaylistTracks(501, {3, 1, 2});
        QVERIFY(waitUntil([&] { return changed.count() > 0; }));

        const QVariantList tracks = m_store->personal().value("playlistDetails").toMap()
            .value(QStringLiteral("501")).toMap().value("tracks").toList();
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks.at(0).toMap().value("id").toDouble(), 3.0);
        QCOMPARE(tracks.at(1).toMap().value("id").toDouble(), 1.0);
        QCOMPARE(tracks.at(2).toMap().value("id").toDouble(), 2.0);
    }

    // 歌单描述写：成功后更新本地详情并作废缓存；失败由 playlistActionFailed 处理。
    void playlistDescriptionUpdatesLocalDetail()
    {
        bool wrote = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v6/playlist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{
                        {"id", 501}, {"name", "P"}, {"description", "old"},
                        {"trackCount", 0}, {"tracks", QJsonArray{}}, {"trackIds", QJsonArray{}}}}}));
                return;
            }
            if (req.path == "/weapi/playlist/desc/update") {
                wrote = true;
                respond(200, "{\"code\":200}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlist:501"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("playlistDetails").toMap()
                .value(QStringLiteral("501")).toMap().value("description").toString()
                == QStringLiteral("old");
        }));

        QSignalSpy changed(m_store, &Store::playlistChanged);
        m_store->setPlaylistDescription(501, QStringLiteral("new description"));
        QVERIFY(waitUntil([&] { return wrote && changed.count() > 0; }));
        const QVariantMap detail = m_store->personal().value("playlistDetails").toMap()
            .value(QStringLiteral("501")).toMap();
        QCOMPARE(detail.value("description").toString(), QStringLiteral("new description"));
    }

    // 关注歌手 / 收藏专辑：成功后发对应 changed，不依赖当前详情是否已加载
    void followAndAlbumSubscribeEmitChanges()
    {
        m_api->handler = [](const FakeApi::Request &, const FakeApi::Responder &respond) {
            respond(200, "{\"code\":200}");
        };
        QSignalSpy artist(m_store, &Store::artistChanged);
        QSignalSpy album(m_store, &Store::albumChanged);
        m_store->setArtistSubscribed(6452, true);
        m_store->setAlbumSubscribed(32311, true);
        QVERIFY(waitUntil([&] { return artist.count() == 1 && album.count() == 1; }));
        QCOMPARE(artist.first().at(0).toDouble(), 6452.0);
        QCOMPARE(artist.first().at(1).toBool(), true);
        QCOMPARE(album.first().at(0).toDouble(), 32311.0);
        QCOMPARE(album.first().at(1).toBool(), true);
    }

    // 评论点赞：成功乐观更新；失败把 liked 与计数都改回
    void commentLikeUpdatesThenRollsBack()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v2/resource/comments") {
                respond(200, "{\"code\":200,\"data\":{\"comments\":["
                             "{\"commentId\":500,\"content\":\"a\",\"likedCount\":10,"
                             "\"liked\":false,\"time\":1,\"user\":{\"userId\":7,\"nickname\":\"u\"}},"
                             "{\"commentId\":501,\"content\":\"b\",\"likedCount\":20,"
                             "\"liked\":true,\"time\":2,\"user\":{\"userId\":8,\"nickname\":\"v\"}}]}}");
                return;
            }
            if (req.path == "/weapi/v1/comment/like") {
                respond(200, "{\"code\":200}");
                return;
            }
            if (req.path == "/weapi/v1/comment/unlike") {
                respond(200, "{\"code\":500,\"message\":\"no\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("comments:1"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("comments").toMap().value("items").toList().size() == 2;
        }));
        m_store->setCommentLiked(QStringLiteral("song"), 1, 500, true);
        QVERIFY(waitUntil([&] {
            const QVariantMap c = m_store->data().value("comments").toMap().value("items").toList().first().toMap();
            return c.value("liked").toBool() && c.value("likes").toDouble() == 11;
        }));

        QSignalSpy failed(m_store, &Store::commentLikeFailed);
        m_store->setCommentLiked(QStringLiteral("song"), 1, 501, false);
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        const QVariantMap second = m_store->data().value("comments").toMap().value("items").toList().at(1).toMap();
        QVERIFY(second.value("liked").toBool());
        QCOMPARE(second.value("likes").toDouble(), 20.0);
    }




    // 风控码可能藏在 HTTP 4xx 的响应体里：仍要解析、回滚并显示服务端原因，不能当断网
    void deviceRiskInHttpErrorBodyRollsBack()
    {
        m_api->handler = [](const FakeApi::Request &, const FakeApi::Responder &respond) {
            respond(400, "{\"code\":250,\"message\":\"系统检测到您的设备存在安全风险\"}");
        };
        QSignalSpy failed(m_store, &Store::commentLikeFailed);
        m_store->setCommentLiked(QStringLiteral("song"), 1, 888, true);
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(3).toString(),
                 QStringLiteral("系统检测到您的设备存在安全风险"));
    }

    // 设备风控是独立分类：不能当成限流或断网，更不能自动重试
    void deviceRiskIsItsOwnFailureClass()
    {
        NcmClient::Response response;
        response.ok = true;
        response.doc = QJsonDocument(QJsonObject{{"code", 250},
                                                 {"message", "系统检测到您的设备存在安全风险"}});
        QCOMPARE(NcmClient::failureOf(response), NcmClient::Failure::DeviceRisk);
    }


    // 防抖：一次快速的双击回到原状态，不应产生任何写请求
    void rapidCommentToggleCoalescesToNoRequest()
    {
        m_api->requests.clear();
        m_api->handler = [](const FakeApi::Request &, const FakeApi::Responder &respond) {
            respond(200, "{\"code\":200}");
        };
        m_store->setCommentLiked(QStringLiteral("song"), 1, 999, true);
        m_store->setCommentLiked(QStringLiteral("song"), 1, 999, false);
        QTest::qWait(500);
        int writes = 0;
        for (const FakeApi::Request &req : std::as_const(m_api->requests))
            if (req.path == "/weapi/v1/comment/like" || req.path == "/weapi/v1/comment/unlike")
                ++writes;
        QCOMPARE(writes, 0);
    }

    // 评论写入要带完整客户端身份；截断的 appver 会被服务端按设备风险拒绝
    void commentLikeUsesCompleteClientContext()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            Q_UNUSED(req);
            respond(200, "{\"code\":200}");
        };
        m_api->requests.clear();
        m_store->setCommentLiked(QStringLiteral("song"), 1, 777, true);
        QVERIFY(waitUntil([&] {
            for (const FakeApi::Request &req : std::as_const(m_api->requests))
                if (req.path == "/weapi/v1/comment/like")
                    return true;
            return false;
        }));
        QByteArray headers;
        for (const FakeApi::Request &req : std::as_const(m_api->requests))
            if (req.path == "/weapi/v1/comment/like")
                headers = req.headers;
        QVERIFY(headers.contains("os=linux"));
        QVERIFY(headers.contains("appver=1.2.1.0428"));
        QVERIFY(headers.contains("channel=netease"));
        QVERIFY(!headers.contains("appver=3.1.17;"));
    }

    // 评论走 v2：热评 sortType=2，最新页 sortType=3 带着上一页的 cursor 翻页；replyCount 进归一化结果
    void commentsUseV2WithCursorAndReplyCounts()
    {
        int hotRequests = 0;
        bool sawSecondPageCursor = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path != "/api/v2/resource/comments") {
                respond(200, "{\"code\":200}");
                return;
            }
            const QUrlQuery q(QString::fromLatin1(req.query));
            const QString sort = q.queryItemValue(QStringLiteral("sortType"));
            const QString cursor = q.queryItemValue(QStringLiteral("cursor"));
            const auto comment = [](double id, double time, int replies) {
                return QJsonObject{{"commentId", id}, {"content", "c"}, {"likedCount", 1},
                                   {"time", time}, {"liked", false},
                                   {"user", QJsonObject{{"userId", 1}, {"nickname", "u"}}},
                                   {"showFloorComment", QJsonObject{{"replyCount", replies}}}};
            };
            if (sort == QLatin1String("2")) {
                ++hotRequests;
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                    {"comments", QJsonArray{comment(10, 30, 14), comment(11, 29, 0)}},
                    {"cursor", "normalHot#2"}, {"hasMore", false}, {"totalCount", 2}}}}));
                return;
            }
            if (sort != QLatin1String("3")) {
                respond(200, "{\"code\":400}");
                return;
            }
            if (cursor.isEmpty()) {
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                    {"comments", QJsonArray{comment(20, 20, 2), comment(21, 19, 0)}},
                    {"cursor", "19"}, {"hasMore", true}, {"totalCount", 3}}}}));
                return;
            }
            sawSecondPageCursor = cursor == QLatin1String("19");
            respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                {"comments", QJsonArray{comment(22, 18, 5)}},
                {"cursor", "18"}, {"hasMore", false}, {"totalCount", 3}}}}));
        };

        m_store->request(QStringLiteral("comments:77"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("comments").toMap().value("items").toList().size() == 2;
        }));
        const QVariantList hot = m_store->data().value("comments").toMap().value("items").toList();
        QCOMPARE(hot.first().toMap().value("replyCount").toDouble(), 14.0);
        QCOMPARE(hot.last().toMap().value("replyCount").toDouble(), 0.0);
        QCOMPARE(hotRequests, 1);

        m_store->request(QStringLiteral("comments:77:0"));
        QVERIFY(waitUntil([&] {
            const QVariantMap page = m_store->data().value("commentPage").toMap();
            return page.value("offset").toInt() == 0 && page.value("items").toList().size() == 2;
        }));
        QVariantMap page = m_store->data().value("commentPage").toMap();
        QCOMPARE(page.value("total").toDouble(), 3.0);
        QCOMPARE(page.value("more").toBool(), true);
        QCOMPARE(page.value("items").toList().first().toMap().value("replyCount").toDouble(), 2.0);

        m_store->request(QStringLiteral("comments:77:2"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("commentPage").toMap().value("offset").toInt() == 2;
        }));
        page = m_store->data().value("commentPage").toMap();
        QVERIFY(sawSecondPageCursor);
        QCOMPARE(page.value("total").toDouble(), 3.0);
        QCOMPARE(page.value("more").toBool(), false);
        QCOMPARE(page.value("items").toList().first().toMap().value("id").toDouble(), 22.0);
        QCOMPARE(page.value("items").toList().first().toMap().value("replyCount").toDouble(), 5.0);
    }

    // 歌单评论用 A_PL_0_ 线程，槽位与歌曲评论分离；分页也走同一套 cursor 逻辑
    void playlistCommentsUsePlaylistThread()
    {
        QList<QByteArray> threads;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path != "/api/v2/resource/comments") {
                respond(200, "{\"code\":200}");
                return;
            }
            const QUrlQuery q(QString::fromLatin1(req.query));
            threads.append(q.queryItemValue(QStringLiteral("threadId")).toUtf8());
            const QJsonObject comment{
                {"commentId", 501}, {"content", "playlist comment"}, {"likedCount", 2},
                {"time", 10}, {"liked", false},
                {"user", QJsonObject{{"userId", 7}, {"nickname", "u"}}},
            };
            respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                {"comments", QJsonArray{comment}}, {"cursor", "10"},
                {"hasMore", false}, {"totalCount", 1}}}}));
        };

        m_store->request(QStringLiteral("playlistComments:42"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistComments").toMap().value("playlistId").toDouble() == 42;
        }));
        QCOMPARE(m_store->data().value("playlistComments").toMap().value("items").toList().size(), 1);
        QVERIFY(!threads.isEmpty());
        QCOMPARE(threads.first(), QByteArray("A_PL_0_42"));

        m_store->request(QStringLiteral("playlistComments:42:0"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistCommentPage").toMap().value("playlistId").toDouble() == 42;
        }));
        const QVariantMap page = m_store->data().value("playlistCommentPage").toMap();
        QCOMPARE(page.value("offset").toInt(), 0);
        QCOMPARE(page.value("items").toList().size(), 1);
        QCOMPARE(page.value("total").toDouble(), 1.0);
        QVERIFY(threads.size() >= 2);
        QCOMPARE(threads.last(), QByteArray("A_PL_0_42"));
    }

    // 歌单评论点赞与歌曲走同一套乐观更新：热评与分页两个槽一起改，取消时一起改回
    void playlistCommentLikeAndUnlikeUpdateBothSlots()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v2/resource/comments") {
                const QJsonObject comment{
                    {"commentId", 501}, {"content", "playlist comment"}, {"likedCount", 2},
                    {"time", 10}, {"liked", false},
                    {"user", QJsonObject{{"userId", 7}, {"nickname", "u"}}},
                };
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                    {"comments", QJsonArray{comment}}, {"cursor", "10"},
                    {"hasMore", false}, {"totalCount", 1}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlistComments:42"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistComments").toMap().value("items").toList().size() == 1;
        }));
        m_store->request(QStringLiteral("playlistComments:42:0"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistCommentPage").toMap().value("items").toList().size() == 1;
        }));

        QSignalSpy succeeded(m_store, &Store::commentLikeSucceeded);
        m_store->setCommentLiked(QStringLiteral("playlist"), 42, 501, true);
        QVERIFY(waitUntil([&] { return succeeded.count() == 1; }));
        QCOMPARE(succeeded.first().at(0).toString(), QStringLiteral("playlist"));
        QCOMPARE(succeeded.first().at(1).toDouble(), 42.0);
        QCOMPARE(succeeded.first().at(2).toDouble(), 501.0);
        QCOMPARE(succeeded.first().at(3).toBool(), true);
        for (const char *slot : {"playlistComments", "playlistCommentPage"}) {
            const QVariantMap row = m_store->data().value(QString::fromLatin1(slot)).toMap()
                                        .value("items").toList().first().toMap();
            QCOMPARE(row.value("liked").toBool(), true);
            QCOMPARE(row.value("likes").toDouble(), 3.0);
        }

        m_store->setCommentLiked(QStringLiteral("playlist"), 42, 501, false);
        QVERIFY(waitUntil([&] { return succeeded.count() == 2; }));
        QCOMPARE(succeeded.at(1).at(3).toBool(), false);
        for (const char *slot : {"playlistComments", "playlistCommentPage"}) {
            const QVariantMap row = m_store->data().value(QString::fromLatin1(slot)).toMap()
                                        .value("items").toList().first().toMap();
            QCOMPARE(row.value("liked").toBool(), false);
            QCOMPARE(row.value("likes").toDouble(), 2.0);
        }
    }

    // 歌单评论点赞失败：两个槽都回滚，信号带上 playlist 身份
    void playlistCommentLikeFailureRollsBackBothSlots()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v2/resource/comments") {
                const QJsonObject comment{
                    {"commentId", 501}, {"content", "playlist comment"}, {"likedCount", 2},
                    {"time", 10}, {"liked", false},
                    {"user", QJsonObject{{"userId", 7}, {"nickname", "u"}}},
                };
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                    {"comments", QJsonArray{comment}}, {"cursor", "10"},
                    {"hasMore", false}, {"totalCount", 1}}}}));
                return;
            }
            if (req.path == "/weapi/v1/comment/like") {
                respond(200, "{\"code\":500,\"message\":\"no\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlistComments:42"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistComments").toMap().value("items").toList().size() == 1;
        }));
        m_store->request(QStringLiteral("playlistComments:42:0"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("playlistCommentPage").toMap().value("items").toList().size() == 1;
        }));

        QSignalSpy failed(m_store, &Store::commentLikeFailed);
        m_store->setCommentLiked(QStringLiteral("playlist"), 42, 501, true);
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("playlist"));
        QCOMPARE(failed.first().at(1).toDouble(), 42.0);
        QCOMPARE(failed.first().at(2).toDouble(), 501.0);
        for (const char *slot : {"playlistComments", "playlistCommentPage"}) {
            const QVariantMap row = m_store->data().value(QString::fromLatin1(slot)).toMap()
                                        .value("items").toList().first().toMap();
            QCOMPARE(row.value("liked").toBool(), false);
            QCOMPARE(row.value("likes").toDouble(), 2.0);
        }
    }

    // 冷启动直接要第 N 页（CLI / 缓存清空）时没有上一页的 cursor：从头顺序补，不能把第一页当成第 N 页
    void commentPageAfterColdOffsetWalksCursors()
    {
        int requests = 0;
        bool secondPageGotCursor = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path != "/api/v2/resource/comments") {
                respond(200, "{\"code\":200}");
                return;
            }
            ++requests;
            const QUrlQuery q(QString::fromLatin1(req.query));
            QJsonArray comments;
            if (q.queryItemValue(QStringLiteral("cursor")).isEmpty()) {
                for (int i = 0; i < 20; ++i) {
                    comments.append(QJsonObject{
                        {"commentId", 100 + i}, {"content", "c"}, {"time", 1000 - i},
                        {"user", QJsonObject{{"userId", 1}, {"nickname", "u"}}},
                        {"showFloorComment", QJsonObject{{"replyCount", i}}},
                    });
                }
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                    {"comments", comments}, {"cursor", "981"}, {"hasMore", true},
                    {"totalCount", 21}}}}));
                return;
            }
            secondPageGotCursor = q.queryItemValue(QStringLiteral("cursor")) == QLatin1String("981")
                                  && q.queryItemValue(QStringLiteral("pageNo")) == QLatin1String("2");
            respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonObject{
                {"comments", QJsonArray{QJsonObject{{"commentId", 200}, {"content", "c"},
                                                    {"time", 980}, {"user", QJsonObject{}}}}},
                {"cursor", "980"}, {"hasMore", false}, {"totalCount", 21}}}}));
        };

        m_store->request(QStringLiteral("comments:77:20"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("commentPage").toMap().value("offset").toInt() == 20;
        }));
        const QVariantMap page = m_store->data().value("commentPage").toMap();
        QVERIFY(secondPageGotCursor);
        QCOMPARE(requests, 2);
        QCOMPARE(page.value("items").toList().first().toMap().value("id").toDouble(), 200.0);
    }

    // 用户主页：资料 + 公开歌单都进 data.user
    void userProfileAndPlaylists()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/user/detail/42") {
                // 等级与累计听歌在响应顶层，不在 profile 里（网易云 v1/user/detail 的实际形状）
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"level", 9},
                    {"listenSongs", 1031},
                    {"profile", QJsonObject{{"userId", 42}, {"nickname", "Listener"},
                                            {"signature", "hi"}, {"follows", 3}, {"followeds", 4},
                                            {"playlistCount", 1}, {"avatarUrl", ""}}}}));
                return;
            }
            if (req.path == "/weapi/user/playlist") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonArray{QJsonObject{
                        {"id", 7}, {"name", "Public"}, {"coverImgUrl", ""}, {"trackCount", 2},
                        {"description", "d"}, {"creator", QJsonObject{{"userId", 42}}}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        m_store->request(QStringLiteral("user:42"));
        QVERIFY(waitUntil([&] {
            const QVariantMap u = m_store->data().value("user").toMap().value("42").toMap();
            return u.value("name").toString() == "Listener"
                && u.value("playlists").toList().size() == 1;
        }));
        const QVariantMap u = m_store->data().value("user").toMap().value("42").toMap();
        QCOMPARE(u.value("id").toDouble(), 42.0);
        QCOMPARE(u.value("follows").toDouble(), 3.0);
        QCOMPARE(u.value("level").toDouble(), 9.0);
        QCOMPARE(u.value("listenSongs").toDouble(), 1031.0);
        QCOMPARE(u.value("playlists").toList().first().toMap().value("name").toString(), QStringLiteral("Public"));
    }

    // 关注列表：第二页接在第一页后面；失败按请求时的 key 报，界面的重试才对得上
    void followsPageAndFailureKeys()
    {
        bool failSecond = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/user/getfollows/42") {
                if (req.query.contains("offset=30") && failSecond) {
                    respond(200, "{\"code\":500}");
                    return;
                }
                const bool second = req.query.contains("offset=30");
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", !second},
                    {"follow", QJsonArray{QJsonObject{{"userId", second ? 2 : 1}, {"nickname", second ? "B" : "A"},
                                                      {"mutual", !second}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::requestFailed);

        m_store->request(QStringLiteral("userFollows:42"));
        QVERIFY(waitUntil([&] { return m_store->data().value("userFollows").toMap().value("items").toList().size() == 1; }));
        m_store->request(QStringLiteral("userFollows:42:30"));
        QVERIFY(waitUntil([&] { return m_store->data().value("userFollows").toMap().value("items").toList().size() == 2; }));
        const QVariantList items = m_store->data().value("userFollows").toMap().value("items").toList();
        QCOMPARE(items.at(0).toMap().value("mutual").toBool(), true);
        QCOMPARE(items.at(1).toMap().value("name").toString(), QStringLiteral("B"));
        QVERIFY(m_api->requests.last().query.contains("order=true"));   // 关注按新到旧

        // 同一页回来第二次（滚到底触发了两次、点了重试）：不会出现重复的人
        m_store->request(QStringLiteral("userFollows:42:30"));
        QTest::qWait(150);
        QCOMPARE(m_store->data().value("userFollows").toMap().value("items").toList().size(), 2);

        failSecond = true;
        m_store->request(QStringLiteral("userFollows:42:30"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("userFollows:42:30"));
    }

    // 两页之间名单变了（有人挪到了前面）：下一页里有已经显示的人。去重后条数比服务器的偏移少，
    // 但下一页的起点仍按服务器算（next），不按条数——否则会重取同一段、甚至一直卡在那里
    void shiftedPageStillAdvances()
    {
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/user/getfollows/42") {
                const int offset = QUrlQuery(QString::fromLatin1(req.query)).queryItemValue(QStringLiteral("offset")).toInt();
                const int id = offset == 2 ? 2 : 1;   // 第 0、1 页都是 A（挪动），第 2 页是 B
                respond(200, json(QJsonObject{{"code", 200}, {"more", offset < 2},
                    {"follow", QJsonArray{QJsonObject{{"userId", id}, {"nickname", id == 1 ? "A" : "B"}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        const auto follows = [&] { return m_store->data().value("userFollows").toMap(); };
        m_store->request(QStringLiteral("userFollows:42"));
        QVERIFY(waitUntil([&] { return follows().value("next").toInt() == 1; }));
        m_store->request(QStringLiteral("userFollows:42:1"));
        QVERIFY(waitUntil([&] { return follows().value("next").toInt() == 2; }));
        QCOMPARE(follows().value("items").toList().size(), 1);   // A 没有重复
        m_store->request(QStringLiteral("userFollows:42:2"));
        QVERIFY(waitUntil([&] { return follows().value("items").toList().size() == 2; }));
    }

    // 只读信箱：会话与历史都归一化到 data.inbox / data.inboxThread
    void readsInboxAndThread()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/msg/private/users") {
                const QJsonObject track{{"id", 9}, {"name", "s"}, {"ar", QJsonArray{}},
                                        {"al", QJsonObject{}}, {"dt", 1000}};
                const QJsonObject payload{{"msg", "hello"}, {"song", track}};
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", false}, {"newMsgCount", 3},
                    {"msgs", QJsonArray{QJsonObject{
                        {"fromUser", QJsonObject{{"userId", 7}, {"nickname", "Peer"}, {"avatarUrl", "http://a"}}},
                        {"toUser", QJsonObject{{"userId", 1}, {"nickname", "Me"}, {"avatarUrl", "http://m"}}},
                        {"lastMsg", QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))},
                        {"lastMsgTime", 123}, {"lastMsgId", 55}, {"newMsgCount", 3},
                        {"user", QJsonObject{{"msgCount", 8}}},
                    }}}}));
                return;
            }
            if (req.path == "/weapi/msg/private/history") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", false},
                    {"msgs", QJsonArray{QJsonObject{
                        {"id", 55},
                        {"fromUser", QJsonObject{{"userId", 7}, {"nickname", "Peer"}, {"avatarUrl", "http://a"}}},
                        {"toUser", QJsonObject{{"userId", 1}, {"nickname", "Me"}}},
                        {"msg", "{\"msg\":\"hello\"}"}, {"time", 123},
                    }}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        m_store->request(QStringLiteral("inbox"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("inbox").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap c = m_store->data().value("inbox").toMap().value("items").toList().first().toMap();
        QCOMPARE(c.value("userId").toDouble(), 7.0);
        QCOMPARE(c.value("name").toString(), QStringLiteral("Peer"));
        QCOMPARE(c.value("unread").toDouble(), 3.0);
        QCOMPARE(c.value("kind").toString(), QStringLiteral("song"));

        m_store->request(QStringLiteral("inboxThread:7"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("inboxThread").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap m = m_store->data().value("inboxThread").toMap().value("items").toList().first().toMap();
        QCOMPARE(m.value("fromUserId").toDouble(), 7.0);
        QCOMPARE(m.value("mine").toBool(), false);
        QCOMPARE(m.value("message").toMap().value("text").toString(), QStringLiteral("hello"));

        // 每一行带 key（界面的列表模型与续页去重认的是同一个）；续页回来已有的行不会重复
        QCOMPARE(c.value("key").toString(), QStringLiteral("7"));
        QVERIFY(!m.value("key").toString().isEmpty());
        const int before = m_api->requests.size();
        m_store->request(QStringLiteral("inbox:1"));
        m_store->request(QStringLiteral("inboxThread:7:%1").arg(qlonglong(m.value("time").toDouble())));
        QVERIFY(waitUntil([&] { return m_api->requests.size() >= before + 2; }));
        QTest::qWait(150);
        QCOMPARE(m_store->data().value("inbox").toMap().value("items").toList().size(), 1);
        QCOMPARE(m_store->data().value("inboxThread").toMap().value("items").toList().size(), 1);
    }

    // 统计 / 通知 / 等级 / 逐字歌词 / 楼中楼：归一化字段与请求 key 的回归
    void readOnlyExtrasNormalize()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/content/activity/listen/data/total") {
                respond(200, json(QJsonObject{{"code", 200},
                                               {"data", QJsonObject{{"totalDuration", 7200}}}}));
                return;
            }
            if (req.path == "/api/pl/count") {
                respond(200, json(QJsonObject{{"code", 200}, {"msg", 2}, {"comment", 3},
                                               {"event", 0}, {"follow", 0}, {"notice", 0},
                                               {"forward", 0}, {"newProgramCount", 0}}));
                return;
            }
            if (req.path == "/api/user/level") {
                respond(200, json(QJsonObject{{"code", 200},
                                               {"data", QJsonObject{{"level", 9}, {"nowPlayCount", 10},
                                                                    {"nextPlayCount", 20}, {"progress", 0.5}}}}));
                return;
            }
            if (req.path == "/api/song/lyric/v1") {
                respond(200, json(QJsonObject{{"code", 200},
                                               {"lrc", QJsonObject{{"lyric", "[00:01.00]hello"}}},
                                               {"tlyric", QJsonObject{{"lyric", ""}}},
                                               {"yrc", QJsonObject{{"lyric", ""}}},
                                               {"klyric", QJsonObject{{"lyric", ""}}},
                                               {"romalrc", QJsonObject{{"lyric", ""}}}}));
                return;
            }
            if (req.path == "/weapi/resource/comment/floor/get") {
                respond(200, json(QJsonObject{{"code", 200},
                                               {"data", QJsonObject{{"comments", QJsonArray{
                                                   QJsonObject{{"commentId", 2}, {"content", "reply"},
                                                               {"likedCount", 0}, {"liked", false},
                                                               {"time", 2}, {"parentCommentId", 1},
                                                               {"user", QJsonObject{{"userId", 9}, {"nickname", "u"}}}}}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("listen:total"));
        m_store->request(QStringLiteral("notifications"));
        m_store->request(QStringLiteral("userLevel"));
        m_store->request(QStringLiteral("lyricsNew:1"));
        m_store->request(QStringLiteral("commentReplies:1:1"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("listenTotal").toMap().value("seconds").toDouble() == 7200
                && m_store->data().value("notifications").toMap().value("total").toInt() == 5
                && m_store->data().value("userLevel").toMap().value("level").toInt() == 9
                && m_store->data().value("lyricsNew").toMap().value("lines").toList().size() == 1
                && m_store->data().value("commentReplies").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap reply = m_store->data().value("commentReplies").toMap()
                                      .value("items").toList().first().toMap();
        QCOMPARE(reply.value("parentId").toDouble(), 1.0);
        QCOMPARE(reply.value("userId").toDouble(), 9.0);
    }

    // 听歌足迹走 SDK typed 解析后，前端拿到的 map 形状与字段类型不变
    void listenStatsKeepQmlShape()
    {
        const QJsonObject distribution{
            {"timeDistribution", QJsonObject{{"morning", 60}, {"night", 40}}}};
        const QJsonObject friends{{"friendCount", 3}, {"rank", 2}};
        const QJsonObject today{{"playCount", 7}, {"songCount", 5}};
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/content/activity/listen/data/realtime/report") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"data", QJsonObject{
                         {"startTime", 1750000000000.0},
                         {"endTime", 1750604800000.0},
                         {"userNickName", "Listener"},
                         {"listenTimeDistributionBlock", distribution},
                         {"weekFriendsListenBlock", friends},
                         {"weekTodayListenBlock", today}}}}));
                return;
            }
            if (req.path == "/api/content/activity/listen/data/song/play/rank") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"data", QJsonObject{
                         {"startTime", 1750000000000.0},
                         {"endTime", 1750604800000.0},
                         {"songCount", 1},
                         {"songItems", QJsonArray{QJsonObject{
                              {"songId", 42},
                              {"songName", "First"},
                              {"albumName", "album"},
                              {"picUrl", "http://p1.music.126.net/cover.jpg"},
                              {"playCount", 17},
                              {"artists", QJsonArray{
                                   QJsonObject{{"artistId", 7}, {"artistName", "Alice"}},
                                   QJsonObject{{"artistId", 8}, {"artistName", "Bob"}}}}}}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("listen:week"));
        m_store->request(QStringLiteral("listenRank:month"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("listenRealtime").toMap().value("type").toString()
                       == QStringLiteral("week")
                && m_store->data().value("listenRank").toMap().value("type").toString()
                       == QStringLiteral("month");
        }));

        const QVariantMap realtime = m_store->data().value("listenRealtime").toMap();
        QCOMPARE(realtime.keys(), QStringList({QStringLiteral("endTime"),
                                               QStringLiteral("listenTimeDistribution"),
                                               QStringLiteral("startTime"),
                                               QStringLiteral("type"),
                                               QStringLiteral("userNickName"),
                                               QStringLiteral("weekFriends"),
                                               QStringLiteral("weekToday")}));
        QCOMPARE(realtime.value("type").toString(), QStringLiteral("week"));
        QCOMPARE(realtime.value("startTime").toDouble(), 1750000000000.0);
        QCOMPARE(realtime.value("endTime").toDouble(), 1750604800000.0);
        QCOMPARE(realtime.value("userNickName").toString(), QStringLiteral("Listener"));
        QCOMPARE(realtime.value("listenTimeDistribution").toJsonObject(), distribution);
        QCOMPARE(realtime.value("weekFriends").toJsonObject(), friends);
        QCOMPARE(realtime.value("weekToday").toJsonObject(), today);

        const QVariantMap rank = m_store->data().value("listenRank").toMap();
        QCOMPARE(rank.keys(), QStringList({QStringLiteral("endTime"), QStringLiteral("items"),
                                           QStringLiteral("songCount"),
                                           QStringLiteral("startTime"),
                                           QStringLiteral("type")}));
        QCOMPARE(rank.value("type").toString(), QStringLiteral("month"));
        QCOMPARE(rank.value("startTime").toDouble(), 1750000000000.0);
        QCOMPARE(rank.value("endTime").toDouble(), 1750604800000.0);
        QCOMPARE(rank.value("songCount").toDouble(), 1.0);
        const QVariantList items = rank.value("items").toList();
        QCOMPARE(items.size(), 1);
        const QVariantMap song = items.first().toMap();
        QCOMPARE(song.keys(), QStringList({QStringLiteral("album"), QStringLiteral("artists"),
                                           QStringLiteral("cover"), QStringLiteral("id"),
                                           QStringLiteral("name"),
                                           QStringLiteral("playCount")}));
        QCOMPARE(song.value("id").toDouble(), 42.0);
        QCOMPARE(song.value("name").toString(), QStringLiteral("First"));
        QCOMPARE(song.value("album").toString(), QStringLiteral("album"));
        QCOMPARE(song.value("cover").toString(),
                 QStringLiteral("http://p1.music.126.net/cover.jpg?param=120y120"));
        QCOMPARE(song.value("playCount").toDouble(), 17.0);
        const QVariantList artists = song.value("artists").toList();
        QCOMPARE(artists.size(), 2);
        QCOMPARE(artists.at(0).toMap(),
                 QVariantMap({{"id", 7.0}, {"name", QStringLiteral("Alice")}}));
        QCOMPARE(artists.at(1).toMap(),
                 QVariantMap({{"id", 8.0}, {"name", QStringLiteral("Bob")}}));
    }

    // 结构不符的听歌足迹回包按对应请求 key 报一句用户能看懂的话，不写半份数据
    void malformedListenStatsReportUserFacingFailures()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/content/activity/listen/data/total") {
                respond(200, "{\"code\":200,\"data\":{}}");
                return;
            }
            if (req.path == "/api/content/activity/listen/data/realtime/report") {
                respond(200, "{\"code\":200,\"data\":{\"startTime\":\"yesterday\"}}");
                return;
            }
            if (req.path == "/api/content/activity/listen/data/song/play/rank") {
                respond(200, "{\"code\":200,\"data\":{\"songItems\":\"nope\"}}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        QSignalSpy failed(m_store, &Store::requestFailed);
        m_store->request(QStringLiteral("listen:total"));
        m_store->request(QStringLiteral("listen:month"));
        m_store->request(QStringLiteral("listenRank:month"));
        QVERIFY(waitUntil([&] { return failed.count() == 3; }));

        QHash<QString, QString> messages;
        for (const QList<QVariant> &signal : failed)
            messages.insert(signal.at(0).toString(), signal.at(1).toString());
        QCOMPARE(messages.value(QStringLiteral("listen:total")),
                 QStringLiteral("暂时取不到听歌统计"));
        QCOMPARE(messages.value(QStringLiteral("listen:month")),
                 QStringLiteral("暂时取不到听歌统计"));
        QCOMPARE(messages.value(QStringLiteral("listenRank:month")),
                 QStringLiteral("暂时取不到播放排行"));
        QVERIFY(m_store->data().value("listenTotal").isNull());
        QVERIFY(m_store->data().value("listenRealtime").isNull());
        QVERIFY(m_store->data().value("listenRank").isNull());
    }

    // MV 播放地址是签名 URL，过期即失效：同一 MV 再次打开必须重新取地址；详情仍用缓存
    void reopenMvFetchesSignedUrlAgain()
    {
        int detailRequests = 0;
        int urlRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/mv/detail") {
                ++detailRequests;
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"subed", false},
                    {"data", QJsonObject{
                        {"id", 7}, {"name", "MV"}, {"cover", "http://cover"},
                        {"artistName", "A"}, {"artistId", 9}, {"duration", 120000},
                        {"playCount", 1}, {"subCount", 2}, {"shareCount", 3},
                        {"commentCount", 4}, {"publishTime", "2020-01-01"}, {"desc", "d"}}}}));
                return;
            }
            if (req.path == "/weapi/song/enhance/play/mv/url") {
                ++urlRequests;
                // expi 是秒：第一份地址 1 秒后过期，第二次打开必须拿不到它
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"data", QJsonObject{
                        {"code", 200},
                        {"url", QStringLiteral("http://media/%1").arg(urlRequests)},
                        {"r", 1080}, {"size", 100}, {"expi", 1}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto url = [this] {
            return m_store->data().value("mv").toMap().value("7").toMap().value("url").toString();
        };

        const auto urlPending = [this] {
            return m_store->data().value("mv").toMap().value("7").toMap().value("urlPending").toBool();
        };

        m_store->request(QStringLiteral("mv:7"));
        QVERIFY(waitUntil([&] { return url() == QStringLiteral("http://media/1"); }));
        QCOMPARE(urlPending(), false);
        QCOMPARE(detailRequests, 1);
        QCOMPARE(urlRequests, 1);

        QTest::qWait(1100);   // 越过第一份地址的 expi

        // 再打开：旧地址立刻清掉，并标明新地址在取（界面不能把它当成「没有可播放的地址」）
        m_store->request(QStringLiteral("mv:7"));
        QCOMPARE(url(), QString());
        QCOMPARE(urlPending(), true);
        QVERIFY(waitUntil([&] { return url() == QStringLiteral("http://media/2"); }));
        QCOMPARE(urlPending(), false);
        QCOMPARE(detailRequests, 1);   // 详情仍是缓存里的那份
        QCOMPARE(urlRequests, 2);
    }

    // 按需请求失败必须发 requestFailed，页面才能从「加载中」切到「重试」
    void failedOnDemandRequestNotifies()
    {
        m_api->handler = [](const FakeApi::Request &, const FakeApi::Responder &respond) {
            respond(500, "{\"code\":500}");
        };
        QSignalSpy failed(m_store, &Store::requestFailed);
        m_store->request(QStringLiteral("playlist:999"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("playlist:999"));
    }

    // 楼中楼失败按完整请求 key 报错，界面才能定位到具体评论并重试
    void commentRepliesFailureUsesFullKey()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/resource/comment/floor/get") {
                respond(200, "{\"code\":500}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::requestFailed);
        m_store->request(QStringLiteral("commentReplies:9:500"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("commentReplies:9:500"));
    }

    // 301 不只出现在红心/评论：任何个人写路径都必须置 sessionExpired 并给统一的过期句子
    void playlistWrite301SetsSessionExpired()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/playlist/create") {
                respond(200, "{\"code\":301,\"message\":\"需要登录\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::playlistActionFailed);
        QSignalSpy expired(m_store, &Store::sessionExpiredChanged);
        QCOMPARE(m_store->sessionExpired(), false);

        m_store->createPlaylist(QStringLiteral("travel"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(m_store->sessionExpired(), true);
        QCOMPARE(expired.count(), 1);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("创建歌单"));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("登录已过期，请重新登录"));
    }

    // 风控/限流码（-460 与 405）走同一句话，且不能被当成断网或会话过期
    void rateLimitedWriteHasItsOwnSentence()
    {
        int replies = 0;
        m_api->handler = [&replies](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/playlist/create") {
                respond(200, replies++ == 0 ? "{\"code\":-460,\"message\":\"请求过于频繁\"}"
                                            : "{\"code\":405}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::playlistActionFailed);

        m_store->createPlaylist(QStringLiteral("a"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("操作太频繁，请稍后再试"));

        m_store->createPlaylist(QStringLiteral("b"));
        QVERIFY(waitUntil([&] { return failed.count() == 2; }));
        QCOMPARE(failed.at(1).at(1).toString(), QStringLiteral("操作太频繁，请稍后再试"));
        QCOMPARE(m_store->sessionExpired(), false);
    }

    // 连接被断开（没有 HTTP 状态码）才是「网络不可用」；服务器回了 HTTP 错误不算断网
    void droppedConnectionIsOffline()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/playlist/create") {
                respond(0, {});   // 不回包，直接断开
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::playlistActionFailed);

        m_store->createPlaylist(QStringLiteral("travel"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("网络不可用，请稍后再试"));
        QCOMPARE(m_store->sessionExpired(), false);
        QCOMPARE(m_store->online(), false);
    }

    // 歌单详情与补取的歌曲详情都把 privileges 放在歌曲旁边的平行数组里：能不能播以它为准，
    // 不能只看 fee（fee=1 的歌对会员账号可以播）
    void playlistTracksUseParallelPrivileges()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v6/playlist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{
                        {"id", 601}, {"name", "P"}, {"trackCount", 2},
                        {"tracks", QJsonArray{QJsonObject{{"id", 7}, {"name", "a"}, {"fee", 1}}}},
                        {"trackIds", QJsonArray{QJsonObject{{"id", 7}}, QJsonObject{{"id", 8}}}}}},
                    {"privileges", QJsonArray{QJsonObject{{"id", 7}, {"fee", 1}, {"pl", 320000}, {"st", 0}}}}}));
                return;
            }
            if (req.path == "/api/v3/song/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"songs", QJsonArray{QJsonObject{{"id", 8}, {"name", "b"}, {"fee", 1}}}},
                    {"privileges", QJsonArray{QJsonObject{{"id", 8}, {"fee", 1}, {"pl", 0}, {"st", 0}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("playlist:601"));
        QVERIFY(waitUntil([&] {
            const QVariantMap detail = m_store->personal().value("playlistDetails").toMap()
                                           .value(QStringLiteral("601")).toMap();
            return detail.value("tracks").toList().size() == 2;
        }));
        const QVariantList tracks = m_store->personal().value("playlistDetails").toMap()
                                        .value(QStringLiteral("601")).toMap().value("tracks").toList();
        QCOMPARE(tracks.at(0).toMap().value("playable").toBool(), true);
        QCOMPARE(tracks.at(1).toMap().value("playable").toBool(), false);
        QCOMPARE(tracks.at(1).toMap().value("blockedBy").toString(), QStringLiteral("vip"));
    }

    // 拒绝的原因只有网易云知道：用它的 message；没有 message 时才用通用的一句
    void rejectionUsesServerMessage()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/playlist/create") {
                respond(200, "{\"code\":500,\"message\":\"歌单名称重复\"}");
                return;
            }
            if (req.path == "/weapi/playlist/remove") {
                respond(200, "{\"code\":500}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy failed(m_store, &Store::playlistActionFailed);

        m_store->createPlaylist(QStringLiteral("travel"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("歌单名称重复"));
        QCOMPARE(m_store->sessionExpired(), false);

        m_store->deletePlaylist(42);
        QVERIFY(waitUntil([&] { return failed.count() == 2; }));
        QCOMPARE(failed.at(1).at(1).toString(), QStringLiteral("网易云没有接受这次操作"));
    }

    // 逐字歌词与普通歌词一样是长缓存：同一首歌第二次打开只命中缓存
    void lyricsNewIsCachedForever()
    {
        int requests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric/v1") {
                ++requests;
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"lrc", QJsonObject{{"lyric", "[00:01.00]hello"}}},
                }));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyricsNew:8"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("lyricsNew").toMap().value("songId").toDouble() == 8;
        }));
        m_store->request(QStringLiteral("lyricsNew:8"));
        QTest::qWait(150);
        QCOMPARE(requests, 1);
    }

    // 逐字歌词：yrc 有内容时 lines 一次解析好（带 words），原始 yrc 不再留；失败 key 带 songId
    void lyricsNewParsesWordTimings()
    {
        bool fail = false;
        m_api->handler = [&fail](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/song/lyric/v1") {
                if (fail) {
                    respond(500, "{\"code\":500}");
                    return;
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"lrc", QJsonObject{{"lyric", "[00:01.00]hello"}}},
                    // 实测：tlyric 对的是 LRC 的行，逐字歌词的译文在 ytlrc（时间与 yrc 的行对齐）
                    {"tlyric", QJsonObject{{"lyric", "[00:01.00]LRC 的译文"}}},
                    {"ytlrc", QJsonObject{{"lyric", "[00:01.00]你好"}}},
                    {"yrc", QJsonObject{{"lyric", QStringLiteral(
                        "{\"t\":0,\"c\":[{\"tx\":\"作词: \"}]}\n"
                        "[1000,1200](1000,200,0)Hel(1200,300,0)lo (1500,700,0)world\n")}}},
                }));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("lyricsNew:5"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("lyricsNew").toMap().value("lines").toList().size() == 1;
        }));
        const QVariantMap lyrics = m_store->data().value("lyricsNew").toMap();
        QCOMPARE(lyrics.value("songId").toDouble(), 5.0);
        QVERIFY(!lyrics.contains(QStringLiteral("yrc")));
        const QVariantMap line = lyrics.value("lines").toList().first().toMap();
        QCOMPARE(line.value("t").toDouble(), 1.0);
        QCOMPARE(line.value("text").toString(), QStringLiteral("Hello world"));
        QCOMPARE(line.value("tr").toString(), QStringLiteral("你好"));
        const QVariantList words = line.value("words").toList();
        QCOMPARE(words.size(), 3);
        QCOMPARE(words.at(1).toMap().value("t").toDouble(), 1.2);
        QCOMPARE(words.at(1).toMap().value("d").toDouble(), 0.3);
        QCOMPARE(words.at(2).toMap().value("text").toString(), QStringLiteral("world"));

        fail = true;
        QSignalSpy failed(m_store, &Store::requestFailed);
        m_store->request(QStringLiteral("lyricsNew:6"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("lyricsNew:6"));
    }

    // 离线：缓存里的歌单、专辑、歌手在接口拒绝连接时照常交付。新鲜期内根本不发请求；
    // 过期后先交付旧值，取数失败只发 requestFailed，不清掉已交付的数据。
    void offlineCacheServesPlaylistAlbumArtist()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v6/playlist/detail") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"playlist", QJsonObject{
                        {"id", 601}, {"name", "P"}, {"coverImgUrl", ""}, {"description", ""},
                        {"trackCount", 1},
                        {"tracks", QJsonArray{QJsonObject{{"id", 7}, {"name", "song"},
                                                          {"ar", QJsonArray{}}, {"al", QJsonObject{}},
                                                          {"dt", 1000}}}},
                        {"trackIds", QJsonArray{QJsonObject{{"id", 7}}}}}}}));
                return;
            }
            if (req.path == "/api/v1/album/123") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"album", QJsonObject{
                        {"id", 123}, {"name", "A"},
                        {"artists", QJsonArray{QJsonObject{{"id", 9}, {"name", "X"}}}},
                        {"picUrl", ""}, {"description", ""}, {"company", ""},
                        {"isSub", false}, {"publishTime", 0}}},
                    {"songs", QJsonArray{QJsonObject{{"id", 8}, {"name", "b"},
                                                     {"ar", QJsonArray{}}, {"al", QJsonObject{}},
                                                     {"dt", 1000}}}}}));
                return;
            }
            if (req.path == "/api/v1/artist/321") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"artist", QJsonObject{
                        {"id", 321}, {"name", "Ar"}, {"alias", QJsonArray{}},
                        {"picUrl", ""}, {"briefDesc", ""}, {"followed", false},
                        {"musicSize", 1}, {"albumSize", 0}}},
                    {"hotSongs", QJsonArray{QJsonObject{{"id", 9}, {"name", "h"},
                                                        {"ar", QJsonArray{}}, {"al", QJsonObject{}},
                                                        {"dt", 1000}}}}}));
                return;
            }
            if (req.path == "/api/artist/albums/321") {
                respond(200, json(QJsonObject{{"code", 200}, {"hotAlbums", QJsonArray{}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        const auto cachedValues = [this] {
            return m_store->personal().value("playlistDetails").toMap()
                       .value(QStringLiteral("601")).toMap().value("name").toString()
                       == QLatin1String("P")
                && m_store->data().value("album").toMap().value("123").toMap().value("name").toString()
                       == QLatin1String("A")
                && m_store->data().value("artist").toMap().value("321").toMap().value("name").toString()
                       == QLatin1String("Ar");
        };

        m_store->request(QStringLiteral("playlist:601"));
        m_store->request(QStringLiteral("album:123"));
        m_store->request(QStringLiteral("artist:321"));
        QVERIFY(waitUntil(cachedValues));

        // 直接把落盘条目的生成时间改老：公开接口只会写「现在」
        const QString cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                                  + QStringLiteral("/queries.db");
        const auto ageCache = [&cachePath](qint64 seconds) {
            bool ok = false;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                             QStringLiteral("store-test-age"));
                db.setDatabaseName(cachePath);
                if (db.open()) {
                    QSqlQuery query(db);
                    query.prepare(QStringLiteral("UPDATE entries SET fetched_at = ?"));
                    query.addBindValue(QDateTime::currentSecsSinceEpoch() - seconds);
                    ok = query.exec();
                }
            }
            QSqlDatabase::removeDatabase(QStringLiteral("store-test-age"));
            return ok;
        };
        const quint16 port = m_api->serverPort();
        const auto reopenStore = [this, port] {
            delete m_store;
            m_store = nullptr;
            delete m_client;
            m_client = nullptr;

            m_client = new NcmClient(nullptr, false);
            m_client->setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(port));
            m_client->adoptCookies(QJsonDocument(QJsonArray{
                QJsonObject{{"MUSIC_U", "test"}, {"__csrf", "csrf"}}}));
            m_store = new Store(m_client);
        };

        // 新鲜期内不发请求。接口对每个连接都不回包、直接断开（连接层拒绝）
        const auto refuse = [](const FakeApi::Request &, const FakeApi::Responder &respond) {
            respond(0, {});
        };
        m_api->requests.clear();
        m_api->handler = refuse;
        delete m_store;
        m_store = nullptr;
        delete m_client;
        m_client = nullptr;
        QVERIFY(ageCache(5 * 60));   // 5 分钟：三种 key 都还在新鲜期内（歌单的 10 分钟是最短的）

        reopenStore();
        {
            QSignalSpy failed(m_store, &Store::requestFailed);
            m_store->request(QStringLiteral("playlist:601"));
            m_store->request(QStringLiteral("album:123"));
            m_store->request(QStringLiteral("artist:321"));
            QVERIFY(cachedValues());
            QTest::qWait(100);
            QCOMPARE(m_api->requests.size(), 0);   // 缓存新鲜：断网也不发请求
            QCOMPARE(failed.count(), 0);
        }

        // 31 天后全部过期：旧值仍然先交付，失败不覆盖
        delete m_store;
        m_store = nullptr;
        delete m_client;
        m_client = nullptr;
        QVERIFY(ageCache(31 * 24 * 60 * 60));
        m_api->close();   // 监听也关掉：连接直接被拒

        reopenStore();
        QSignalSpy failed(m_store, &Store::requestFailed);
        m_store->request(QStringLiteral("playlist:601"));
        m_store->request(QStringLiteral("album:123"));
        m_store->request(QStringLiteral("artist:321"));
        QVERIFY(cachedValues());            // 先给缓存
        QVERIFY(waitUntil([&] { return failed.count() == 3; }, 15000));
        QVERIFY(cachedValues());            // 三个请求都失败，旧值原样保留
    }
    // 百科：完整 block + 创作者 + 副歌时间合并成一个可直接渲染的形状
    void wikiCombinesBlocksCreatorsAndChorus()
    {
        QJsonArray songBlocks;
        songBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockSongInfoVo"},
            {"blockInfo", QJsonObject{{"desc", "一首动画片头曲。"}}}});
        songBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockBaseInfoVo"},
            {"blockInfo", QJsonObject{{"wikiSubElementVos", QJsonArray{
                QJsonObject{{"title", "语种"}, {"content", "日语"}},
                QJsonObject{{"title", "曲风"},
                            {"wikiSubMetaVos", QJsonArray{
                                QJsonObject{{"text", "原声带"}},
                                QJsonObject{{"text", "动画"}}}}},
            }}}}});
        songBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockMvInfoVo"},
            {"blockInfo", QJsonObject{{"wikiSubElementVos", QJsonArray{
                QJsonObject{{"title", "ALIVE"}, {"artists", "ClariS"},
                            {"imgUrl", "http://mv"}, {"duration", 221000},
                            {"publishTime", "2022-08-03"}, {"playNum", 2750},
                            {"jumpUrl", "orpheus://mv/22621587"}},
            }}}}});
        songBlocks.append(QJsonObject{
            {"blockCode", "wikiSubBlockSongCommentVo"},
            {"blockInfo", QJsonObject{{"wikiSubElementVos", QJsonArray{
                QJsonObject{{"content", "好听"}, {"commenter", "某人"},
                            {"likes", 7}, {"typeName", "精选乐评"}},
            }}}}});

        QJsonObject songDetail{
            {"songName", "ALIVE"}, {"albumName", "ALIVE"}, {"artistName", "ClariS"},
            {"coverImage", "http://cover"}, {"blocks", songBlocks}};
        QJsonObject memoryRn{
            {"moduleTitle", "回忆坐标"},
            {"firstListen", QJsonObject{{"period", "深夜"}, {"season", "仲夏"},
                                         {"time", "01:25"}, {"timestamp", 1657387559000.0}}},
            {"totalPlay", QJsonObject{{"playCount", 1887}, {"text", "听到天荒地老"}}}};
        QJsonArray topBlocks{
            QJsonObject{{"rnData", memoryRn}},
            QJsonObject{{"rnData", songDetail}}};
        const QJsonObject wikiRoot{
            {"code", 200},
            {"data", QJsonObject{{"blocks", topBlocks}}}};

        QJsonObject creatorRole{
            {"roleName", "作词"},
            {"creatorMetaVOS", QJsonArray{
                QJsonObject{{"artistId", 42}, {"artistName", "重永亮介"},
                            {"artistPic", "http://creator"}, {"isSub", false}}}}};
        const QJsonObject creatorsRoot{
            {"code", 200},
            {"data", QJsonObject{{"songCreatorsRoleVos", QJsonArray{creatorRole}}}}};
        const QJsonObject chorusRoot{
            {"code", 200},
            {"chorus", QJsonArray{QJsonObject{{"id", 9}, {"startTime", 42402},
                                               {"endTime", 80866}}}}};

        const QByteArray wikiJson = json(wikiRoot);
        const QByteArray creatorsJson = json(creatorsRoot);
        const QByteArray chorusJson = json(chorusRoot);
        bool wikiPersonal = false;
        bool creatorsPersonal = false;
        bool chorusPersonal = false;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/link/page/parent/relation/construct/info") {
                wikiPersonal = req.headers.contains("MUSIC_U=test");
                respond(200, wikiJson);
                return;
            }
            if (req.path == "/api/song/creators") {
                creatorsPersonal = req.headers.contains("MUSIC_U=test");
                respond(200, creatorsJson);
                return;
            }
            if (req.path == "/api/song/chorus") {
                chorusPersonal = req.headers.contains("MUSIC_U=test");
                respond(200, chorusJson);
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("wiki:9"));
        QVERIFY(waitUntil([&] {
            const QVariantMap wiki = m_store->data().value("wiki").toMap().value("9").toMap();
            return wiki.value("songId").toDouble() == 9 && wiki.value("loaded").toBool();
        }));
        const QVariantMap wiki = m_store->data().value("wiki").toMap().value("9").toMap();
        QCOMPARE(wiki.value("songName").toString(), QStringLiteral("ALIVE"));
        QCOMPARE(wiki.value("description").toString(), QStringLiteral("一首动画片头曲。"));
        QCOMPARE(wiki.value("basic").toList().size(), 2);
        QCOMPARE(wiki.value("creators").toList().size(), 1);
        QCOMPARE(wiki.value("mvs").toList().at(0).toMap().value("id").toDouble(), 22621587.0);
        QCOMPARE(wiki.value("reviews").toList().at(0).toMap().value("content").toString(),
                 QStringLiteral("好听"));
        QCOMPARE(wiki.value("memory").toMap().value("period").toString(), QStringLiteral("深夜"));
        QCOMPARE(wiki.value("chorus").toMap().value("start").toDouble(), 42.402);
        QCOMPARE(wiki.value("empty").toBool(), false);
        QVERIFY(wikiPersonal);
        QVERIFY(creatorsPersonal);
        QVERIFY(chorusPersonal);
    }

    // 搜索联想：最新查询赢；空白查询不发请求；联想不进磁盘缓存
    void searchSuggestLatestWinsAndStaysOffDisk()
    {
        QHash<QString, FakeApi::Responder> pending;
        int requests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/search/suggest/web") {
                ++requests;
                const QUrlQuery form(QString::fromUtf8(req.body));
                pending.insert(form.queryItemValue(QStringLiteral("s"), QUrl::FullyDecoded),
                               respond);
                return;
            }
            respond(200, "{\"code\":200}");
        };
        const auto reply = [](int id) {
            return json(QJsonObject{
                {"code", 200},
                {"result", QJsonObject{
                    {"order", QJsonArray{QStringLiteral("songs"), QStringLiteral("artists"),
                                         QStringLiteral("albums"), QStringLiteral("playlists")}},
                    {"songs", QJsonArray{QJsonObject{
                        {"id", id}, {"name", "歌"},
                        {"artists", QJsonArray{QJsonObject{{"id", 11}, {"name", "歌手"}}}},
                        {"album", QJsonObject{{"id", 12}, {"name", "专辑"}}}}}},
                    {"artists", QJsonArray{QJsonObject{
                        {"id", 21}, {"name", "艺人"}, {"picUrl", "http://cover/artist.jpg"}}}},
                    {"albums", QJsonArray{QJsonObject{
                        {"id", 31}, {"name", "碟"},
                        {"artist", QJsonObject{{"id", 21}, {"name", "艺人"}}}}}},
                    {"playlists", QJsonArray{QJsonObject{
                        {"id", 41}, {"name", "单"}, {"coverImgUrl", "http://cover/playlist.jpg"},
                        {"trackCount", 7}}}}}}});
        };

        m_store->request(QStringLiteral("searchSuggest:first"));
        QVERIFY(waitUntil([&] { return pending.contains(QStringLiteral("first")); }));
        m_store->request(QStringLiteral("searchSuggest:second"));
        QVERIFY(waitUntil([&] { return pending.contains(QStringLiteral("second")); }));

        // 新查询先回、旧查询后回：旧结果必须被请求代号丢弃
        pending.value(QStringLiteral("second"))(200, reply(2));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("searchSuggest").toMap().value("query").toString()
                == QStringLiteral("second");
        }));
        pending.value(QStringLiteral("first"))(200, reply(1));
        QTest::qWait(150);

        const QVariantMap suggest = m_store->data().value("searchSuggest").toMap();
        QCOMPARE(suggest.value("query").toString(), QStringLiteral("second"));
        QCOMPARE(suggest.value("order").toList().size(), 4);
        const QVariantMap song = suggest.value("songs").toList().first().toMap();
        QCOMPARE(song.value("id").toDouble(), 2.0);
        QCOMPARE(song.value("artist").toString(), QStringLiteral("歌手"));
        QCOMPARE(song.value("album").toString(), QStringLiteral("专辑"));
        QCOMPARE(suggest.value("artists").toList().first().toMap().value("cover").toString(),
                 QStringLiteral("http://cover/artist.jpg"));
        QCOMPARE(suggest.value("albums").toList().first().toMap().value("artistId").toDouble(), 21.0);
        QCOMPARE(suggest.value("playlists").toList().first().toMap().value("count").toDouble(), 7.0);

        const int before = requests;
        m_store->request(QStringLiteral("searchSuggest:   "));
        m_store->request(QStringLiteral("searchSuggest:"));
        QTest::qWait(100);
        QCOMPARE(requests, before);

        // key 的第一段之后全是用户原文：查询里的冒号不能再被拆掉
        m_store->request(QStringLiteral("searchSuggest:colon:query"));
        QVERIFY(waitUntil([&] { return pending.contains(QStringLiteral("colon:query")); }));
        pending.value(QStringLiteral("colon:query"))(200, reply(9));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("searchSuggest").toMap().value("query").toString()
                == QStringLiteral("colon:query");
        }));

        // 搜索联想不落盘：重建 Store（只共享 SQLite 缓存）后同一查询必须重新发请求
        const int beforeReopen = requests;
        delete m_store;
        m_store = new Store(m_client);
        m_store->request(QStringLiteral("searchSuggest:second"));
        QVERIFY(waitUntil([&] { return requests == beforeReopen + 1; }));
        QVERIFY(pending.contains(QStringLiteral("second")));
        pending.value(QStringLiteral("second"))(200, reply(5));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("searchSuggest").toMap()
                       .value("songs").toList().value(0).toMap().value("id").toDouble() == 5.0;
        }));
    }

    // 新歌直接进现有队列：字段与 ncmTrack 同形，封面保持原图
    void discoverNewSongsUseQueueTrackShape()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/personalized/newsong") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"result", QJsonArray{QJsonObject{
                        {"id", 1}, {"picUrl", "http://cover/fallback.jpg"},
                        {"song", QJsonObject{
                            {"id", 101}, {"name", "新歌"}, {"duration", 210500},
                            {"fee", 1}, {"alia", QJsonArray{QStringLiteral("别名")}},
                            {"sq", true}, {"mv", 77},
                            {"artists", QJsonArray{
                                QJsonObject{{"id", 3}, {"name", "歌手"}},
                                QJsonObject{{"id", 4}, {"name", "合作"}}}},
                            {"album", QJsonObject{{"id", 5}, {"name", "专辑"},
                                                  {"picUrl", "http://cover/new.jpg"}}}}}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        m_store->request(QStringLiteral("discover:newSongs"));
        QVERIFY(waitUntil([&] { return m_store->data().value("newSongs").toList().size() == 1; }));
        const QVariantMap track = m_store->data().value("newSongs").toList().first().toMap();
        QCOMPARE(track.value("id").toDouble(), 101.0);
        QCOMPARE(track.value("artist").toString(), QStringLiteral("歌手 / 合作"));
        QCOMPARE(track.value("artistId").toDouble(), 3.0);
        QCOMPARE(track.value("artists").toList().at(1).toMap().value("id").toDouble(), 4.0);
        QCOMPARE(track.value("albumId").toDouble(), 5.0);
        QCOMPARE(track.value("album").toString(), QStringLiteral("专辑"));
        QCOMPARE(track.value("dur").toDouble(), 210.0);
        QCOMPARE(track.value("cover").toString(), QStringLiteral("http://cover/new.jpg"));
        QCOMPARE(track.value("alias").toString(), QStringLiteral("别名"));
        QCOMPARE(track.value("quality").toString(), QStringLiteral("lossless"));
        QCOMPARE(track.value("mv").toDouble(), 77.0);
        QCOMPARE(track.value("playable").toBool(), false);
        QCOMPARE(track.value("blockedBy").toString(), QStringLiteral("vip"));
        QVERIFY(track.contains("reason"));
    }

    void discoverNewAlbumsTagsAndMvs()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/album/new") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"total", 100},
                    {"albums", QJsonArray{QJsonObject{
                        {"id", 2}, {"name", "新碟"}, {"picUrl", "http://cover/album.jpg"},
                        {"publishTime", 1700000000000.0},
                        {"artist", QJsonObject{{"id", 8}, {"name", "主歌手"}}},
                        {"size", 10}}}}}));
                return;
            }
            if (req.path == "/api/playlist/highquality/tags") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"tags", QJsonArray{QJsonObject{
                        {"id", 1}, {"name", "华语"}, {"type", 0}, {"category", 0},
                        {"hot", true}}}}}));
                return;
            }
            if (req.path == "/api/personalized/mv") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"result", QJsonArray{QJsonObject{
                        {"id", 55}, {"type", 1}, {"name", "MV"}, {"copywriter", "推荐语"},
                        {"picUrl", "http://cover/mv.jpg"}, {"duration", 214000.0},
                        {"playCount", 99.0}, {"artistId", 8}, {"artistName", "主歌手"}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("discover:newAlbums"));
        m_store->request(QStringLiteral("discover:highqualityTags"));
        m_store->request(QStringLiteral("discover:mvs"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("newAlbums").toList().size() == 1
                && m_store->data().value("highqualityTags").toList().size() == 1
                && m_store->data().value("recommendedMvs").toList().size() == 1;
        }));

        const QVariantMap album = m_store->data().value("newAlbums").toList().first().toMap();
        QCOMPARE(album.value("id").toDouble(), 2.0);
        QCOMPARE(album.value("artist").toString(), QStringLiteral("主歌手"));
        QCOMPARE(album.value("artistId").toDouble(), 8.0);
        QCOMPARE(album.value("publishTime").toDouble(), 1700000000000.0);
        QCOMPARE(album.value("size").toDouble(), 10.0);

        const QVariantMap tag = m_store->data().value("highqualityTags").toList().first().toMap();
        QCOMPARE(tag.value("name").toString(), QStringLiteral("华语"));
        QCOMPARE(tag.value("hot").toBool(), true);

        const QVariantMap mv = m_store->data().value("recommendedMvs").toList().first().toMap();
        QCOMPARE(mv.value("id").toDouble(), 55.0);
        QCOMPARE(mv.value("artist").toString(), QStringLiteral("主歌手"));
        QCOMPARE(mv.value("artistId").toDouble(), 8.0);
        QCOMPARE(mv.value("duration").toDouble(), 214.0);
        QCOMPARE(mv.value("playCount").toDouble(), 99.0);
        QCOMPARE(mv.value("reason").toString(), QStringLiteral("推荐语"));
        QCOMPARE(mv.value("cover").toString(), QStringLiteral("http://cover/mv.jpg"));
    }

    // 精品歌单翻页沿用第一页的请求代号：旧页晚到不能覆盖新页
    void highqualityPagingKeepsFirstPageToken()
    {
        QHash<QString, FakeApi::Responder> pending;
        QStringList cats;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/playlist/highquality/list") {
                const QUrlQuery form(QString::fromUtf8(req.body));
                const QString cat = form.queryItemValue(QStringLiteral("cat"));
                pending.insert(cat + u':' + form.queryItemValue(QStringLiteral("lasttime")),
                               respond);
                cats.append(cat);
                return;
            }
            respond(200, "{\"code\":200}");
        };
        const auto page = [](int id, double next) {
            return json(QJsonObject{
                {"code", 200}, {"more", true}, {"lasttime", next},
                {"playlists", QJsonArray{QJsonObject{
                    {"id", id}, {"name", "精品"}, {"coverImgUrl", "http://cover/hq.jpg"},
                    {"description", "简介"}, {"trackCount", 12}, {"playCount", 34},
                    {"creator", QJsonObject{{"userId", 9}, {"nickname", "歌单主"}}}}}}});
        };

        // 第一页的 next 是下一页的游标；第二页沿用第一页的请求代号，条目接在第一页后面
        m_store->request(QStringLiteral("discover:highquality:全部"));
        QVERIFY(waitUntil([&] {
            return pending.contains(QStringLiteral("全部:0"));
        }));
        pending.value(QStringLiteral("全部:0"))(200, page(5, 777));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("highquality").toMap().value("next").toDouble() == 777.0;
        }));
        m_store->request(QStringLiteral("discover:highquality:全部:777"));
        QVERIFY(waitUntil([&] {
            return pending.contains(QStringLiteral("全部:777"));
        }));
        pending.value(QStringLiteral("全部:777"))(200, page(6, 888));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("highquality").toMap().value("next").toDouble() == 888.0;
        }));
        const QVariantMap paged = m_store->data().value("highquality").toMap();
        QCOMPARE(paged.value("cat").toString(), QStringLiteral("全部"));
        QCOMPARE(paged.value("more").toBool(), true);
        const QVariantList pagedItems = paged.value("items").toList();
        QCOMPARE(pagedItems.size(), 2);
        QCOMPARE(pagedItems.at(0).toMap().value("id").toDouble(), 5.0);
        const QVariantMap pagedItem = pagedItems.at(1).toMap();
        QCOMPARE(pagedItem.value("id").toDouble(), 6.0);
        // 同一页再送来一次（缓存命中）：不重复接
        m_store->request(QStringLiteral("discover:highquality:全部:777"));
        QTest::qWait(150);
        QCOMPARE(m_store->data().value("highquality").toMap().value("items").toList().size(), 2);
        QCOMPARE(pagedItem.value("count").toDouble(), 12.0);
        QCOMPARE(pagedItem.value("plays").toDouble(), 34.0);
        QCOMPARE(pagedItem.value("creator").toString(), QStringLiteral("歌单主"));

        // 换分类是新的一份列表：整槽换掉，不接在旧分类后面
        m_store->request(QStringLiteral("discover:highquality:华语"));
        QVERIFY(waitUntil([&] {
            return pending.contains(QStringLiteral("华语:0"));
        }));
        pending.value(QStringLiteral("华语:0"))(200, page(7, 1000));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("highquality").toMap().value("cat").toString()
                == QStringLiteral("华语");
        }));

        const QVariantMap switched = m_store->data().value("highquality").toMap();
        QCOMPARE(switched.value("next").toDouble(), 1000.0);
        QCOMPARE(switched.value("items").toList().size(), 1);
        QCOMPARE(switched.value("items").toList().first().toMap().value("id").toDouble(), 7.0);
        QCOMPARE(cats.size(), 3);
        QCOMPARE(cats.at(0), QStringLiteral("全部"));
        QCOMPARE(cats.at(1), QStringLiteral("全部"));
        QCOMPARE(cats.at(2), QStringLiteral("华语"));
    }

    // 电台详情 + 一页节目；翻页复用槽里的详情，不再请求详情接口
    void radioDetailThenProgramPagingReusesDetail()
    {
        int detailRequests = 0;
        QHash<QString, FakeApi::Responder> pending;
        QStringList asc;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/djradio/v2/get") {
                ++detailRequests;
                respond(200, json(QJsonObject{{"code", 200}, {"data", radioObject()}}));
                return;
            }
            if (req.path == "/api/dj/program/byradio") {
                const QUrlQuery form(QString::fromUtf8(req.body));
                pending.insert(form.queryItemValue(QStringLiteral("offset")), respond);
                asc.append(form.queryItemValue(QStringLiteral("asc")));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        // 每页两个节目，其中一个没有主歌（不返回，但占 offset）
        const auto page = [](int mainSongId) {
            return json(QJsonObject{
                {"code", 200}, {"count", 2}, {"more", true},
                {"programs", QJsonArray{programObject(mainSongId), programObject(0)}}});
        };

        m_store->request(QStringLiteral("radio:7"));
        QVERIFY(waitUntil([&] { return pending.contains(QStringLiteral("0")); }));
        pending.value(QStringLiteral("0"))(200, page(55));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("radio").toMap().value("id").toDouble() == 7.0
                && m_store->data().value("radio").toMap().value("programs").toList().size() == 1;
        }));
        const QVariantMap radio = m_store->data().value("radio").toMap();
        QCOMPARE(radio.value("name").toString(), QStringLiteral("电台"));
        QCOMPARE(radio.value("more").toBool(), true);
        const QVariantMap dj = radio.value("dj").toMap();
        QCOMPARE(dj.value("id").toDouble(), 99.0);
        QCOMPARE(dj.value("avatar").toString(), QStringLiteral("http://cover/dj.jpg"));
        const QVariantMap program = radio.value("programs").toList().first().toMap();
        QCOMPARE(program.value("id").toDouble(), 55.0);
        QCOMPARE(program.value("name").toString(), QStringLiteral("节目"));
        QCOMPARE(program.value("artist").toString(), QStringLiteral("主播"));
        QCOMPARE(program.value("album").toString(), QStringLiteral("电台"));
        QCOMPARE(program.value("albumId").toDouble(), 0.0);
        QCOMPARE(program.value("artistId").toDouble(), 0.0);
        QCOMPARE(program.value("artists").toList().first().toMap().value("name").toString(),
                 QStringLiteral("主播"));
        QCOMPARE(program.value("dur").toDouble(), 65.0);
        QCOMPARE(program.value("cover").toString(), QStringLiteral("http://cover/program.jpg"));
        QCOMPARE(program.value("programId").toDouble(), 66.0);
        QCOMPARE(program.value("radioId").toDouble(), 7.0);
        QCOMPARE(program.value("serial").toDouble(), 31.0);
        QCOMPARE(program.value("createTime").toDouble(), 1700000000000.0);
        QCOMPARE(program.value("description").toString(), QStringLiteral("节目简介"));

        QCOMPARE(radio.value("next").toInt(), 2);   // 被跳过的节目也占位
        m_store->request(QStringLiteral("radio:7:2"));
        QVERIFY(waitUntil([&] { return pending.contains(QStringLiteral("2")); }));
        QCOMPARE(detailRequests, 1);   // 翻页复用槽里的详情
        pending.value(QStringLiteral("2"))(200, page(56));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("radio").toMap().value("next").toInt() == 4;
        }));
        const QVariantMap paged = m_store->data().value("radio").toMap();
        QCOMPARE(paged.value("id").toDouble(), 7.0);
        const QVariantList programs = paged.value("programs").toList();
        QCOMPARE(programs.size(), 2);   // 第二页接在第一页后面
        QCOMPARE(programs.at(0).toMap().value("id").toDouble(), 55.0);
        QCOMPARE(programs.at(1).toMap().value("id").toDouble(), 56.0);
        QCOMPARE(detailRequests, 1);
        QCOMPARE(asc.size(), 2);
        QCOMPARE(asc.at(0), QStringLiteral("false"));
        QCOMPARE(asc.at(1), QStringLiteral("false"));
    }

    void discoverRecommendedProgramsFillProgramShape()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/personalized/djprogram") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"result", QJsonArray{QJsonObject{
                        {"id", 66}, {"program", programObject(55)}}}}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };
        m_store->request(QStringLiteral("discover:programs"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("recommendedPrograms").toList().size() == 1;
        }));
        const QVariantMap program =
            m_store->data().value("recommendedPrograms").toList().first().toMap();
        QCOMPARE(program.value("id").toDouble(), 55.0);
        QCOMPARE(program.value("name").toString(), QStringLiteral("节目"));
        QCOMPARE(program.value("artist").toString(), QStringLiteral("主播"));
        QCOMPARE(program.value("album").toString(), QStringLiteral("电台"));
        QCOMPARE(program.value("dur").toDouble(), 65.0);
        QCOMPARE(program.value("programId").toDouble(), 66.0);
        QCOMPARE(program.value("radioId").toDouble(), 7.0);
    }

    // 用户动态：第一页 cursor 是 SDK 初始值 -1；翻页沿用第一页代号并累积
    void userEventsPagesAndMapsResource()
    {
        int calls = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/event/get/900042") {
                ++calls;
                const bool second = calls > 1;
                const QJsonObject event{
                    {"id", second ? 900010 : 900009},
                    {"actName", second ? "分享歌单 B" : "分享歌单"},
                    {"eventTime", second ? 1700000001000.0 : 1700000000000.0},
                    {"user", QJsonObject{{"userId", second ? 900102 : 900101},
                                         {"nickname", second ? "B" : "A"},
                                         {"avatarUrl", second ? "http://avatar-b" : "http://avatar-a"}}},
                    {"info", QJsonObject{
                        {"title", second ? "标题 B" : "标题"},
                        {"description", second ? "正文 B" : "正文"},
                        {"likedCount", second ? 8 : 7},
                        {"commentCount", 2},
                        {"shareCount", 1},
                        {"commentThread", QJsonObject{
                            {"id", "A_EV_2_1"},
                            {"resourceType", 2},
                            {"resourceInfo", QJsonObject{
                                {"id", second ? 900142 : 900141},
                                {"name", second ? "第二页歌单" : "分享歌单"},
                                {"imgUrl", "http://event-cover"},
                                {"eventType", 13}}}}},
                    }},
                };
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"more", !second},
                    {"lasttime", second ? 1700000002000.0 : 1700000001000.0},
                    {"events", QJsonArray{event}},
                }));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("userEvents:900042"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("userEvents").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap first = m_store->data().value("userEvents").toMap();
        QCOMPARE(first.value("userId").toDouble(), 900042.0);
        QCOMPARE(first.value("next").toDouble(), 1700000001000.0);
        QCOMPARE(first.value("more").toBool(), true);
        const QVariantMap item = first.value("items").toList().first().toMap();
        QCOMPARE(item.value("id").toDouble(), 900009.0);
        QCOMPARE(item.value("action").toString(), QStringLiteral("分享歌单"));
        QCOMPARE(item.value("text").toString(), QStringLiteral("正文"));
        QCOMPARE(item.value("time").toDouble(), 1700000000000.0);
        QCOMPARE(item.value("user").toMap().value("id").toDouble(), 900101.0);
        QCOMPARE(item.value("user").toMap().value("avatar").toString(),
                 QStringLiteral("http://avatar-a"));
        const QVariantMap resource = item.value("resource").toMap();
        QCOMPARE(resource.value("kind").toString(), QStringLiteral("playlist"));
        QCOMPARE(resource.value("id").toDouble(), 900141.0);
        QCOMPARE(resource.value("name").toString(), QStringLiteral("分享歌单"));
        QCOMPARE(resource.value("cover").toString(), QStringLiteral("http://event-cover"));
        QCOMPARE(item.value("likes").toDouble(), 7.0);
        QCOMPARE(item.value("comments").toDouble(), 2.0);
        QCOMPARE(item.value("shares").toDouble(), 1.0);

        m_store->request(QStringLiteral("userEvents:900042:1700000001000"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("userEvents").toMap().value("items").toList().size() == 2;
        }));
        const QVariantMap secondPage = m_store->data().value("userEvents").toMap();
        QCOMPARE(secondPage.value("next").toDouble(), 1700000002000.0);
        QCOMPARE(secondPage.value("more").toBool(), false);
        QCOMPARE(secondPage.value("items").toList().at(0).toMap().value("id").toDouble(), 900009.0);
    }

    // 动态页被隐私挡住：页面内说明，不发全局 requestFailed
    void userEventsPrivacyIsInlineRestriction()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/event/get/900042") {
                respond(200, "{\"code\":-460,\"message\":\"用户隐私无权限查看\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };
        QSignalSpy restricted(m_store, &Store::requestRestricted);
        QSignalSpy failed(m_store, &Store::requestFailed);

        m_store->request(QStringLiteral("userEvents:900042"));
        QVERIFY(waitUntil([&] { return restricted.count() == 1; }));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(restricted.first().at(0).toString(), QStringLiteral("userEvents:900042"));
        const QVariantMap slot = m_store->data().value("userEvents").toMap();
        QCOMPARE(slot.value("userId").toDouble(), 900042.0);
        QCOMPARE(slot.value("restricted").toBool(), true);
        QCOMPARE(slot.value("message").toString(), QStringLiteral("用户隐私无权限查看"));
        QCOMPARE(slot.value("items").toList().size(), 0);
    }

    // 没有登录：所有账号绑定 key 都不发请求、不发失败；动态封面只清空槽
    void accountBoundRequestsNeedLogin()
    {
        m_client->clearAuthCookies();
        m_api->requests.clear();
        QSignalSpy failed(m_store, &Store::requestFailed);
        QSignalSpy restricted(m_store, &Store::requestRestricted);
        const QStringList keys{
            QStringLiteral("userEvents:900042"),
            QStringLiteral("userEvents:900042:1700000001000"),
            QStringLiteral("library:artists"),
            QStringLiteral("library:albums"),
            QStringLiteral("library:mvs"),
            QStringLiteral("notify:notices"),
            QStringLiteral("notify:comments"),
            QStringLiteral("notify:forwards"),
        };
        for (const QString &key : keys)
            m_store->request(key);
        QTest::qWait(150);
        QCOMPARE(m_api->requests.size(), 0);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(restricted.count(), 0);

        m_store->request(QStringLiteral("dynamicCover:900009"));
        QCOMPARE(m_api->requests.size(), 0);
        const QVariantMap cover = m_store->data().value("dynamicCover").toMap();
        QCOMPARE(cover.value("songId").toDouble(), 900009.0);
        QVERIFY(cover.value("url").toString().isEmpty());
        QCOMPARE(cover.value("width").toDouble(), 0.0);
        QCOMPARE(cover.value("height").toDouble(), 0.0);
        QCOMPARE(failed.count(), 0);
    }

    // 关注用户：成功同步 user / 关注列表 / 粉丝列表；失败发 followFailed；未登录不发请求
    void followUserUpdatesSlotsAndEmits()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/user/detail/900042") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", 900042}, {"nickname", "目标"},
                                            {"follows", 2}, {"followeds", 5}}},
                }));
                return;
            }
            if (req.path == "/weapi/user/playlist") {
                respond(200, json(QJsonObject{{"code", 200}, {"playlist", QJsonArray{}}}));
                return;
            }
            if (req.path == "/api/user/getfollows/900042") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", false},
                    {"follow", QJsonArray{
                        QJsonObject{{"userId", 900007}, {"nickname", "A"}, {"followed", false}},
                        QJsonObject{{"userId", 900008}, {"nickname", "B"}, {"followed", true}},
                    }},
                }));
                return;
            }
            if (req.path == "/api/user/getfolloweds/900042") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", false},
                    {"followeds", QJsonArray{
                        QJsonObject{{"userId", 900007}, {"nickname", "A"}, {"followed", false}},
                    }},
                }));
                return;
            }
            if (req.path == "/weapi/user/follow/900007"
                || req.path == "/weapi/user/follow/900042") {
                respond(200, "{\"code\":200}");
                return;
            }
            if (req.path == "/weapi/user/follow/900008") {
                respond(200, "{\"code\":500,\"message\":\"服务器拒绝\"}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("user:900042"));
        m_store->request(QStringLiteral("userFollows:900042"));
        m_store->request(QStringLiteral("userFolloweds:900042"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("user").toMap().value("900042").toMap().value("id").toDouble() == 900042
                && m_store->data().value("userFollows").toMap().value("items").toList().size() == 2
                && m_store->data().value("userFolloweds").toMap().value("items").toList().size() == 1;
        }));

        QSignalSpy changed(m_store, &Store::userFollowChanged);
        QSignalSpy failed(m_store, &Store::followFailed);
        // 关注的是列表里的另一个人：只同步列表行，不改当前被查看用户的资料
        m_store->setUserFollowed(900007, true);
        QVERIFY(waitUntil([&] { return changed.count() == 1; }));
        QCOMPARE(changed.first().at(0).toDouble(), 900007.0);
        QCOMPARE(changed.first().at(1).toBool(), true);
        QCOMPARE(m_store->data().value("user").toMap().value("900042").toMap().value("followed").toBool(), false);
        QCOMPARE(m_store->data().value("user").toMap().value("900042").toMap().value("followeds").toDouble(), 5.0);
        QCOMPARE(m_store->data().value("userFollows").toMap().value("items").toList()
                     .at(0).toMap().value("followed").toBool(), true);
        QCOMPARE(m_store->data().value("userFollows").toMap().value("items").toList()
                     .at(1).toMap().value("followed").toBool(), true);
        QCOMPARE(m_store->data().value("userFolloweds").toMap().value("items").toList()
                     .at(0).toMap().value("followed").toBool(), true);

        // 关注当前被查看的用户：资料槽与粉丝数一起更新
        m_store->setUserFollowed(900042, true);
        QVERIFY(waitUntil([&] { return changed.count() == 2; }));
        QCOMPARE(m_store->data().value("user").toMap().value("900042").toMap().value("followed").toBool(), true);
        QCOMPARE(m_store->data().value("user").toMap().value("900042").toMap().value("followeds").toDouble(), 6.0);

        m_store->setUserFollowed(900008, true);
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("关注用户"));
        QCOMPARE(failed.first().at(1).toString(), QStringLiteral("服务器拒绝"));

        m_client->clearAuthCookies();
        m_api->requests.clear();
        m_store->setUserFollowed(900007, false);
        QCOMPARE(failed.count(), 2);
        QCOMPARE(failed.last().at(0).toString(), QStringLiteral("取消关注"));
        QCOMPARE(failed.last().at(1).toString(), QStringLiteral("登录后才能关注"));
        QCOMPARE(m_api->requests.size(), 0);
    }

    // 收藏列表：三页形状；取关后当前槽立即移除，下一次请求重新取服务器状态
    void libraryPagesAndUnsubscribeRemoval()
    {
        int artistListRequests = 0;
        int albumListRequests = 0;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/artist/sublist") {
                ++artistListRequests;
                QJsonArray data;
                if (artistListRequests == 1) {
                    data.append(QJsonObject{
                        {"id", 900101}, {"name", "歌手"}, {"picUrl", "http://artist"},
                        {"alias", QJsonArray{"Alias"}}, {"albumSize", 9}, {"mvSize", 3}});
                } else if (artistListRequests == 2) {
                    data.append(QJsonObject{{"id", 900102}, {"name", "第二页歌手"}});
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"count", artistListRequests >= 3 ? 0 : 2},
                    {"hasMore", artistListRequests == 1},
                    {"data", data},
                }));
                return;
            }
            if (req.path == "/weapi/album/sublist") {
                ++albumListRequests;
                QJsonArray data;
                if (albumListRequests == 1) {
                    data.append(QJsonObject{
                        {"id", 900201}, {"name", "专辑"}, {"picUrl", "http://album"},
                        {"artists", QJsonArray{QJsonObject{{"id", 900101}, {"name", "歌手"}}}},
                        {"size", 4}});
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"count", albumListRequests >= 2 ? 0 : 1},
                    {"hasMore", false},
                    {"data", data},
                }));
                return;
            }
            if (req.path == "/weapi/cloudvideo/allvideo/sublist") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"count", 2}, {"hasMore", false},
                    {"data", QJsonArray{
                        QJsonObject{{"type", 0}, {"title", "MV"}, {"durationms", 265000},
                                    {"creator", QJsonArray{QJsonObject{{"userName", "作者"}}}},
                                    {"playTime", 782783}, {"coverUrl", "http://mv"},
                                    {"vid", "5307442"}},
                        QJsonObject{{"type", 1}, {"title", "视频"}, {"durationms", 1000},
                                    {"creator", QJsonArray{QJsonObject{{"userName", "用户"}}}},
                                    {"playTime", 3}, {"coverUrl", "http://video"},
                                    {"vid", "4E673ABCA5B939B889FDE4A2B148C2BD"}},
                    }},
                }));
                return;
            }
            if (req.path == "/weapi/artist/unsub" || req.path == "/weapi/album/unsub") {
                respond(200, "{\"code\":200}");
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("library:artists"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("savedArtists").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap artists = m_store->personal().value("savedArtists").toMap();
        QCOMPARE(artists.value("next").toInt(), 1);   // 下一页从第 1 条之后取
        QCOMPARE(artists.value("more").toBool(), true);
        QCOMPARE(artists.value("count").toDouble(), 2.0);
        const QVariantMap artist = artists.value("items").toList().first().toMap();
        QCOMPARE(artist.value("id").toDouble(), 900101.0);
        QCOMPARE(artist.value("alias").toString(), QStringLiteral("Alias"));
        QCOMPARE(artist.value("albums").toDouble(), 9.0);
        QCOMPARE(artist.value("mvs").toDouble(), 3.0);
        QCOMPARE(artist.value("cover").toString(), QStringLiteral("http://artist"));

        m_store->request(QStringLiteral("library:artists:1"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("savedArtists").toMap().value("items").toList().size() == 2;
        }));
        QCOMPARE(m_store->personal().value("savedArtists").toMap().value("next").toInt(), 2);

        m_store->request(QStringLiteral("library:albums"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("savedAlbums").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap album = m_store->personal().value("savedAlbums").toMap()
            .value("items").toList().first().toMap();
        QCOMPARE(album.value("id").toDouble(), 900201.0);
        QCOMPARE(album.value("name").toString(), QStringLiteral("专辑"));
        QCOMPARE(album.value("artist").toString(), QStringLiteral("歌手"));
        QCOMPARE(album.value("artistId").toDouble(), 900101.0);
        QCOMPARE(album.value("size").toDouble(), 4.0);

        m_store->request(QStringLiteral("library:mvs"));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("savedMvs").toMap().value("items").toList().size() == 2;
        }));
        const QVariantList mvs = m_store->personal().value("savedMvs").toMap().value("items").toList();
        QCOMPARE(mvs.at(0).toMap().value("kind").toString(), QStringLiteral("mv"));
        QCOMPARE(mvs.at(0).toMap().value("id").toDouble(), 5307442.0);
        QVERIFY(mvs.at(0).toMap().value("vid").toString().isEmpty());
        QCOMPARE(mvs.at(0).toMap().value("duration").toDouble(), 265.0);
        QCOMPARE(mvs.at(0).toMap().value("playCount").toDouble(), 782783.0);
        QCOMPARE(mvs.at(1).toMap().value("kind").toString(), QStringLiteral("video"));
        QCOMPARE(mvs.at(1).toMap().value("id").toDouble(), 0.0);
        QCOMPARE(mvs.at(1).toMap().value("vid").toString(),
                 QStringLiteral("4E673ABCA5B939B889FDE4A2B148C2BD"));

        QSignalSpy artistChanged(m_store, &Store::artistChanged);
        QSignalSpy albumChanged(m_store, &Store::albumChanged);
        m_store->setArtistSubscribed(900101, false);
        m_store->setAlbumSubscribed(900201, false);
        QVERIFY(waitUntil([&] { return artistChanged.count() == 1 && albumChanged.count() == 1; }));

        QCOMPARE(m_store->personal().value("savedArtists").toMap().value("items").toList().size(), 1);
        QCOMPARE(m_store->personal().value("savedArtists").toMap().value("count").toDouble(), 1.0);
        QCOMPARE(m_store->personal().value("savedAlbums").toMap().value("items").toList().size(), 0);
        QCOMPARE(m_store->personal().value("savedAlbums").toMap().value("count").toDouble(), 0.0);

        // 下一次请求必须重新走服务器，不能返回取关前的列表
        const int before = artistListRequests;
        m_store->request(QStringLiteral("library:artists"));
        QVERIFY(waitUntil([&] { return artistListRequests > before; }));
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("savedArtists").toMap().value("items").toList().isEmpty();
        }));

        // 新收藏：列表清空，下次打开向服务器取（新的一项只有服务器知道排在哪）。
        // 曾经只有取关会改列表，新关注的歌手要等重启才出现
        m_store->setAlbumSubscribed(900201, true);
        QVERIFY(waitUntil([&] { return albumChanged.count() == 2; }));
        QVERIFY(m_store->personal().value("savedAlbums").isNull());

        m_store->logout();
        QVERIFY(m_store->personal().value("savedArtists").isNull());
        QVERIFY(m_store->personal().value("savedAlbums").isNull());
        QVERIFY(m_store->personal().value("savedMvs").isNull());
    }

    // 三类通知：字段、资源映射、payload 类型文案、分页与退出清理
    void notificationListsMapAndPage()
    {
        int noticeCalls = 0;
        int forwardCalls = 0;
        const auto encode = [](const QJsonObject &object) {
            return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
        };
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/nuser/account/get") {
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"profile", QJsonObject{{"userId", 900042}, {"nickname", "我"}}},
                }));
                return;
            }
            if (req.path == "/weapi/msg/notices") {
                ++noticeCalls;
                QJsonArray notices;
                if (noticeCalls == 1) {
                    // 坏 payload 必须被跳过，不能变成空行
                    notices.append(QJsonObject{{"id", 900301}, {"userId", 900001},
                                               {"time", 1700000000001.0}, {"notice", "not-json"}});
                    const QJsonObject playlistPayload{
                        {"type", 2},
                        {"user", QJsonObject{{"userId", 900001}, {"nickname", "收藏者"},
                                             {"avatarUrl", "http://notice-avatar"}}},
                        {"playlist", QJsonObject{{"id", 900401}, {"name", "歌单名"},
                                                 {"coverImgUrl", "http://playlist-cover"}}},
                    };
                    notices.append(QJsonObject{{"id", 900302}, {"userId", 900001},
                                               {"time", 1700000000002.0},
                                               {"notice", encode(playlistPayload)}});
                    const QJsonObject generalPayload{
                        {"type", 1},
                        {"user", QJsonObject{{"userId", 900002}, {"nickname", "通知用户"},
                                             {"avatarUrl", "http://general-avatar"}}},
                        {"comment", QJsonObject{{"commentId", 900501}, {"content", "被赞的评论"}}},
                        {"generalNotice", QJsonObject{{"actionDesc", "赞了你的评论"},
                                                      {"content", "通知正文"},
                                                      {"type", 3}, {"resourceId", 900456}}},
                    };
                    notices.append(QJsonObject{{"id", 900303}, {"userId", 900002},
                                               {"time", 1700000000003.0},
                                               {"notice", encode(generalPayload)}});
                } else {
                    const QJsonObject playlistPayload{
                        {"type", 2},
                        {"user", QJsonObject{{"userId", 900001}, {"nickname", "收藏者"},
                                             {"avatarUrl", "http://notice-avatar"}}},
                        {"playlist", QJsonObject{{"id", 900402}, {"name", "第二页歌单"},
                                                 {"coverImgUrl", "http://playlist-cover-2"}}},
                    };
                    notices.append(QJsonObject{{"id", 900304}, {"userId", 900001},
                                               {"time", 1700000000004.0},
                                               {"notice", encode(playlistPayload)}});
                }
                respond(200, json(QJsonObject{
                    {"code", 200},
                    {"more", noticeCalls == 1},
                    {"notices", notices},
                }));
                return;
            }
            if (req.path == "/weapi/v1/user/comments/900042") {
                respond(200, json(QJsonObject{
                    {"code", 200}, {"total", 12}, {"more", true},
                    {"comments", QJsonArray{QJsonObject{
                        {"user", QJsonObject{{"userId", 900003}, {"nickname", "回复者"},
                                             {"avatarUrl", "http://reply-avatar"}}},
                        {"beRepliedContent", "原评论"},
                        {"content", "回复内容"},
                        {"time", 1700000000100.0},
                        {"combindId", "1700000000100_900003_4"},
                        {"resourceType", 4},
                    }}},
                }));
                return;
            }
            if (req.path == "/weapi/forwards/get") {
                ++forwardCalls;
                QJsonArray forwards;
                if (forwardCalls == 1) {
                    const QJsonObject song{
                        {"id", 900601}, {"name", "分享的歌"},
                        {"artists", QJsonArray{QJsonObject{{"name", "歌手"}}}},
                        {"album", QJsonObject{{"picUrl", "http://song-cover"}}},
                    };
                    const QJsonObject event{
                        {"user", QJsonObject{{"userId", 900004}, {"nickname", "动态作者"},
                                             {"avatarUrl", "http://event-avatar"}}},
                        {"type", 13},
                        {"json", encode(QJsonObject{{"msg", "事件正文"},
                                                    {"song", song}})},
                    };
                    // 转发行是 E{user,type,json}，E.json 才是 S{msg,event/song/...}
                    forwards.append(QJsonObject{
                        {"id", 900701}, {"userId", 900004}, {"time", 1700000000200.0},
                        {"type", 1},
                        {"json", encode(QJsonObject{
                            {"user", QJsonObject{{"userId", 900004}, {"nickname", "动态作者"},
                                                 {"avatarUrl", "http://event-avatar"}}},
                            {"type", 13},
                            {"json", encode(QJsonObject{{"msg", "@了你"}, {"event", event}})},
                        })},
                    });
                    const QJsonObject video{
                        {"vid", "5307442"}, {"title", "视频"},
                        {"coverUrl", "http://video-cover"},
                        {"creator", QJsonObject{{"nickname", "视频作者"}}},
                    };
                    forwards.append(QJsonObject{
                        {"id", 900702}, {"userId", 900005}, {"time", 1700000000201.0},
                        {"type", 1},
                        {"json", encode(QJsonObject{
                            {"user", QJsonObject{{"userId", 900005}, {"nickname", "视频用户"},
                                                 {"avatarUrl", "http://video-avatar"}}},
                            {"type", 13},
                            {"json", encode(QJsonObject{{"msg", "分享了视频"}, {"video", video}})},
                        })},
                    });
                } else {
                    forwards.append(QJsonObject{
                        {"id", 900703}, {"userId", 900006}, {"time", 1700000000202.0},
                        {"type", 1},
                        {"json", encode(QJsonObject{
                            {"user", QJsonObject{{"userId", 900006}, {"nickname", "第二页用户"}}},
                            {"type", 13},
                            {"json", encode(QJsonObject{
                                {"msg", "第二页"},
                                {"playlist", QJsonObject{{"id", 900801}, {"name", "第二页歌单"}}}})},
                        })},
                    });
                }
                respond(200, json(QJsonObject{
                    {"code", 200}, {"more", forwardCalls == 1}, {"newCount", 3},
                    {"lasttime", 1700000000201.0}, {"forwards", forwards},
                }));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        // 评论通知需要当前账号 uid：先走一次账号链
        m_store->refresh();
        QVERIFY(waitUntil([&] {
            return m_store->personal().value("account").toMap().value("id").toDouble() == 900042;
        }));

        m_store->request(QStringLiteral("notify:notices"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("notices").toMap().value("items").toList().size() == 2;
        }));
        const QVariantMap notices = m_store->data().value("notices").toMap();
        QCOMPARE(notices.value("next").toDouble(), 1700000000003.0);
        QCOMPARE(notices.value("more").toBool(), true);
        const QVariantMap playlistNotice = notices.value("items").toList().at(0).toMap();
        QCOMPARE(playlistNotice.value("id").toDouble(), 900302.0);
        QCOMPARE(playlistNotice.value("text").toString(), QStringLiteral("收藏了你的歌单"));
        QCOMPARE(playlistNotice.value("user").toMap().value("avatar").toString(),
                 QStringLiteral("http://notice-avatar"));
        QCOMPARE(playlistNotice.value("resource").toMap().value("kind").toString(),
                 QStringLiteral("playlist"));
        QCOMPARE(playlistNotice.value("resource").toMap().value("id").toDouble(), 900401.0);
        QCOMPARE(playlistNotice.value("resource").toMap().value("name").toString(),
                 QStringLiteral("歌单名"));
        const QVariantMap generalNotice = notices.value("items").toList().at(1).toMap();
        QCOMPARE(generalNotice.value("text").toString(), QStringLiteral("赞了你的评论"));
        QCOMPARE(generalNotice.value("resource").toMap().value("kind").toString(),
                 QStringLiteral("other"));
        QCOMPARE(generalNotice.value("resource").toMap().value("id").toDouble(), 900456.0);

        m_store->request(QStringLiteral("notify:notices:1700000000003"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("notices").toMap().value("items").toList().size() == 3;
        }));
        QCOMPARE(m_store->data().value("notices").toMap().value("more").toBool(), false);

        m_store->request(QStringLiteral("notify:comments"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("commentNotices").toMap().value("items").toList().size() == 1;
        }));
        const QVariantMap comments = m_store->data().value("commentNotices").toMap();
        QCOMPARE(comments.value("next").toDouble(), 1700000000100.0);
        QCOMPARE(comments.value("more").toBool(), true);
        QCOMPARE(comments.value("total").toDouble(), 12.0);
        const QVariantMap comment = comments.value("items").toList().first().toMap();
        QCOMPARE(comment.value("id").toString(), QStringLiteral("1700000000100_900003_4"));
        QCOMPARE(comment.value("content").toString(), QStringLiteral("回复内容"));
        QCOMPARE(comment.value("repliedContent").toString(), QStringLiteral("原评论"));
        QCOMPARE(comment.value("user").toMap().value("name").toString(), QStringLiteral("回复者"));
        QCOMPARE(comment.value("resource").toMap().value("kind").toString(),
                 QStringLiteral("other"));

        m_store->request(QStringLiteral("notify:forwards"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("forwards").toMap().value("items").toList().size() == 2;
        }));
        const QVariantMap forwards = m_store->data().value("forwards").toMap();
        QCOMPARE(forwards.value("next").toInt(), 2);
        QCOMPARE(forwards.value("more").toBool(), true);
        QCOMPARE(forwards.value("newCount").toInt(), 3);
        const QVariantMap firstForward = forwards.value("items").toList().at(0).toMap();
        QCOMPARE(firstForward.value("id").toDouble(), 900701.0);
        QCOMPARE(firstForward.value("user").toMap().value("name").toString(),
                 QStringLiteral("动态作者"));
        QCOMPARE(firstForward.value("message").toString(), QStringLiteral("@了你"));
        QCOMPARE(firstForward.value("resource").toMap().value("kind").toString(),
                 QStringLiteral("song"));
        QCOMPARE(firstForward.value("resource").toMap().value("id").toDouble(), 900601.0);
        QCOMPARE(firstForward.value("resource").toMap().value("name").toString(),
                 QStringLiteral("分享的歌"));
        const QVariantMap videoForward = forwards.value("items").toList().at(1).toMap();
        QCOMPARE(videoForward.value("resource").toMap().value("kind").toString(),
                 QStringLiteral("video"));
        QCOMPARE(videoForward.value("resource").toMap().value("id").toDouble(), 0.0);
        QCOMPARE(videoForward.value("resource").toMap().value("vid").toString(),
                 QStringLiteral("5307442"));

        m_store->request(QStringLiteral("notify:forwards:2"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("forwards").toMap().value("items").toList().size() == 3;
        }));
        QCOMPARE(m_store->data().value("forwards").toMap().value("next").toInt(), 3);

        m_store->logout();
        QVERIFY(m_store->data().value("notices").isNull());
        QVERIFY(m_store->data().value("commentNotices").isNull());
        QVERIFY(m_store->data().value("forwards").isNull());
    }

    // 动态封面：最新请求获胜；没有封面/未登录只写空槽，不发失败；退出清空
    void dynamicCoverLatestRequestWins()
    {
        FakeApi::Responder held;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/songplay/dynamic-cover") {
                if (req.body.contains("songId=900009")) {
                    held = respond;
                    return;
                }
                if (req.body.contains("songId=900010")) {
                    respond(200, json(QJsonObject{
                        {"code", 200},
                        {"data", QJsonObject{{"videoPlayUrl", "http://cover-10"},
                                             {"width", 1080}, {"height", 1920}}},
                    }));
                    return;
                }
                respond(200, json(QJsonObject{{"code", 200}, {"data", QJsonValue::Null}}));
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("dynamicCover:900009"));
        QVERIFY(waitUntil([&] { return static_cast<bool>(held); }));
        m_store->request(QStringLiteral("dynamicCover:900010"));
        QVERIFY(waitUntil([&] {
            return m_store->data().value("dynamicCover").toMap().value("songId").toDouble() == 900010;
        }));
        const QVariantMap available = m_store->data().value("dynamicCover").toMap();
        QCOMPARE(available.value("url").toString(), QStringLiteral("http://cover-10"));
        QCOMPARE(available.value("width").toInt(), 1080);
        QCOMPARE(available.value("height").toInt(), 1920);

        // 上一首的晚到回包不能覆盖当前槽
        held(200, json(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"videoPlayUrl", "http://cover-9"},
                                 {"width", 1}, {"height", 2}}},
        }));
        QTest::qWait(150);
        QCOMPARE(m_store->data().value("dynamicCover").toMap().value("songId").toDouble(), 900010.0);

        m_store->request(QStringLiteral("dynamicCover:900011"));
        QVERIFY(waitUntil([&] {
            const QVariantMap slot = m_store->data().value("dynamicCover").toMap();
            return slot.value("songId").toDouble() == 900011 && slot.value("url").toString().isEmpty();
        }));
        QCOMPARE(m_store->data().value("dynamicCover").toMap().value("width").toDouble(), 0.0);
        QCOMPARE(m_store->data().value("dynamicCover").toMap().value("height").toDouble(), 0.0);

        QSignalSpy failed(m_store, &Store::requestFailed);
        m_client->clearAuthCookies();
        m_api->requests.clear();
        m_store->request(QStringLiteral("dynamicCover:900012"));
        QCOMPARE(m_api->requests.size(), 0);
        const QVariantMap loggedOut = m_store->data().value("dynamicCover").toMap();
        QCOMPARE(loggedOut.value("songId").toDouble(), 900012.0);
        QVERIFY(loggedOut.value("url").toString().isEmpty());
        QCOMPARE(loggedOut.value("width").toDouble(), 0.0);
        QCOMPARE(loggedOut.value("height").toDouble(), 0.0);
        QCOMPARE(failed.count(), 0);

        m_store->logout();
        QVERIFY(m_store->data().value("dynamicCover").isNull());
    }

};

QTEST_GUILESS_MAIN(StoreTest)
#include "store_test.moc"

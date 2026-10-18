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

    static bool likedIdsContain(const Store *store, double songId)
    {
        for (const QVariant &v : store->personal().value("likedIds").toList())
            if (v.toDouble() == songId)
                return true;
        return false;
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
        QVERIFY(m_store->data().value("lyrics").toList().isEmpty());

        m_store->request(QStringLiteral("lyrics:9"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 2; }));
        QVERIFY(waitUntil([&] { return m_store->data().value("lyrics").toList().size() == 1; }));
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
        QVERIFY(m_store->data().value("lyrics").toList().isEmpty());

        m_store->request(QStringLiteral("lyrics:10"));
        QVERIFY(waitUntil([&] { return lyricRequests >= 2; }));
        QVERIFY(waitUntil([&] { return m_store->data().value("lyrics").toList().size() == 1; }));
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
        QVERIFY(waitUntil([&] { return m_store->data().value("lyricsTrackId").toDouble() == 11; }));
        m_store->request(QStringLiteral("lyrics:11"));
        QTest::qWait(150);
        QCOMPARE(lyricRequests, 1);   // 命中的是永久缓存
        QVERIFY(m_store->data().value("lyrics").toList().isEmpty());
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
        const QVariantMap user = m_store->data().value("user").toMap();
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

    // 旧页面的回包晚于新页面时必须丢弃：单槽数据由最新请求代号拥有。
    void staleAlbumResponseCannotOverwriteNewSlot()
    {
        FakeApi::Responder first;
        FakeApi::Responder second;
        m_api->handler = [&](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/api/v1/album/1") {
                first = respond;
                return;
            }
            if (req.path == "/api/v1/album/2") {
                second = respond;
                return;
            }
            respond(200, "{\"code\":200}");
        };

        m_store->request(QStringLiteral("album:1"));
        QVERIFY(waitUntil([&] { return static_cast<bool>(first); }));
        m_store->request(QStringLiteral("album:2"));
        QVERIFY(waitUntil([&] { return static_cast<bool>(second); }));

        second(200, json(QJsonObject{
            {"code", 200}, {"album", QJsonObject{{"id", 2}, {"name", "B"},
                {"artists", QJsonArray{}}, {"publishTime", 0}}}, {"songs", QJsonArray{}}}));
        QVERIFY(waitUntil([&] { return m_store->data().value("album").toMap().value("id").toDouble() == 2; }));

        first(200, json(QJsonObject{
            {"code", 200}, {"album", QJsonObject{{"id", 1}, {"name", "A"},
                {"artists", QJsonArray{}}, {"publishTime", 0}}}, {"songs", QJsonArray{}}}));
        QTest::qWait(150);
        QCOMPARE(m_store->data().value("album").toMap().value("id").toDouble(), 2.0);
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
            QStringLiteral("commentReplies:0:0"),
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
        m_store->setCommentLiked(1, 500, true);
        QVERIFY(waitUntil([&] {
            const QVariantMap c = m_store->data().value("comments").toMap().value("items").toList().first().toMap();
            return c.value("liked").toBool() && c.value("likes").toDouble() == 11;
        }));

        QSignalSpy failed(m_store, &Store::commentLikeFailed);
        m_store->setCommentLiked(1, 501, false);
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
        m_store->setCommentLiked(1, 888, true);
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(2).toString(),
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
        m_store->setCommentLiked(1, 999, true);
        m_store->setCommentLiked(1, 999, false);
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
        m_store->setCommentLiked(1, 777, true);
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
            const QVariantMap u = m_store->data().value("user").toMap();
            return u.value("name").toString() == "Listener"
                && u.value("playlists").toList().size() == 1;
        }));
        const QVariantMap u = m_store->data().value("user").toMap();
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

        failSecond = true;
        m_store->request(QStringLiteral("userFollows:42:30"));
        QVERIFY(waitUntil([&] { return failed.count() == 1; }));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("userFollows:42:30"));
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
            return m_store->data().value("mv").toMap().value("url").toString();
        };

        const auto urlPending = [this] {
            return m_store->data().value("mv").toMap().value("urlPending").toBool();
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
                && m_store->data().value("album").toMap().value("name").toString()
                       == QLatin1String("A")
                && m_store->data().value("artist").toMap().value("name").toString()
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
        QVERIFY(ageCache(2 * 60 * 60));   // 2 小时：三种 key 都还在新鲜期内

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
};

QTEST_GUILESS_MAIN(StoreTest)
#include "store_test.moc"

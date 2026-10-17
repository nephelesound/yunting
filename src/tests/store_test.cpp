// Store 的行为测试：seam 是本地 QTcpServer（扮演 music.163.com），不 mock Store/NcmClient 内部。
// 覆盖：业务码错误不写缓存、歌单写后缓存失效、连点红心不丢最后一次意图、迟到的详情不把
// 已取消的红心插回列表。
#include "../store.h"
#include "../ncmclient.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
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
            if (req.path.startsWith("/api/v1/resource/hotcomments/")) {
                respond(200, "{\"code\":200,\"hotComments\":["
                             "{\"commentId\":500,\"content\":\"a\",\"likedCount\":10,"
                             "\"liked\":false,\"time\":1,\"user\":{\"userId\":7,\"nickname\":\"u\"}},"
                             "{\"commentId\":501,\"content\":\"b\",\"likedCount\":20,"
                             "\"liked\":true,\"time\":2,\"user\":{\"userId\":8,\"nickname\":\"v\"}}]}");
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

    // 用户主页：资料 + 公开歌单都进 data.user
    void userProfileAndPlaylists()
    {
        m_api->handler = [](const FakeApi::Request &req, const FakeApi::Responder &respond) {
            if (req.path == "/weapi/v1/user/detail/42") {
                respond(200, json(QJsonObject{
                    {"code", 200},
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
        QCOMPARE(u.value("playlists").toList().first().toMap().value("name").toString(), QStringLiteral("Public"));
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

        m_store->request(QStringLiteral("mv:7"));
        QVERIFY(waitUntil([&] { return url() == QStringLiteral("http://media/1"); }));
        QCOMPARE(detailRequests, 1);
        QCOMPARE(urlRequests, 1);

        QTest::qWait(1100);   // 越过第一份地址的 expi

        m_store->request(QStringLiteral("mv:7"));
        QVERIFY(waitUntil([&] { return url() == QStringLiteral("http://media/2"); }));
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

    // 分类不看 message 文本：服务器自己的措辞不能冒充限流/过期
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
};

QTEST_GUILESS_MAIN(StoreTest)
#include "store_test.moc"

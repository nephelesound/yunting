// 命令行解析、各子命令与界面启动。main() 只设应用身份；正常退出与 SIGTERM/SIGINT
// 都经过 Qt 事件循环，aboutToQuit 里的凭据冲刷因此照常执行。
#include "cli.h"

#include "apicli.h"
#include "audioengine.h"
#include "clipboardhelper.h"
#include "diagnostics.h"
#include "downloadmanager.h"
#include "imagecache.h"
#include "layershellhelper.h"
#include "listentogether.h"
#include "mpris.h"
#include "mvvideo.h"
#include "ncmclient.h"
#include "powerinhibitor.h"
#include "sdk/listen_together.h"
#include "session.h"
#include "store.h"
#include "tray.h"

#include <KLocalizedQmlContext>
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSet>
#include <qqml.h>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <functional>

#ifdef YUNTING_KSIGNALHANDLER
#include <KSignalHandler>
#else
#include <QSocketNotifier>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
// 选项集中注册：注册顺序决定 --help 的输出顺序。
struct CommandLine {
    QCommandLineParser parser;
    QCommandLineOption dumpStore{"dump-store",
                                 "Fetch initial data, write {data, personal} JSON and exit",
                                 "file"};
    QCommandLineOption smokeAudio{"smoke-audio",
                                  "Resolve a song's play URL and play it for a few seconds",
                                  "songId"};
    QCommandLineOption audioQuality{"audio-quality",
                                    "Set playback quality: " + AudioEngine::qualityLevels().join(u'|'),
                                    "level"};
    QCommandLineOption importGtk4Cookies{"import-gtk4-cookies",
                                         "Import MUSIC_U/__csrf/NMTID from netease-cloud-music-gtk4 once"};
    QCommandLineOption allowWrite{"allow-write",
                                  "Allow explicit write commands (like / playlist selftests)"};
    QCommandLineOption like{"like", "Like/unlike a song: --like <songId> [on|off] --allow-write",
                            "songId"};
    QCommandLineOption commentLike{"comment-like",
                                   "Like/unlike a comment: --comment-like <songId>:<commentId> [on|off] --allow-write",
                                   "comment"};
    QCommandLineOption playlistSelfTest{"playlist-selftest",
                                        "Create, fill, rename and delete a private test playlist, then exit"};
    QCommandLineOption subscribeSelfTest{"playlist-subscribe-selftest",
                                         "Subscribe and unsubscribe one public playlist, then exit"};
    QCommandLineOption detail{"detail", "Fetch one artist/album and print JSON: --detail artist:6452",
                              "key"};
    QCommandLineOption comments{"comments", "Fetch one comments page: --comments <songId>[:offset]",
                                "song"};
    QCommandLineOption artistSongs{"artist-songs",
                                   "Fetch one artist songs page: --artist-songs <artistId>[:offset]",
                                   "artist"};
    QCommandLineOption mv{"mv", "Fetch one MV (detail + play URL): --mv <mvId>", "mv"};
    QCommandLineOption searchUsers{"search-users", "Search users: --search-users <query>", "query"};
    QCommandLineOption searchRadios{"search-radios", "Search radio stations: --search-radios <query>",
                                    "query"};
    QCommandLineOption api{"api",
                           "Inspect/debug backend API: --api list | --api get <path> [key=value...]",
                           "command"};
    QCommandLineOption apiPersonal{"api-personal", "Send stored cookies with --api get"};
    QCommandLineOption anonymous{"anonymous", "Skip keychain/cookies (read-only debugging)"};
    QCommandLineOption verbose{"verbose",
                               "Print warnings and this app's debug logs, including every request path, to stderr"};
    QCommandLineOption screenshot{"screenshot",
                                  "Load the UI, wait for live data, save a PNG of the window and exit",
                                  "file"};
    QCommandLineOption eqPreset{"eq-preset",
                                "Experimental EQ preset: flat/pop/rock/bass/vocal/live/spatial",
                                "preset"};
    QCommandLineOption soakAudio{"soak-audio",
                                 "Headless playback soak: cycle tracks/seek/EQ/device for N seconds",
                                 "seconds"};
    QCommandLineOption scrobbleMode{"scrobble-mode",
                                    "Listening log: legacy (web) or ncbl (PC client PLV/PLD)",
                                    "mode"};
    QCommandLineOption inbox{"inbox",
                             "Read-only mailbox; optional user ID for one conversation",
                             "uid", QString()};
    QCommandLineOption storeKey{"store-key",
                                "Debug: request one Store key and print data/personal",
                                "key"};
    QCommandLineOption listenTogetherSelfTest{
        "listen-together-selftest",
        "Create a room, sync a two-song queue and end it (requires --allow-write)"};
    QCommandLineOption refreshSession{"refresh-session",
                                      "Refresh the stored MUSIC_U cookie and exit"};
    QCommandLineOption downloadSelfTest{"download-selftest",
                                        "Download one song to the offline directory, verify it and remove it (requires --allow-write)",
                                        "songId"};

    CommandLine()
    {
        parser.setApplicationDescription(QStringLiteral("NetEase Cloud Music client"));
        parser.addHelpOption();
        parser.addOption(dumpStore);
        parser.addOption(smokeAudio);
        parser.addOption(audioQuality);
        parser.addOption(importGtk4Cookies);
        parser.addOption(allowWrite);
        parser.addOption(like);
        parser.addOption(commentLike);
        parser.addOption(playlistSelfTest);
        parser.addOption(subscribeSelfTest);
        parser.addOption(detail);
        parser.addOption(comments);
        parser.addOption(artistSongs);
        parser.addOption(mv);
        parser.addOption(searchUsers);
        parser.addOption(searchRadios);
        parser.addOption(api);
        parser.addOption(apiPersonal);
        parser.addOption(anonymous);
        parser.addOption(verbose);
        parser.addOption(screenshot);
        parser.addOption(eqPreset);
        parser.addOption(soakAudio);
        parser.addOption(scrobbleMode);
        parser.addOption(inbox);
        parser.addOption(storeKey);
        parser.addOption(listenTogetherSelfTest);
        parser.addOption(refreshSession);
        parser.addOption(downloadSelfTest);
    }
};

// CLI 只读调试：等某个 data 槽位落地，打印 `{ <key>: value }`。缓存命中会同步触发
// dataChanged，所以先 request 再决定是否进事件循环。
int waitForStoreData(Store &store, const QString &requestKey, const QString &dataKey,
                     const std::function<bool(const QVariantMap &)> &match)
{
    QEventLoop loop;
    int rc = 2;
    bool printed = false;
    QObject::connect(&store, &Store::dataChanged, &loop, [&] {
        if (printed)
            return;
        const QVariant value = store.data().value(dataKey);
        if (!value.isValid() || value.isNull() || !match(value.toMap()))
            return;
        printed = true;
        const QJsonObject out{{dataKey, QJsonValue::fromVariant(value)}};
        std::printf("%s", QJsonDocument(out).toJson(QJsonDocument::Indented).constData());
        rc = 0;
        loop.quit();
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    store.request(requestKey);
    if (rc == 2)
        loop.exec();
    return rc;
}

#ifndef YUNTING_KSIGNALHANDLER
int g_signalPipe[2] = {-1, -1};

// 信号处理里只写一个字节，退出留给事件循环里的 QSocketNotifier。
void signalToPipe(int)
{
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(g_signalPipe[1], &byte, sizeof byte);
}
#endif

void installQuitSignals()
{
#ifdef YUNTING_KSIGNALHANDLER
    KSignalHandler *handler = KSignalHandler::self();
    QObject::connect(handler, &KSignalHandler::signalReceived, qApp, [](int) {
        QCoreApplication::quit();
    });
    handler->watchSignal(SIGTERM);
    handler->watchSignal(SIGINT);
#else
    if (::pipe(g_signalPipe) != 0)
        return;
    for (const int fd : g_signalPipe) {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    auto *notifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, qApp);
    QObject::connect(notifier, &QSocketNotifier::activated, qApp, [notifier] {
        char byte = 0;
        while (::read(notifier->socket(), &byte, sizeof byte) > 0) { }
        QCoreApplication::quit();
    });
    struct sigaction action {};
    action.sa_handler = signalToPipe;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
#endif
}

// QtKeychain 的读写是异步的：等 jar 从存储读出（钥匙串或后备文件）再继续。
// 兜底 10 秒防钥匙串守护进程挂死时永远起不来。
void waitForClientReady(NcmClient &client)
{
    if (client.isReady())
        return;
    QEventLoop readyLoop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&client, &NcmClient::ready, &readyLoop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &readyLoop, &QEventLoop::quit);
    timeout.start(10 * 1000);
    readyLoop.exec();
}

int runImport(NcmClient &client)
{
    // 一次性导入：写进与启动同一个存储（钥匙串，或后备文件）。异步链在内部跑完。
    const int n = client.importGtk4();
    if (n < 0) {
        std::fprintf(stderr, "import failed\n");
        return 1;
    }
    std::printf("imported %d cookies\n", n);
    return 0;
}

// 子命令共享的后端对象；构造顺序与原来的 main 一致。
struct Backend {
    NcmClient &client;
    Store &store;
    AudioEngine &audio;
    Session &session;
    Mpris &mpris;
};

int runDump(const CommandLine &cli, Backend &backend)
{
    Store &store = backend.store;
    store.waitForInitial();
    // 按需数据走真实的 request() 路径（经 QueryCache），契约也就覆盖了它：
    // 搜索一个词、取热歌榜第一首的歌词与热评，等三者落地（兜底 15 秒）
    const QString probeQuery = QStringLiteral("林俊杰");
    const QVariantList hot = store.data().value("tracks").toList();
    const double probeSong = hot.isEmpty() ? 0 : hot.first().toMap().value("id").toDouble();
    const auto landed = [&] {
        const QVariantMap d = store.data();
        return d.value("search").toMap().value("query").toString() == probeQuery
            && !d.value("search").toMap().value("songs").toList().isEmpty()
            && (probeSong == 0 || d.value("lyrics").toMap().contains(QString::number(qlonglong(probeSong))))
            && (probeSong == 0 || d.value("comments").toMap().value("songId").toDouble() == probeSong);
    };
    store.request(QStringLiteral("search:") + probeQuery);
    if (probeSong != 0) {
        store.request(QStringLiteral("lyrics:%1").arg(qlonglong(probeSong)));
        store.request(QStringLiteral("comments:%1").arg(qlonglong(probeSong)));
    }
    QEventLoop loop;
    QTimer::singleShot(15 * 1000, &loop, &QEventLoop::quit);
    QObject::connect(&store, &Store::dataChanged, &loop, [&] { if (landed()) loop.quit(); });
    if (!landed())
        loop.exec();
    const QJsonObject root{{"data", QJsonObject::fromVariantMap(store.data())},
                           {"personal", QJsonObject::fromVariantMap(store.personal())}};
    QFile out(cli.parser.value(cli.dumpStore));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write %s\n",
                     qPrintable(cli.parser.value(cli.dumpStore)));
        return 1;
    }
    out.write(QJsonDocument(root).toJson());
    return 0;
}

int runApi(const CommandLine &cli, NcmClient &client)
{
    return runApiCommand(client, cli.parser.value(cli.api), cli.parser.positionalArguments(),
                         cli.parser.isSet(cli.apiPersonal));
}

int runLike(const CommandLine &cli, Backend &backend)
{
    // 只用于验证 weapi 写链路：--like 108485 on / --like 108485 off
    NcmClient &client = backend.client;
    Store &store = backend.store;
    const double songId = cli.parser.value(cli.like).toDouble();
    const QString mode = cli.parser.positionalArguments().value(0, QStringLiteral("on"));
    const bool liked = mode.compare(QLatin1String("off"), Qt::CaseInsensitive) != 0;
    if (songId <= 0) {
        std::fprintf(stderr, "invalid song id\n");
        return 1;
    }
    if (!client.hasMusicU()) {
        std::fprintf(stderr, "not logged in\n");
        return 1;
    }
    // 先加载首屏，trackById 才能找到 287398；也让本地歌单卡片的计数/曲目检查有意义
    store.waitForInitial();
    QEventLoop loop;
    int rc = 2;
    QObject::connect(&store, &Store::likeSucceeded, &loop, [&](double id, bool state) {
        if (id == songId) {
            rc = 0;
            std::printf("%s %lld\n", state ? "liked" : "unliked", qlonglong(songId));
            loop.quit();
        }
    });
    QObject::connect(&store, &Store::likeFailed, &loop,
                     [&](double id, bool, const QString &message) {
        if (id == songId) {
            rc = 1;
            std::fprintf(stderr, "%s\n", qPrintable(message));
            loop.quit();
        }
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    store.setLiked(songId, liked);
    if (rc == 2)
        loop.exec();
    return rc;
}

int runCommentLike(const CommandLine &cli, Backend &backend)
{
    // 排查评论点赞风控：--comment-like 287398:9667444137 on --allow-write
    NcmClient &client = backend.client;
    Store &store = backend.store;
    const QStringList parts = cli.parser.value(cli.commentLike).split(u':');
    if (parts.size() != 2) {
        std::fprintf(stderr, "expected songId:commentId\n");
        return 1;
    }
    const double songId = parts.at(0).toDouble();
    const double commentId = parts.at(1).toDouble();
    if (songId <= 0 || commentId <= 0) {
        std::fprintf(stderr, "invalid song/comment id\n");
        return 1;
    }
    if (!client.hasMusicU()) {
        std::fprintf(stderr, "not logged in\n");
        return 1;
    }
    const QString mode = cli.parser.positionalArguments().value(0, QStringLiteral("on"));
    const bool liked = mode.compare(QLatin1String("off"), Qt::CaseInsensitive) != 0;
    QEventLoop loop;
    int rc = 2;
    QObject::connect(&store, &Store::commentLikeSucceeded, &loop,
                     [&](const QString &kind, double s, double c, bool state) {
        if (kind == QLatin1String("song") && s == songId && c == commentId) {
            std::printf("%s %lld:%lld\n", state ? "liked" : "unliked",
                        qlonglong(songId), qlonglong(commentId));
            rc = 0;
            loop.quit();
        }
    });
    QObject::connect(&store, &Store::commentLikeFailed, &loop,
                     [&](const QString &kind, double s, double c, const QString &message) {
        if (kind == QLatin1String("song") && s == songId && c == commentId) {
            std::fprintf(stderr, "comment like failed: %s\n", qPrintable(message));
            rc = 1;
            loop.quit();
        }
    });
    QTimer::singleShot(8 * 1000, &loop, &QEventLoop::quit);
    store.setCommentLiked(QStringLiteral("song"), songId, commentId, liked);
    loop.exec();
    if (rc == 2) {
        std::fprintf(stderr, "comment-like: no response within 8s (unknown; not a success)\n");
        rc = 2;
    }
    return rc;
}

int runSearch(const CommandLine &cli, Backend &backend)
{
    const bool users = cli.parser.isSet(cli.searchUsers);
    const QString q = cli.parser.value(users ? cli.searchUsers : cli.searchRadios);
    if (q.isEmpty()) {
        std::fprintf(stderr, "empty query\n");
        return 1;
    }
    const QString kind = users ? QStringLiteral("searchUsers") : QStringLiteral("searchRadios");
    const QString key = (users ? QStringLiteral("searchUsers:") : QStringLiteral("searchRadios:")) + q;
    return waitForStoreData(backend.store, key, kind, [&](const QVariantMap &result) {
        return result.value("query").toString() == q;
    });
}

int runMv(const CommandLine &cli, Backend &backend)
{
    const double mvId = cli.parser.value(cli.mv).toDouble();
    if (mvId <= 0) {
        std::fprintf(stderr, "expected <mvId>\n");
        return 1;
    }
    return waitForStoreData(backend.store, QStringLiteral("mv:%1").arg(qlonglong(mvId)),
                            QStringLiteral("mv"), [&](const QVariantMap &byId) {
        return byId.contains(QString::number(qlonglong(mvId)));
    });
}

int runArtistSongs(const CommandLine &cli, Backend &backend)
{
    const QStringList parts = cli.parser.value(cli.artistSongs).split(u':');
    const double artistId = parts.value(0).toDouble();
    const int offset = parts.value(1).toInt();
    if (artistId <= 0 || offset < 0) {
        std::fprintf(stderr, "expected <artistId>[:offset]\n");
        return 1;
    }
    return waitForStoreData(backend.store,
                            QStringLiteral("artistSongs:%1:%2").arg(qlonglong(artistId)).arg(offset),
                            QStringLiteral("artistSongs"), [&](const QVariantMap &page) {
        return page.value("artistId").toDouble() == artistId
            && page.value("offset").toInt() == offset;
    });
}

int runComments(const CommandLine &cli, Backend &backend)
{
    const QStringList parts = cli.parser.value(cli.comments).split(u':');
    const double songId = parts.value(0).toDouble();
    const int offset = parts.value(1).toInt();
    if (songId <= 0 || offset < 0) {
        std::fprintf(stderr, "expected <songId>[:offset]\n");
        return 1;
    }
    return waitForStoreData(backend.store,
                            QStringLiteral("comments:%1:%2").arg(qlonglong(songId)).arg(offset),
                            QStringLiteral("commentPage"), [&](const QVariantMap &page) {
        return page.value("songId").toDouble() == songId
            && page.value("offset").toInt() == offset;
    });
}

int runDetail(const CommandLine &cli, Backend &backend)
{
    const QString key = cli.parser.value(cli.detail);
    const QString kind = key.section(u':', 0, 0);
    const double id = key.section(u':', 1).toDouble();
    if ((kind != QLatin1String("artist") && kind != QLatin1String("album")) || id <= 0) {
        std::fprintf(stderr, "expected artist:<id> or album:<id>\n");
        return 1;
    }
    return waitForStoreData(backend.store, key, kind, [&](const QVariantMap &byId) {
        return byId.contains(QString::number(qlonglong(id)));
    });
}

int runSubscribeSelfTest(const CommandLine &cli, Backend &backend)
{
    Q_UNUSED(cli);
    NcmClient &client = backend.client;
    Store &store = backend.store;
    if (!client.hasMusicU()) {
        std::fprintf(stderr, "not logged in\n");
        return 1;
    }
    store.waitForInitial();
    QSet<double> subscribed;
    for (const QVariant &v : store.personal().value("subscribed").toList())
        subscribed.insert(v.toMap().value("id").toDouble());
    double pid = 0;
    for (const QVariant &v : store.personal().value("playlists").toList()) {
        const QVariantMap card = v.toMap();
        const double id = card.value("id").toDouble();
        if (id > 0 && !subscribed.contains(id)) {
            pid = id;
            std::printf("candidate %lld %s\n", qlonglong(pid), qPrintable(card.value("name").toString()));
            break;
        }
    }
    if (pid <= 0) {
        std::fprintf(stderr, "no unsubscribed public playlist found\n");
        return 2;
    }
    QEventLoop loop;
    int rc = 2;
    int step = 0;
    const auto has = [&] {
        for (const QVariant &v : store.personal().value("subscribed").toList())
            if (v.toMap().value("id").toDouble() == pid)
                return true;
        return false;
    };
    QObject::connect(&store, &Store::playlistChanged, &loop, [&](double id) {
        if (id != pid)
            return;
        if (step == 0) {
            if (!has()) {
                std::fprintf(stderr, "subscribe landed but local subscribed is missing\n");
                loop.quit();
                return;
            }
            step = 1;
            std::printf("subscribed\n");
            store.setPlaylistSubscribed(pid, false);
        } else if (step == 1) {
            if (has()) {
                std::fprintf(stderr, "unsubscribe landed but local subscribed still has it\n");
                loop.quit();
                return;
            }
            std::printf("unsubscribed, local=ok\n");
            rc = 0;
            loop.quit();
        }
    });
    QObject::connect(&store, &Store::playlistActionFailed, &loop,
                     [&](const QString &action, const QString &message) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(action), qPrintable(message));
        loop.quit();
    });
    QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
    store.setPlaylistSubscribed(pid, true);
    loop.exec();
    if (rc != 0 && has()) {
        // 失败也尽量取消订阅，账号回到原状
        QEventLoop cleanup;
        QObject::connect(&store, &Store::playlistChanged, &cleanup, [&](double id) {
            if (id == pid)
                cleanup.quit();
        });
        QTimer::singleShot(5 * 1000, &cleanup, &QEventLoop::quit);
        store.setPlaylistSubscribed(pid, false);
        cleanup.exec();
    }
    return rc;
}

int runPlaylistSelfTest(const CommandLine &cli, Backend &backend)
{
    Q_UNUSED(cli);
    NcmClient &client = backend.client;
    Store &store = backend.store;
    // 端到端验证歌单写接口；只创建一个私有测试歌单，最后删除，账号回到原状。
    if (!client.hasMusicU()) {
        std::fprintf(stderr, "not logged in\n");
        return 1;
    }
    // 先加载首屏，trackById 才能找到 287398；也让本地歌单卡片的计数/曲目检查有意义
    store.waitForInitial();
    QEventLoop loop;
    int rc = 2;
    int step = 0;
    double pid = 0;
    bool localOk = true;
    const auto cardIn = [&](const char *key) -> QVariantMap {
        for (const QVariant &v : store.personal().value(QLatin1String(key)).toList()) {
            QVariantMap card = v.toMap();
            if (card.value("id").toDouble() == pid)
                return card;
        }
        return {};
    };
    const QString name = QStringLiteral("云听自检 %1").arg(QDateTime::currentSecsSinceEpoch());
    QObject::connect(&store, &Store::playlistCreated, &loop, [&](double id, const QString &) {
        if (step != 0) return;
        pid = id;
        step = 1;
        localOk = localOk && !cardIn("mine").isEmpty();
        std::printf("created %lld\n", qlonglong(pid));
        store.addSongToPlaylist(pid, 287398);
    });
    QObject::connect(&store, &Store::playlistChanged, &loop, [&](double id) {
        if (id != pid) return;
        if (step == 1) {
            step = 2;
            const QVariantMap card = cardIn("mine");
            localOk = localOk && card.value("count").toDouble() == 1
                && !card.value("tracks").toList().isEmpty();
            std::printf("added\n");
            store.removeSongFromPlaylist(pid, 287398);
        } else if (step == 2) {
            step = 3;
            const QVariantMap card = cardIn("mine");
            localOk = localOk && card.value("count").toDouble() == 0
                && card.value("tracks").toList().isEmpty();
            std::printf("removed\n");
            store.renamePlaylist(pid, name + QStringLiteral(" renamed"));
        } else if (step == 3) {
            step = 4;
            localOk = localOk && cardIn("mine").value("name").toString().endsWith(QStringLiteral(" renamed"));
            std::printf("renamed\n");
            store.deletePlaylist(pid);
        } else if (step == 4) {
            localOk = localOk && cardIn("mine").isEmpty();
            std::printf("deleted, local=%s\n", localOk ? "ok" : "bad");
            rc = localOk ? 0 : 1;
            loop.quit();
        }
    });
    QObject::connect(&store, &Store::playlistActionFailed, &loop,
                     [&](const QString &action, const QString &message) {
        std::fprintf(stderr, "%s: %s\n", qPrintable(action), qPrintable(message));
        rc = 1;
        loop.quit();
    });
    QTimer::singleShot(45 * 1000, &loop, &QEventLoop::quit);
    store.createPlaylist(name, true);
    loop.exec();
    if (rc != 0 && pid > 0) {
        // 失败也尽量删除测试歌单，避免留下垃圾；等请求真正落地再退出
        QEventLoop cleanup;
        QObject::connect(&store, &Store::playlistChanged, &cleanup, [&](double id) {
            if (id == pid)
                cleanup.quit();
        });
        QTimer::singleShot(5 * 1000, &cleanup, &QEventLoop::quit);
        store.deletePlaylist(pid);
        cleanup.exec();
    }
    return rc;
}

int runSoak(const CommandLine &cli, QApplication &app, Backend &backend)
{
    AudioEngine &audio = backend.audio;
    if (cli.parser.isSet(cli.audioQuality))
        audio.setQuality(cli.parser.value(cli.audioQuality));
    const int seconds = std::max(10, cli.parser.value(cli.soakAudio).toInt());
    const QList<double> ids{1973665667, 287398, 108485, 347230, 1330348068};
    const QStringList presets{QStringLiteral("flat"), QStringLiteral("pop"), QStringLiteral("rock"),
                              QStringLiteral("bass"), QStringLiteral("vocal"), QStringLiteral("live"),
                              QStringLiteral("spatial")};
    auto *step = new int(0);
    auto *failures = new int(0);
    QObject::connect(&audio, &AudioEngine::failed, &audio, [failures](const QString &reason) {
        ++*failures;
        std::fprintf(stderr, "[soak] failure: %s\n", qPrintable(reason));
    });
    auto rssKb = [] {
        QFile status(QStringLiteral("/proc/self/status"));
        if (!status.open(QIODevice::ReadOnly))
            return qint64(0);
        for (const QByteArray &line : status.readAll().split('\n'))
            if (line.startsWith("VmRSS:"))
                return line.mid(6).trimmed().split(' ').value(0).toLongLong();
        return qint64(0);
    };
    audio.setVolume(0.55);
    auto *cycle = new QTimer(&audio);
    QObject::connect(cycle, &QTimer::timeout, &audio, [&audio, ids, presets, step, failures, rssKb] {
        const int i = (*step)++;
        audio.applyEqPreset(presets.at(i % presets.size()));
        audio.setVolume(0.35 + 0.05 * (i % 10));
        audio.setMuted(i % 12 == 6);
        audio.setTrack(QVariantMap{{"id", ids.at(i % ids.size())}, {"dur", 0}});
        audio.setPlaying(true);
        if (i % 2 == 0)
            audio.seek(12);
        if (i % 7 == 0) {
            const QVariantList outputs = audio.audioOutputs();
            if (outputs.size() > 1) {
                const QString id = outputs.at((i / 7) % outputs.size()).toMap().value("id").toString();
                audio.setOutputDevice(id);
            }
        }
        std::printf("[soak] step=%d track=%lld rss=%lldkB failures=%d\n", i,
                    qlonglong(ids.at(i % ids.size())), rssKb(), *failures);
        std::fflush(stdout);
    });
    cycle->start(20 * 1000);
    QTimer::singleShot(seconds * 1000, &app, [&audio, cycle, failures] {
        audio.setPlaying(false);
        audio.setMuted(false);
        cycle->stop();
        std::printf("[soak] done failures=%d\n", *failures);
        QCoreApplication::quit();
    });
    return app.exec();
}

int runStoreKey(const CommandLine &cli, Backend &backend)
{
    NcmClient &client = backend.client;
    Store &store = backend.store;
    if (!client.isReady()) {
        QEventLoop ready;
        QObject::connect(&client, &NcmClient::ready, &ready, &QEventLoop::quit);
        QTimer::singleShot(15 * 1000, &ready, &QEventLoop::quit);
        ready.exec();
    }
    QEventLoop loop;
    QObject::connect(&store, &Store::dataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&store, &Store::requestFailed, &loop, [&loop](const QString &key, const QString &reason) {
        std::fprintf(stderr, "store request failed: %s: %s\n", qPrintable(key), qPrintable(reason));
        loop.quit();
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    store.request(cli.parser.value(cli.storeKey));
    loop.exec();
    const QJsonObject out{
        {"data", QJsonObject::fromVariantMap(store.data())},
        {"personal", QJsonObject::fromVariantMap(store.personal())},
    };
    std::printf("%s", QJsonDocument(out).toJson(QJsonDocument::Indented).constData());
    return 0;
}

int runInbox(const CommandLine &cli, Backend &backend)
{
    NcmClient &client = backend.client;
    Store &store = backend.store;
    if (!client.hasMusicU()) {
        std::fprintf(stderr, "not logged in\n");
        return 1;
    }
    if (!client.isReady()) {
        QEventLoop ready;
        QObject::connect(&client, &NcmClient::ready, &ready, &QEventLoop::quit);
        QTimer::singleShot(15 * 1000, &ready, &QEventLoop::quit);
        ready.exec();
    }
    const QString uid = cli.parser.value(cli.inbox);
    const QString key = uid.isEmpty() ? QStringLiteral("inbox")
                                      : QStringLiteral("inboxThread:") + uid;
    QEventLoop loop;
    QObject::connect(&store, &Store::dataChanged, &loop, &QEventLoop::quit);
    QObject::connect(&store, &Store::requestFailed, &loop, [&loop](const QString &key, const QString &reason) {
        std::fprintf(stderr, "inbox failed: %s: %s\n", qPrintable(key), qPrintable(reason));
        loop.quit();
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    store.request(key);
    loop.exec();
    const QJsonObject out{
        {"inbox", QJsonObject::fromVariantMap(store.data().value("inbox").toMap())},
        {"inboxThread", QJsonObject::fromVariantMap(store.data().value("inboxThread").toMap())},
    };
    std::printf("%s", QJsonDocument(out).toJson(QJsonDocument::Indented).constData());
    return 0;
}

int runSmokeAudio(const CommandLine &cli, QApplication &app, Backend &backend)
{
    // 无窗口：事件循环跑在 QCoreApplication 节奏上，AudioEngine 自己打印状态
    if (cli.parser.isSet(cli.audioQuality))
        backend.audio.setQuality(cli.parser.value(cli.audioQuality));
    const int rc = backend.audio.smokeTest(cli.parser.value(cli.smokeAudio).toDouble(), 6);
    if (rc != 0)
        return rc;
    return app.exec();
}

int runDownloadSelfTest(const CommandLine &cli, Backend &backend)
{
    const qint64 songId = cli.parser.value(cli.downloadSelfTest).toLongLong();
    if (songId <= 0)
        return 1;
    DownloadManager downloads(&backend.client);
    QEventLoop loop;
    int rc = 1;
    QObject::connect(&downloads, &DownloadManager::finished, &loop,
                     [&](double id, const QString &path) {
        const qint64 size = QFileInfo(path).size();
        std::printf("download selftest: id=%lld bytes=%lld path=%s\n",
                    static_cast<long long>(id), static_cast<long long>(size),
                    qPrintable(path));
        downloads.remove(id);
        rc = size > 0 ? 0 : 1;
        loop.quit();
    });
    QObject::connect(&downloads, &DownloadManager::failed, &loop,
                     [&](double, const QString &message) {
        std::fprintf(stderr, "download selftest failed: %s\n", qPrintable(message));
        loop.quit();
    });
    QTimer::singleShot(90 * 1000, &loop, &QEventLoop::quit);
    downloads.download(static_cast<double>(songId), QStringLiteral("lossless"));
    loop.exec();
    return rc;
}

int runRefreshSession(Backend &backend)
{
    QEventLoop loop;
    int rc = 1;
    Session session(&backend.client, &backend.store);
    QObject::connect(&session, &Session::refreshed, &loop, [&] {
        std::printf("session refresh: ok\n");
        rc = 0;
        loop.quit();
    });
    QObject::connect(&session, &Session::refreshFailed, &loop, [&](const QString &message) {
        std::fprintf(stderr, "session refresh failed: %s\n", qPrintable(message));
        loop.quit();
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    session.refreshSession();
    loop.exec();
    return rc;
}

int runListenTogetherSelfTest(Backend &backend)
{
    Store &store = backend.store;
    NcmClient &client = backend.client;
    AudioEngine &audio = backend.audio;
    store.waitForInitial();
    if (client.hasMusicU() && store.accountUid() > 0) {
        // accountUid is filled by the initial account request; waitForInitial covers it.
    } else {
        std::fprintf(stderr, "listen-together selftest requires a signed-in session\n");
        return 1;
    }

    // 先只读查一次，避免把账号上已有的（别人/其它设备的）房间当成自测房间同步或结束。
    bool alreadyInRoom = false;
    {
        QEventLoop statusLoop;
        ncm::sdk::fetchStatus(client, &statusLoop,
                              [&](ncm::sdk::Result<ncm::sdk::ListenTogetherStatus> status) {
            alreadyInRoom = status && status.value->inRoom;
            statusLoop.quit();
        });
        QTimer::singleShot(15 * 1000, &statusLoop, &QEventLoop::quit);
        statusLoop.exec();
    }
    if (alreadyInRoom) {
        std::fprintf(stderr, "listen-together selftest: account is already in a room; refusing to touch it\n");
        return 1;
    }

    ListenTogether room(&client, &store, &audio);
    QEventLoop loop;
    int rc = 1;
    bool queueSent = false;

    QObject::connect(&room, &ListenTogether::stateChanged, &loop, [&] {
        if (!room.inRoom() || queueSent)
            return;
        queueSent = true;
        room.syncQueue(QVariantList{108485.0, 1.0});
        QTimer::singleShot(1500, &room, &ListenTogether::leaveRoom);
    });
    QObject::connect(&room, &ListenTogether::leftRoom, &loop, [&] {
        std::printf("listen-together selftest: created, synced, ended\n");
        rc = 0;
        loop.quit();
    });
    QObject::connect(&room, &ListenTogether::errorChanged, &loop, [&] {
        if (!room.error().isEmpty())
            std::fprintf(stderr, "listen-together: %s\n", qPrintable(room.error()));
    });
    QTimer::singleShot(20 * 1000, &loop, &QEventLoop::quit);
    room.createRoom();
    loop.exec();
    return rc;
}

int runGui(const CommandLine &cli, QApplication &app, Backend &backend)
{
    NcmClient &client = backend.client;
    Store &store = backend.store;
    AudioEngine &audio = backend.audio;
    Session &session = backend.session;
    Mpris &mpris = backend.mpris;
    // 一起听：房间状态/轮询/播放指令独立于 Store；QML 从 backend.listenTogether 取。
    ListenTogether listenTogether(&client, &store, &audio);

    // 界面是完整客户端：用户点播放就是真实收听，默认允许上报（命令行 smoke 才需要 --allow-write）
    audio.setScrobblingAllowed(true);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    // 托盘只在有窗口的运行方式下创建（--dump-store / --smoke-audio 上面已返回，无窗口）
    Tray tray(&audio);
    // 放歌时系统不睡眠（屏幕照常熄灭、锁屏）；看 MV 时屏幕常亮由 QML 设 backend.power.keepScreenOn
    PowerInhibitor power;
    QObject::connect(&audio, &AudioEngine::playingChanged, &power, [&] { power.setPreventSleep(audio.playing()); });

    store.refresh();   // 首屏数据异步到达，QML 通过 data / personal 的 NOTIFY 自动更新
    // 钥匙串超时后才读出 jar（例如在等用户解锁钱包）：先按匿名起来，读出后再按登录态刷一次
    if (!client.isReady())
        QObject::connect(&client, &NcmClient::ready, &store, &Store::refresh, Qt::SingleShotConnection);
    // QML 的 Settings 要求 organizationName，否则 QSettings 初始化失败（队列/窗口大小无法恢复）。
    // "." 让它与不设组织名时落在同一层：~/.config/yunting.conf，而不会多套一层 yunting/。
    // 放在这里而不是开头：之前构造的 Store / CookieVault / 图片缓存已拿到不带组织名的 XDG 路径。
    const QString imageCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images";
    app.setOrganizationName(QStringLiteral("."));
    // 下载文件用 AppDataLocation；必须在组织/应用名设好之后构造。
    DownloadManager downloads(&client);
    // MV 画面：libmpv 渲染进 QML；原型与截图不注册这个类型，MvPage 退回内置替身
    qmlRegisterType<MvVideo>("org.nephelesound.yunting", 1, 0, "MvVideo");
    QQmlApplicationEngine engine;
    // kirigami-addons（「关于」页、对话框）用 i18nd() 取自己的译文；应用自己的界面不走 i18n
    KLocalization::setupLocalizedContext(&engine);
    ImageCacheFactory imageCache(imageCacheDir);
    engine.setNetworkAccessManagerFactory(&imageCache);
    // main.qml 用 root.backend.{store,audio,session,mpris,tray,layerShell,power,clipboard,diagnostics} 取后端；QVariantMap 的 key 就是属性名
    LayerShellHelper layerShell;
    ClipboardHelper clipboard;
    Diagnostics diagnostics;
    diagnostics.traffic = [&client] { return client.trafficSummary(); };
    QVariantMap backendMap{{"store", QVariant::fromValue<QObject *>(&store)},
                           {"audio", QVariant::fromValue<QObject *>(&audio)},
                           {"session", QVariant::fromValue<QObject *>(&session)},
                           {"mpris", QVariant::fromValue<QObject *>(&mpris)},
                           {"tray", QVariant::fromValue<QObject *>(&tray)},
                           {"layerShell", QVariant::fromValue<QObject *>(&layerShell)},                           {"power", QVariant::fromValue<QObject *>(&power)},
                           {"clipboard", QVariant::fromValue<QObject *>(&clipboard)},
                           {"diagnostics", QVariant::fromValue<QObject *>(&diagnostics)},
                           {"listenTogether", QVariant::fromValue<QObject *>(&listenTogether)},
                           {"downloads", QVariant::fromValue<QObject *>(&downloads)}};
    engine.setInitialProperties({{"backend", backendMap}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // 界面编进资源：单一资源目录（qmldir 的 singleton 声明按同目录解析），
    // Snapshot.qml 与快照数据不编入，backend 注入后它的创建路径不会走到
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/org/nephelesound/yunting/main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // 窗口创建完成后关联托盘，左键从此显示/隐藏主窗口
    tray.setAssociatedWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst()));

    if (cli.parser.isSet(cli.screenshot)) {
        // 等首屏数据与封面到齐再截（真实网络，给足时间）；同步 grabWindow，不依赖 grabToImage 回调
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = cli.parser.value(cli.screenshot);
        QTimer::singleShot(8000, &app, [window, file] {
            const bool saved = window && window->grabWindow().save(file);
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }
    return app.exec();
}

} // namespace

namespace Cli {
int run(QApplication &app)
{
    CommandLine cli;
    cli.parser.process(app);

    if (cli.parser.isSet(cli.verbose) || cli.parser.isSet(cli.screenshot)) {
        // 警告以上一律打印；--verbose 时再打开本应用的 yunting.* 调试分类（每个请求的路径、流量告警等）
        if (cli.parser.isSet(cli.verbose))
            QLoggingCategory::setFilterRules(QStringLiteral("yunting.*.debug=true"));
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
            const bool ours = ctx.category && qstrncmp(ctx.category, "yunting.", 8) == 0;
            if (ours || (type != QtDebugMsg && type != QtInfoMsg))
                std::fprintf(stderr, "%s\n", qPrintable(msg));
        });
    }

    // 接在上面的处理器之后：日志照常输出，另记一份最近的警告给「关于」页的诊断信息
    Diagnostics::install();
    installQuitSignals();

    const bool anonymous = cli.parser.isSet(cli.anonymous);
    NcmClient client(nullptr, !anonymous);
    // 退出登录后的清凭据是异步写（钥匙串）：进程退出前最多等 2 秒把它写完，
    // 否则下一条匿名 Set-Cookie 之前进程就没了，旧 MUSIC_U 会留在钥匙串里。
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &client, [&client] {
        std::fprintf(stderr, "aboutToQuit: flushing credentials\n");
        if (!client.flushCredentials(2000))
            qWarning() << "credential flush timed out; credentials may not be persisted";
    });

    waitForClientReady(client);

    if (cli.parser.isSet(cli.importGtk4Cookies))
        return runImport(client);

    Store store(&client);
    AudioEngine audio(&client);
    if (cli.parser.isSet(cli.eqPreset))
        audio.applyEqPreset(cli.parser.value(cli.eqPreset));
    if (cli.parser.isSet(cli.scrobbleMode))
        audio.setScrobbleMode(cli.parser.value(cli.scrobbleMode));
    // 命令行播放（--smoke-audio / --soak-audio）默认只读；界面启动在 runGui 里打开。
    audio.setScrobblingAllowed(cli.parser.isSet(cli.allowWrite));
    Session session(&client, &store);
    Mpris mpris(&audio, &store);
    // 打卡成功后本地「最近播放」立即更新；服务端数据仍是权威，下次 refresh 会覆盖
    QObject::connect(&audio, &AudioEngine::scrobbled, &store, &Store::notePlayed);

    Backend backend{client, store, audio, session, mpris};

    if (cli.parser.isSet(cli.dumpStore))
        return runDump(cli, backend);
    if (cli.parser.isSet(cli.api))
        return runApi(cli, client);

    if (cli.parser.isSet(cli.like) || cli.parser.isSet(cli.commentLike)
        || cli.parser.isSet(cli.playlistSelfTest)
        || cli.parser.isSet(cli.subscribeSelfTest)
        || cli.parser.isSet(cli.listenTogetherSelfTest)
        || cli.parser.isSet(cli.downloadSelfTest)) {
        if (!cli.parser.isSet(cli.allowWrite)) {
            std::fprintf(stderr, "refusing write commands without --allow-write\n");
            return 1;
        }
    }

    if (cli.parser.isSet(cli.like))
        return runLike(cli, backend);
    if (cli.parser.isSet(cli.commentLike))
        return runCommentLike(cli, backend);
    if (cli.parser.isSet(cli.searchUsers) || cli.parser.isSet(cli.searchRadios))
        return runSearch(cli, backend);
    if (cli.parser.isSet(cli.mv))
        return runMv(cli, backend);
    if (cli.parser.isSet(cli.artistSongs))
        return runArtistSongs(cli, backend);
    if (cli.parser.isSet(cli.comments))
        return runComments(cli, backend);
    if (cli.parser.isSet(cli.detail))
        return runDetail(cli, backend);
    if (cli.parser.isSet(cli.subscribeSelfTest))
        return runSubscribeSelfTest(cli, backend);
    if (cli.parser.isSet(cli.playlistSelfTest))
        return runPlaylistSelfTest(cli, backend);
    if (cli.parser.isSet(cli.soakAudio))
        return runSoak(cli, app, backend);
    if (cli.parser.isSet(cli.storeKey))
        return runStoreKey(cli, backend);
    if (cli.parser.isSet(cli.inbox))
        return runInbox(cli, backend);
    if (cli.parser.isSet(cli.smokeAudio))
        return runSmokeAudio(cli, app, backend);
    if (cli.parser.isSet(cli.listenTogetherSelfTest))
        return runListenTogetherSelfTest(backend);
    if (cli.parser.isSet(cli.refreshSession))
        return runRefreshSession(backend);
    if (cli.parser.isSet(cli.downloadSelfTest))
        return runDownloadSelfTest(cli, backend);
    return runGui(cli, app, backend);
}
} // namespace Cli

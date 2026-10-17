#include "apicli.h"

#include "ncmclient.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>
#include <QUrlQuery>
#include <cstdio>

namespace {
// 调试目录：只读端点的一张表。--api get 接受任意路径，这张表只负责打印清单；
// 新增端点时改这一行，不要把同样的路径再抄到别处。
struct Endpoint {
    const char *name;
    const char *path;
    const char *params;
    bool personal;
};

const Endpoint kEndpoints[] = {
    {"account", "api/nuser/account/get", "", true},
    {"toplist", "api/toplist/detail", "", false},
    {"playlist", "api/v6/playlist/detail", "id n", false},
    {"song-detail", "api/v3/song/detail", "ids", false},
    {"search", "api/search/get", "q type limit offset", false},
    {"artist", "api/v1/artist/<id>", "", false},
    {"artist-songs", "api/v1/artist/songs", "id limit offset order", false},
    {"artist-albums", "api/artist/albums/<id>", "limit offset", false},
    {"album", "api/v1/album/<id>", "", false},
    {"lyrics", "api/song/lyric", "id lv tv", false},
    {"comments", "api/v1/resource/comments/R_SO_4_<id>", "limit offset", false},
    {"hot-comments", "api/v1/resource/hotcomments/R_SO_4_<id>", "limit", false},
    {"song-url", "api/song/enhance/player/url", "ids br", true},
    {"recent", "api/play-record/song/list", "limit", true},
    {"cloud", "api/v1/cloud/get", "limit offset", true},
    {"daily", "api/v3/discovery/recommend/songs", "", true},
    {"recommend-resource", "api/v1/discovery/recommend/resource", "", true},
    {"radar", "api/v6/playlist/detail", "id=3136952023 n=1000", true},
    {"fm", "api/v1/radio/get", "", true},
    {"similar", "api/v1/discovery/simiSong", "songid limit", false},
};

void printInventory()
{
    std::fputs("只读 GET：--api get <path> [key=value...] [--api-personal]\n\n", stdout);
    for (const Endpoint &e : kEndpoints) {
        std::printf("  %-22s %-42s %s%s\n", e.name, e.path, e.params,
                    e.personal ? "  (personal)" : "");
    }
    std::fputs("\n写操作不走通用 GET：\n"
               "  --like <songId> on|off\n"
               "  --comment-like <songId>:<commentId> on|off\n"
               "  --playlist-selftest\n\n"
               "示例：\n"
               "  yunting --api get api/v1/artist/6452\n"
               "  yunting --api get api/search/get q=林俊杰 type=1 limit=5\n"
               "  yunting --api get api/nuser/account/get --api-personal\n",
               stdout);
}
} // namespace

int runApiCommand(NcmClient &client, const QString &command, const QStringList &args, bool personal)
{
    if (command == QLatin1String("list")) {
        printInventory();
        return 0;
    }
    if (command != QLatin1String("get")) {
        std::fprintf(stderr, "unknown api command: %s (use --api list)\n", qPrintable(command));
        return 1;
    }

    const QString path = args.value(0);
    if (path.isEmpty()) {
        std::fprintf(stderr, "usage: --api get <path> [key=value...]\n");
        return 1;
    }

    QUrlQuery query;
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        const qsizetype eq = arg.indexOf(u'=');
        if (eq <= 0) {
            std::fprintf(stderr, "bad parameter: %s (expected key=value)\n", qPrintable(arg));
            return 1;
        }
        query.addQueryItem(arg.left(eq), arg.mid(eq + 1));
    }

    int rc = 1;
    QEventLoop loop;
    QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
    client.get(path, query, personal, [&](const QJsonDocument &doc, bool ok) {
        if (!ok) {
            std::fprintf(stderr, "request failed: %s\n", qPrintable(path));
        } else {
            const QByteArray json = doc.toJson(QJsonDocument::Indented);
            std::fwrite(json.constData(), 1, json.size(), stdout);
            rc = 0;
        }
        loop.quit();
    });
    loop.exec();
    return rc;
}

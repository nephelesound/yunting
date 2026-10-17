// 新版歌词：逐字 yrc / klyric / romalrc + 普通 LRC。只读 GET。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

void Store::requestLyricsNew(double songId)
{
    if (songId <= 0)
        return;
    m_client->get(QStringLiteral("api/song/lyric/v1"),
                  query({{"id", num(songId)}, {"cp", "false"}, {"tv", "0"}, {"lv", "0"},
                         {"rv", "0"}, {"kv", "0"}, {"yv", "0"}, {"ytv", "0"}, {"yrv", "0"}}),
                  true,
                  [this, songId](const QJsonDocument &doc, bool ok) {
        const QJsonObject root = body(doc);
        if (!ok || root.value("code").toInt() != 200) {
            emit requestFailed(QStringLiteral("lyricsNew"), QStringLiteral("暂时取不到逐字歌词"));
            return;
        }
        const QString lrc = root.value("lrc").toObject().value("lyric").toString();
        const QString tlyric = root.value("tlyric").toObject().value("lyric").toString();
        put("lyricsNew", QVariantMap{
            {"songId", songId},
            {"lrc", lrc},
            {"tlyric", tlyric},
            {"yrc", root.value("yrc").toObject().value("lyric").toString()},
            {"klyric", root.value("klyric").toObject().value("lyric").toString()},
            {"romalrc", root.value("romalrc").toObject().value("lyric").toString()},
            {"lines", ncmParseLrc(lrc, tlyric)},
        }, false);
    });
}

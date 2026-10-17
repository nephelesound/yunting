// 新版歌词：逐字 yrc 解析成与普通歌词相同的行，没有 yrc 时退回 LRC。只读 GET。
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
            emit requestFailed(QStringLiteral("lyricsNew:%1").arg(num(songId)),
                               QStringLiteral("暂时取不到逐字歌词"));
            return;
        }
        const QString lrc = root.value("lrc").toObject().value("lyric").toString();
        const QString tlyric = root.value("tlyric").toObject().value("lyric").toString();
        const QString yrc = root.value("yrc").toObject().value("lyric").toString();
        // 逐字歌词有自己的译文 ytlrc，时间戳与 yrc 的行对齐；tlyric 对的是 LRC 的行，与 yrc 对不上
        QString ytlrc = root.value("ytlrc").toObject().value("lyric").toString();
        if (ytlrc.isEmpty())
            ytlrc = tlyric;
        put("lyricsNew", QVariantMap{
            {"songId", songId},
            {"lines", yrc.isEmpty() ? ncmParseLrc(lrc, tlyric) : ncmParseYrc(yrc, ytlrc)},
        }, false);
    });
}

// 新版歌词：逐字 yrc 解析成与普通歌词相同的行，没有 yrc 时退回 LRC。只读 GET。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "sdk/lyrics.h"

#include <QJsonDocument>

using namespace ncm;

void Store::requestLyricsNew(double songId)
{
    if (songId <= 0)
        return;
    const QString key = QStringLiteral("lyricsNew:") + num(songId);
    const quint64 token = m_requests.begin(QStringLiteral("lyricsNew"));
    m_cache.query(key,
        [this, songId](QueryCache::Done done) {
            // 逐字歌词是增强项：公开接口，不依赖登录 cookie；失败由 QueryCache 统一收口，
            // 不弹全局错误（普通歌词仍可用，页面也不会因此显示「没取到」）。
            m_client->get(QStringLiteral("api/song/lyric/v1"),
                          query({{"id", num(songId)}, {"cp", "false"}, {"tv", "0"}, {"lv", "0"},
                                 {"rv", "0"}, {"kv", "0"}, {"yv", "0"}, {"ytv", "0"}, {"yrv", "0"}}),
                          false,
                          [done, songId](const QJsonDocument &doc, bool ok) {
                if (!ok) {
                    done({}, false);
                    return;
                }
                const auto parsed =
                    sdk::parseSongLyrics(body(doc), static_cast<qint64>(songId));
                if (!parsed) {
                    done({}, false);
                    return;
                }
                const sdk::SongLyrics &lyrics = *parsed.value;
                // 逐字歌词有自己的译文 ytlrc，时间戳与 yrc 的行对齐；tlyric 对的是 LRC 的行，与 yrc 对不上
                const QString ytlrc =
                    lyrics.ytlrc.isEmpty() ? lyrics.tlyric : lyrics.ytlrc;
                done(QVariantMap{
                    {"songId", songId},
                    {"lines", lyrics.yrc.isEmpty()
                                  ? ncmParseLrc(lyrics.lrc, lyrics.tlyric)
                                  : ncmParseYrc(lyrics.yrc, ytlrc)},
                }, true);
            });
        },
        [this, token](const QVariant &lyrics) {
            if (m_requests.current(QStringLiteral("lyricsNew"), token))
                put("lyricsNew", lyrics, false);
        });
}

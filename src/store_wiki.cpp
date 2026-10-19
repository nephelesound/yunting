// 百科的 Store 接缝：SDK 负责三个请求的合并与 typed 解析，这里只做缓存和 QML 兼容形状。
#include "store.h"

#include "json_util.h"
#include "sdk/song_wiki_service.h"

using namespace ncm;

void Store::requestWiki(const QString &key, double songId)
{
    if (!(songId > 0))
        return;

    const QString slot = QStringLiteral("wiki:") + num(songId);
    const quint64 token = m_requests.begin(slot);
    m_cache.query(
        key,
        [this, songId](QueryCache::Done done) {
            sdk::fetchSongWiki(*m_client, {static_cast<qint64>(songId)}, this,
                               [done](sdk::Result<sdk::SongWiki> result) {
                if (!result) {
                    done({}, false);
                    return;
                }
                done(result.value->toVariantMap(), true);
            });
        },
        [this, slot, token, songId](const QVariant &value) {
            if (m_requests.current(slot, token))
                putKeyed("wiki", num(songId), value);
        });
}

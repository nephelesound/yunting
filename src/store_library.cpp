// 收藏列表：歌手 / 专辑 / MV+视频。账号绑定、只读；分页请求沿用第一页的代号。
// 列表每次向服务器取，不做缓存：收藏/取关后下一次请求必须看到服务器的新状态。
#include "store.h"

#include "sdk/library.h"

using namespace ncm;

namespace {

constexpr int kLibraryPageSize = 25;

// 失败按请求时的 key 报（第一页不带 offset），界面才能对上这一页的重试
QString libraryKey(const char *kind, int offset)
{
    const QString base = QStringLiteral("library:") + QLatin1String(kind);
    return offset > 0 ? base + u':' + QString::number(offset) : base;
}

} // namespace

void Store::requestSavedArtists(int offset)
{
    if (!m_client->hasMusicU() || offset < 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("savedArtists"))
                                     : m_requests.begin(QStringLiteral("savedArtists"));
    sdk::fetchSubscribedArtists(*m_client, kLibraryPageSize, offset, this,
                                [this, offset, epoch, token]
                                (sdk::Result<sdk::SubscribedArtistPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("savedArtists"), token))
            return;
        if (!result) {
            emit requestFailed(libraryKey("artists", offset),
                               requestFailure(result.error, QStringLiteral("暂时取不到收藏的歌手")));
            return;
        }
        const sdk::SubscribedArtistPage &page = *result.value;
        QVariantList items;
        if (offset > 0)
            items = m_personal.value("savedArtists").toMap().value("items").toList();
        for (const sdk::SubscribedArtist &artist : page.items) {
            items.append(QVariantMap{
                {"id", static_cast<double>(artist.id)},
                {"name", artist.name},
                {"cover", artist.cover},
                // 与歌手页一致：别名只取第一条，界面显示一个译名/原名。
                {"alias", artist.alias.isEmpty() ? QString() : artist.alias.first()},
                {"albums", static_cast<double>(artist.albumSize)},
                {"mvs", static_cast<double>(artist.mvSize)},
            });
        }
        put("savedArtists", QVariantMap{
            {"offset", offset},
            {"more", page.hasMore},
            {"count", static_cast<double>(page.count)},
            {"items", items},
        }, true);
    });
}

void Store::requestSavedAlbums(int offset)
{
    if (!m_client->hasMusicU() || offset < 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("savedAlbums"))
                                     : m_requests.begin(QStringLiteral("savedAlbums"));
    sdk::fetchSubscribedAlbums(*m_client, kLibraryPageSize, offset, this,
                               [this, offset, epoch, token]
                               (sdk::Result<sdk::SubscribedAlbumPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("savedAlbums"), token))
            return;
        if (!result) {
            emit requestFailed(libraryKey("albums", offset),
                               requestFailure(result.error, QStringLiteral("暂时取不到收藏的专辑")));
            return;
        }
        const sdk::SubscribedAlbumPage &page = *result.value;
        QVariantList items;
        if (offset > 0)
            items = m_personal.value("savedAlbums").toMap().value("items").toList();
        for (const sdk::SubscribedAlbum &album : page.items) {
            items.append(QVariantMap{
                {"id", static_cast<double>(album.id)},
                {"name", album.name},
                {"cover", album.cover},
                {"artist", album.artistName},
                {"artistId", static_cast<double>(album.artistId)},
                {"size", static_cast<double>(album.size)},
            });
        }
        put("savedAlbums", QVariantMap{
            {"offset", offset},
            {"more", page.hasMore},
            {"count", static_cast<double>(page.count)},
            {"items", items},
        }, true);
    });
}

void Store::requestSavedMvs(int offset)
{
    if (!m_client->hasMusicU() || offset < 0)
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = offset > 0 ? m_requests.token(QStringLiteral("savedMvs"))
                                     : m_requests.begin(QStringLiteral("savedMvs"));
    sdk::fetchSubscribedMvs(*m_client, kLibraryPageSize, offset, this,
                            [this, offset, epoch, token]
                            (sdk::Result<sdk::SubscribedMvPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("savedMvs"), token))
            return;
        if (!result) {
            emit requestFailed(libraryKey("mvs", offset),
                               requestFailure(result.error, QStringLiteral("暂时取不到收藏的 MV")));
            return;
        }
        const sdk::SubscribedMvPage &page = *result.value;
        QVariantList items;
        if (offset > 0)
            items = m_personal.value("savedMvs").toMap().value("items").toList();
        for (const sdk::SubscribedMv &mv : page.items) {
            // SDK type: 0 = MV（id 是数字），1 = 视频（id 是 vid 字符串）。
            const bool video = mv.type == 1;
            items.append(QVariantMap{
                {"kind", video ? QStringLiteral("video") : QStringLiteral("mv")},
                {"id", video ? 0.0 : static_cast<double>(mv.id.toLongLong())},
                {"vid", video ? mv.id : QString()},
                {"name", mv.name},
                {"cover", mv.cover},
                {"artist", mv.creatorName},
                {"duration", static_cast<double>(mv.duration)},
                {"playCount", static_cast<double>(mv.playCount)},
            });
        }
        put("savedMvs", QVariantMap{
            {"offset", offset},
            {"more", page.hasMore},
            {"count", static_cast<double>(page.count)},
            {"items", items},
        }, true);
    });
}

// Store 的歌单写操作：创建 / 收藏 / 加歌 / 移歌 / 改名 / 删除 / 排序。
// 服务器成功后再同步本地 personal；失败只发 playlistActionFailed，不改本地。
#include "store.h"

#include "json_util.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#include <algorithm>

using namespace ncm;

namespace {
// JS encodeURI keeps reserved URL characters; QUrl::toPercentEncoding would
// encode "rpc://" and "https://" into an unusable string.
QString encodeUri(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(
        value, QByteArray(";/?:@&=+$,#-_.!~*'()")));
}
} // namespace

void Store::playlistWrite(const QString &path, const QJsonObject &payload, const QString &action,
                          std::function<void(const QJsonObject &)> onSuccess)
{
    if (!m_client->hasMusicU()) {
        emit playlistActionFailed(action, QStringLiteral("登录后才能修改歌单"));
        return;
    }
    const quint64 epoch = m_epoch;
    m_client->postWeapi(path, payload, true,
                        [this, epoch, action, onSuccess](const NcmClient::Response &response) {
        if (epoch != m_epoch)   // 期间退出登录：不要改新会话的数据
            return;
        const QString failure = requestFailure(response);
        if (failure.isEmpty()) {
            onSuccess(response.doc.object());
            return;
        }
        emit playlistActionFailed(action, failure);
    });
}

void Store::createPlaylist(const QString &name, bool privatePlaylist)
{
    const QJsonObject payload{{"name", name},
                              {"privacy", privatePlaylist ? QStringLiteral("10") : QStringLiteral("0")},
                              {"type", QStringLiteral("NORMAL")}};
    playlistWrite(QStringLiteral("playlist/create"), payload, QStringLiteral("创建歌单"),
                  [this, name](const QJsonObject &resp) {
        double id = resp.value("id").toDouble();
        if (id <= 0)
            id = resp.value("playlist").toObject().value("id").toDouble();
        if (id <= 0) {
            emit playlistActionFailed(QStringLiteral("创建歌单"), QStringLiteral("接口没有返回歌单 id"));
            return;
        }
        QVariantMap card{{"id", id}, {"name", name}, {"cover", QString{}},
                         {"count", 0.0}, {"reason", QString{}}};
        QVariantList mine = m_personal.value("mine").toList();
        mine.prepend(card);
        put("mine", mine, true);
        emit playlistCreated(id, name);
    });
}

void Store::setPlaylistSubscribed(const QVariantMap &playlist, bool subscribed)
{
    const double id = playlist.value("id").toDouble();
    if (!(id > 0))
        return;
    const QString action = subscribed ? QStringLiteral("收藏歌单") : QStringLiteral("取消收藏歌单");
    debounceWrite(QStringLiteral("subscribe:playlist:") + num(id), [this, id, playlist, subscribed, action] {
        playlistWrite(subscribed ? QStringLiteral("playlist/subscribe") : QStringLiteral("playlist/unsubscribe"),
                  QJsonObject{{"id", num(id)}}, action,
                  [this, id, playlist, subscribed](const QJsonObject &) {
        QVariantList lists = m_personal.value("subscribed").toList();
        for (qsizetype i = lists.size() - 1; i >= 0; --i)
            if (lists.at(i).toMap().value("id").toDouble() == id)
                lists.removeAt(i);
        if (subscribed)
            lists.prepend(playlist);
        put("subscribed", lists, true);
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
    });
}

void Store::addSongToPlaylist(double playlistId, double songId)
{
    // /playlist/track/add 对新歌单会回 401；manipulate/tracks 是旧版但当前账号可用。
    playlistWrite(QStringLiteral("playlist/manipulate/tracks"),
                  QJsonObject{{"op", QStringLiteral("add")},
                              {"pid", num(playlistId)},
                              {"trackIds", QStringLiteral("[%1]").arg(num(songId))},
                              {"imme", QStringLiteral("true")}},
                  QStringLiteral("添加歌曲"), [this, playlistId, songId](const QJsonObject &) {
        const QVariantMap track = trackById(songId);
        bool stale = false;

        const auto addToCards = [this, playlistId, songId, track, &stale](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() != playlistId)
                    continue;
                QVariantList list = card.value("tracks").toList();
                bool present = false;
                for (const QVariant &t : list)
                    present = present || t.toMap().value("id").toDouble() == songId;
                if (present)
                    continue;   // 服务器端的幂等请求：本地没有新增，计数也不能动
                // 卡片可能只带了曲目数、没带曲目（首屏预览）：不要把列表变成“只有这一首”，
                // 保持空表让 Source 按需取完整曲目。空歌单（count=0）可以安全放入第一首。
                if (!track.isEmpty() && (!list.isEmpty() || card.value("count").toDouble() == 0)) {
                    list.append(track);
                    card.insert("tracks", list);
                    card.insert("count", card.value("count").toDouble() + 1);
                    v = card;
                    changed = true;
                } else {
                    stale = true;   // 缺曲目信息：交给重新取详情，不盲改计数
                }
            }
            if (changed)
                put(key, cards, true);
        };
        addToCards("playlists");
        addToCards("mine");
        addToCards("subscribed");

        QVariantMap details = m_personal.value("playlistDetails").toMap();
        const QString key = num(playlistId);
        if (details.contains(key)) {
            QVariantMap detail = details.value(key).toMap();
            QVariantList list = detail.value("tracks").toList();
            bool present = false;
            for (const QVariant &t : list)
                present = present || t.toMap().value("id").toDouble() == songId;
            if (!present) {
                if (!track.isEmpty()) {
                    list.append(track);
                    detail.insert("tracks", list);
                    detail.insert("count", std::max(0.0, detail.value("count").toDouble()) + 1);
                    details.insert(key, detail);
                    put("playlistDetails", details, true);
                } else {
                    stale = true;
                }
            }
        }
        // 缓存里的详情是写操作前的快照：失效，下一次按需请求重新取
        m_cache.forget(QStringLiteral("playlist:") + key);
        if (stale)
            requestPlaylist(QStringLiteral("playlist:") + key, playlistId);
        emit playlistChanged(playlistId);
    });
}

void Store::removeSongFromPlaylist(double playlistId, double songId)
{
    playlistWrite(QStringLiteral("playlist/manipulate/tracks"),
                  QJsonObject{{"op", QStringLiteral("del")},
                              {"pid", num(playlistId)},
                              {"trackIds", QStringLiteral("[%1]").arg(num(songId))},
                              {"imme", QStringLiteral("true")}},
                  QStringLiteral("移出歌单"), [this, playlistId, songId](const QJsonObject &) {
        const auto removeFromCards = [this, playlistId, songId](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() != playlistId)
                    continue;
                QVariantList list = card.value("tracks").toList();
                bool removed = false;
                for (qsizetype i = list.size() - 1; i >= 0; --i) {
                    if (list.at(i).toMap().value("id").toDouble() == songId) {
                        list.removeAt(i);
                        removed = true;
                    }
                }
                if (!removed)
                    continue;   // 列表里本来就没有：计数不能凭空减一
                card.insert("tracks", list);
                card.insert("count", std::max(0.0, card.value("count").toDouble() - 1));
                v = card;
                changed = true;
            }
            if (changed)
                put(key, cards, true);
        };
        removeFromCards("playlists");
        removeFromCards("mine");
        removeFromCards("subscribed");

        QVariantMap details = m_personal.value("playlistDetails").toMap();
        const QString key = num(playlistId);
        if (details.contains(key)) {
            QVariantMap detail = details.value(key).toMap();
            QVariantList list = detail.value("tracks").toList();
            bool removed = false;
            for (qsizetype i = list.size() - 1; i >= 0; --i) {
                if (list.at(i).toMap().value("id").toDouble() == songId) {
                    list.removeAt(i);
                    removed = true;
                }
            }
            if (removed) {
                detail.insert("tracks", list);
                detail.insert("count", std::max(0.0, detail.value("count").toDouble() - 1));
                details.insert(key, detail);
                put("playlistDetails", details, true);
            }
        }
        // 缓存里的详情是移除前的快照：下一次按需请求必须重新取
        m_cache.forget(QStringLiteral("playlist:") + num(playlistId));
        emit playlistChanged(playlistId);
    });
}

void Store::renamePlaylist(double id, const QString &name)
{
    playlistWrite(QStringLiteral("playlist/update/name"), QJsonObject{{"id", num(id)}, {"name", name}},
                  QStringLiteral("重命名歌单"), [this, id, name](const QJsonObject &) {
        const auto renameIn = [this, id, name](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() == id) {
                    card.insert("name", name);
                    v = card;
                    changed = true;
                }
            }
            if (changed)
                put(key, cards, true);
        };
        renameIn("playlists");
        renameIn("mine");
        renameIn("subscribed");
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        if (details.contains(num(id))) {
            QVariantMap detail = details.value(num(id)).toMap();
            detail.insert("name", name);
            details.insert(num(id), detail);
            put("playlistDetails", details, true);
        }
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
}

void Store::setPlaylistDescription(double id, const QString &description)
{
    playlistWrite(QStringLiteral("playlist/desc/update"),
                  QJsonObject{{"id", num(id)}, {"desc", description}},
                  QStringLiteral("更新歌单描述"), [this, id, description](const QJsonObject &) {
        const auto updateIn = [this, id, description](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() == id) {
                    card.insert("description", description);
                    v = card;
                    changed = true;
                }
            }
            if (changed)
                put(key, cards, true);
        };
        updateIn("playlists");
        updateIn("mine");
        updateIn("subscribed");
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        if (details.contains(num(id))) {
            QVariantMap detail = details.value(num(id)).toMap();
            detail.insert("description", description);
            details.insert(num(id), detail);
            put("playlistDetails", details, true);
        }
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
}

void Store::setPlaylistPrivacy(double id, bool privatePlaylist)
{
    const QString privacy = privatePlaylist ? QStringLiteral("10") : QStringLiteral("0");
    playlistWrite(QStringLiteral("playlist/update/privacy"),
                  QJsonObject{{"id", num(id)}, {"privacy", privacy}},
                  QStringLiteral("修改歌单隐私"), [this, id, privacy](const QJsonObject &) {
        const auto updateIn = [this, id, privacy](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() == id) {
                    card.insert("privacy", privacy);
                    v = card;
                    changed = true;
                }
            }
            if (changed)
                put(key, cards, true);
        };
        updateIn("playlists");
        updateIn("mine");
        updateIn("subscribed");
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        if (details.contains(num(id))) {
            QVariantMap detail = details.value(num(id)).toMap();
            detail.insert("privacy", privacy);
            details.insert(num(id), detail);
            put("playlistDetails", details, true);
        }
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
}

void Store::setPlaylistTags(double id, const QStringList &tags)
{
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString value = tag.trimmed();
        if (!value.isEmpty())
            cleaned.append(value);
    }
    const QString joined = cleaned.join(QLatin1Char(','));
    playlistWrite(QStringLiteral("playlist/tags/update"),
                  QJsonObject{{"id", num(id)}, {"tags", joined}},
                  QStringLiteral("更新歌单标签"), [this, id, cleaned](const QJsonObject &) {
        QVariantList values;
        for (const QString &tag : cleaned)
            values.append(tag);
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        if (details.contains(num(id))) {
            QVariantMap detail = details.value(num(id)).toMap();
            detail.insert("tags", values);
            details.insert(num(id), detail);
            put("playlistDetails", details, true);
        }
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
}

void Store::startPlaylistImport(const QString &playlistName, const QJsonArray &songs)
{
    const QString serialized = QString::fromUtf8(
        QJsonDocument(songs).toJson(QJsonDocument::Compact));
    playlistWrite(QStringLiteral("playlist/import/name/task/create"),
                  QJsonObject{{QStringLiteral("songs"), serialized},
                              {QStringLiteral("playlistName"), playlistName}},
                  QStringLiteral("导入歌单"), [this](const QJsonObject &resp) {
        const double taskId = resp.value(QStringLiteral("data")).toObject()
                                  .value(QStringLiteral("taskId")).toDouble();
        if (taskId <= 0) {
            emit playlistActionFailed(QStringLiteral("导入歌单"),
                                      QStringLiteral("接口没有返回任务 id"));
            return;
        }
        emit playlistImportStarted(taskId);
    });
}

void Store::importPlaylistFromText(const QString &text, const QString &name)
{
    if (text.trimmed().isEmpty()) {
        emit playlistActionFailed(QStringLiteral("导入歌单"), QStringLiteral("导入内容为空"));
        return;
    }
    const QString playlistName = name.trimmed().isEmpty()
        ? QStringLiteral("导入音乐 %1").arg(QDateTime::currentDateTime().toString(
              QStringLiteral("yyyy-MM-dd HH:mm")))
        : name.trimmed();
    const QString encoded = encodeUri(QStringLiteral("rpc://playlist/import?text=") + text);
    QJsonArray songs;
    songs.append(QJsonObject{{QStringLiteral("name"), playlistName},
                             {QStringLiteral("type"), QString()},
                             {QStringLiteral("url"), encoded}});
    startPlaylistImport(playlistName, songs);
}

void Store::importPlaylistFromLinks(const QVariantList &links, const QString &name)
{
    QStringList urls;
    for (const QVariant &value : links) {
        const QString url = value.toString().trimmed();
        if (!url.isEmpty())
            urls.append(url);
    }
    if (urls.isEmpty()) {
        emit playlistActionFailed(QStringLiteral("导入歌单"), QStringLiteral("没有可导入的链接"));
        return;
    }

    const QString playlistName = name.trimmed().isEmpty()
        ? QStringLiteral("导入音乐 %1").arg(QDateTime::currentDateTime().toString(
              QStringLiteral("yyyy-MM-dd HH:mm")))
        : name.trimmed();
    QJsonArray songs;
    for (const QString &url : std::as_const(urls)) {
        songs.append(QJsonObject{
            {QStringLiteral("name"), playlistName},
            {QStringLiteral("type"), QString()},
            {QStringLiteral("url"), encodeUri(url)},
        });
    }
    startPlaylistImport(playlistName, songs);
}

void Store::deletePlaylist(double id)
{
    const QString ids = QStringLiteral("[%1]").arg(num(id));
    playlistWrite(QStringLiteral("playlist/remove"), QJsonObject{{"ids", ids}},
                  QStringLiteral("删除歌单"), [this, id](const QJsonObject &) {
        const auto removeFrom = [this, id](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (qsizetype i = cards.size() - 1; i >= 0; --i) {
                if (cards.at(i).toMap().value("id").toDouble() == id) {
                    cards.removeAt(i);
                    changed = true;
                }
            }
            if (changed)
                put(key, cards, true);
        };
        removeFrom("playlists");
        removeFrom("mine");
        removeFrom("subscribed");
        QVariantMap details = m_personal.value("playlistDetails").toMap();
        details.remove(num(id));
        put("playlistDetails", details, true);
        m_cache.forget(QStringLiteral("playlist:") + num(id));
        emit playlistChanged(id);
    });
}

void Store::reorderPlaylistTracks(double playlistId, const QVariantList &trackIds)
{
    QStringList encodedIds;
    encodedIds.reserve(trackIds.size());
    for (const QVariant &v : trackIds)
        encodedIds.append(num(v.toDouble()));
    const QString encoded = QLatin1Char('[') + encodedIds.join(u',') + QLatin1Char(']');

    playlistWrite(QStringLiteral("playlist/manipulate/tracks"),
                  QJsonObject{{"op", QStringLiteral("update")},
                              {"pid", num(playlistId)},
                              {"trackIds", encoded}},
                  QStringLiteral("调整歌曲顺序"), [this, playlistId, trackIds](const QJsonObject &) {
        const auto reorder = [&trackIds](const QVariantList &source) {
            QVariantList ordered;
            ordered.reserve(source.size());
            for (const QVariant &wanted : trackIds) {
                for (const QVariant &v : source) {
                    if (v.toMap().value("id").toDouble() == wanted.toDouble()) {
                        ordered.append(v);
                        break;
                    }
                }
            }
            for (const QVariant &v : source) {
                bool seen = false;
                for (const QVariant &x : ordered)
                    seen = seen || x.toMap().value("id").toDouble() == v.toMap().value("id").toDouble();
                if (!seen)
                    ordered.append(v);
            }
            return ordered;
        };

        const auto reorderCard = [this, playlistId, &reorder](const char *key) {
            QVariantList cards = m_personal.value(QLatin1String(key)).toList();
            bool changed = false;
            for (QVariant &v : cards) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() != playlistId)
                    continue;
                if (!card.value("tracks").toList().isEmpty())
                    card.insert("tracks", reorder(card.value("tracks").toList()));
                v = card;
                changed = true;
            }
            if (changed)
                put(key, cards, true);
        };
        reorderCard("playlists");
        reorderCard("mine");
        reorderCard("subscribed");

        QVariantMap details = m_personal.value("playlistDetails").toMap();
        const QString key = num(playlistId);
        if (details.contains(key)) {
            QVariantMap detail = details.value(key).toMap();
            detail.insert("tracks", reorder(detail.value("tracks").toList()));
            details.insert(key, detail);
            put("playlistDetails", details, true);
        }
        // 详情缓存里的顺序已经过时：下次从歌单页重新进入必须重新取
        m_cache.forget(QStringLiteral("playlist:") + key);
        emit playlistChanged(playlistId);
    });
}

void Store::reorderPlaylists(const QVariantList &ids)
{
    QJsonArray order;
    for (const QVariant &v : ids)
        order.append(v.toDouble());
    const QString encoded = QString::fromUtf8(QJsonDocument(order).toJson(QJsonDocument::Compact));
    playlistWrite(QStringLiteral("playlist/order/update"), QJsonObject{{"ids", encoded}},
                  QStringLiteral("调整歌单顺序"), [this, ids](const QJsonObject &) {
        QVariantList mine = m_personal.value("mine").toList();
        QVariantList ordered;
        for (const QVariant &wanted : ids) {
            for (const QVariant &v : mine) {
                if (v.toMap().value("id").toDouble() == wanted.toDouble()) {
                    ordered.append(v);
                    break;
                }
            }
        }
        for (const QVariant &v : mine) {
            bool seen = false;
            for (const QVariant &x : ordered)
                seen = seen || x.toMap().value("id").toDouble() == v.toMap().value("id").toDouble();
            if (!seen)
                ordered.append(v);
        }
        put("mine", ordered, true);
        emit playlistChanged(0);
    });
}

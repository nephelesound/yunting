#include "store.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"

#include <QDate>
#include <QEventLoop>
#include <algorithm>
#include <cmath>
#include <QHash>
#include <QNetworkInformation>
#include <QStandardPaths>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>

using namespace ncm;

namespace {
constexpr double HotChart = 3778678;
constexpr double Radar = 3136952023;

QVariant nullVariant() { return QVariant::fromValue(nullptr); }
} // namespace

Store::Store(NcmClient *client, QObject *parent)
    : QObject(parent), m_client(client),
      m_cache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/queries.json")
{
    // 骨架先就位：形状永远完整，QML 绑定不会踩到缺键（值本身仍可为 null / 空表）
    m_data = QVariantMap{
        {"toplists", QVariantList{}},
        {"playlist", QVariantMap{{"id", 0.0}, {"name", QString{}}, {"cover", QString{}},
                                 {"description", QString{}}, {"count", 0.0}, {"plays", 0.0}}},
        {"tracks", QVariantList{}},
        {"lyricsTrackId", 0.0},
        {"lyrics", QVariantList{}},
        {"search", QVariantMap{{"query", QString{}}, {"songs", QVariantList{}},
                               {"artists", QVariantList{}}, {"playlists", QVariantList{}},
                               {"albums", QVariantList{}}, {"mvs", QVariantList{}}}},
        {"hotSearches", QVariantList{}},
        {"comments", QVariantMap{{"songId", 0.0}, {"items", QVariantList{}}}},
        // 全部评论分页：{ songId, offset, total, more, items }；初始 null
        {"commentPage", nullVariant()},
        {"searchUsers", nullVariant()},
        {"searchRadios", nullVariant()},
        // 按需加载：歌手页 / 歌手全部歌曲 / 专辑页；初始为 null，结构与 snapshot.py 保持一致
        {"artist", nullVariant()},
        {"artistSongs", nullVariant()},
        {"album", nullVariant()},
        {"mv", nullVariant()},
    };
    m_personal = QVariantMap{
        {"account", nullVariant()},
        {"daily", nullVariant()},
        {"heart", nullVariant()},
        {"radar", nullVariant()},
        {"liked", nullVariant()},
        {"likedIds", QVariantList{}},
        {"mine", QVariantList{}},
        {"recent", nullVariant()},
        {"cloud", nullVariant()},
        {"subscribed", QVariantList{}},
        {"playlists", QVariantList{}},
        {"fm", QVariantMap{{"tracks", QVariantList{}}}},
        {"similar", QVariantMap{{"seed", nullVariant()}, {"tracks", QVariantList{}}}},
        {"playlistDetails", QVariantMap{}},
    };
    m_emptyPersonal = m_personal;
    restoreScreen();

    connect(m_client, &NcmClient::requestFinished, this, &Store::requestFinished);
    // 系统说网络恢复（离线→在线沿）：还上离线期间欠的那次刷新
    if (QNetworkInformation::loadDefaultBackend()) {
        m_netInfo = QNetworkInformation::instance();
        connect(m_netInfo, &QNetworkInformation::reachabilityChanged, this, &Store::connectionRegained);
        if (m_netInfo->reachability() == QNetworkInformation::Reachability::Disconnected)
            setOnline(false);
    }
}

// 离线→在线：QNetworkInformation 报告恢复，或（后端不可用时）下一个请求成功。
// m_refreshOwed 保证每次离线期间只补拉一次，两个来源谁先到谁还，不循环刷新。
void Store::connectionRegained()
{
    if (!m_netInfo || m_netInfo->reachability() != QNetworkInformation::Reachability::Online)
        return;
    if (m_online) {
        // 迟到的 Online 沿（请求成功已把 online 翻回 true）：按欠账处理，不重复刷
        if (m_refreshOwed) {
            m_refreshOwed = false;
            refresh();
        }
        return;
    }
    setOnline(true);
    m_refreshOwed = false;
    refresh();
}

// 上次的首屏值先放上（只放骨架里有的键，旧版本缓存里多出来的字段不带进来）。
// 个人部分按会话分开存：登录与匿名各一份，互不覆盖；换了账号由 fetchAccount 发现并清掉
QString Store::personalKey() const
{
    return m_client->hasMusicU() ? QStringLiteral("screen:personal") : QStringLiteral("screen:personal:anon");
}

void Store::restoreScreen()
{
    const auto overlay = [](QVariantMap &into, const QVariantMap &cached) {
        for (auto it = cached.constBegin(); it != cached.constEnd(); ++it)
            if (into.contains(it.key()))
                into.insert(it.key(), it.value());
    };
    overlay(m_data, m_cache.peek("screen:data").toMap());
    overlay(m_personal, m_cache.peek(personalKey()).toMap());
}

void Store::put(const char *key, const QVariant &value, bool personalSide)
{
    if (personalSide) {
        m_personal.insert(QLatin1String(key), value);
        m_cache.remember(personalKey(), m_personal);
        emit personalChanged();
    } else {
        m_data.insert(QLatin1String(key), value);
        m_cache.remember("screen:data", m_data);
        emit dataChanged();
    }
}

void Store::refresh()
{
    // 种子可能换人（匿名热歌第一首 → 登录后红心第一首），重新竞速一次
    m_similarFired = false;
    m_similarSeed.clear();
    fetchAnonymous();
    fetchPublicPersonal();   // 漫游是公共接口：匿名也取（带 cookie 时是个人的）
    if (m_client->hasMusicU())
        fetchAccount();      // 账号有效就走个人链；无效时在回调里退回匿名推荐歌单
    else
        fetchAnonymousPlaylists();
}

// 红心是唯一的常用写操作：先改本地（界面立刻响应），服务器拒绝时改回。
// 与歌单/评论不同，失败不需要用户重试按钮：下一次点击会重新发。
void Store::setLiked(double songId, bool liked)
{
    if (!m_client->hasMusicU()) {
        emit likeFailed(songId, liked, QStringLiteral("登录后才能同步红心"));
        return;
    }
    if (m_likePending.contains(songId))
        return;

    const QVariantList before = m_personal.value("likedIds").toList();
    bool currentlyLiked = false;
    for (const QVariant &v : before)
        currentlyLiked = currentlyLiked || v.toDouble() == songId;
    if (currentlyLiked == liked) {
        emit likeSucceeded(songId, liked);
        return;
    }

    // 保持原有顺序：过滤掉这首歌，目标为喜欢时追加到末尾。
    const auto withState = [songId](const QVariantList &source, bool target) {
        QVariantList ids;
        ids.reserve(source.size() + 1);
        for (const QVariant &v : source)
            if (v.toDouble() != songId)
                ids.append(v);
        if (target)
            ids.append(songId);
        return ids;
    };
    const auto writeIds = [this, withState](const QVariantList &source, bool target) {
        put("likedIds", withState(source, target), true);
    };
    const auto writeCount = [this](int delta) {
        const QVariant raw = m_personal.value("liked");
        if (!raw.isValid() || raw.isNull())
            return;
        QVariantMap likedMap = raw.toMap();
        likedMap.insert("count", std::max(0.0, likedMap.value("count").toDouble() + delta));
        put("liked", likedMap, true);
    };

    writeIds(before, liked);
    writeCount(liked ? 1 : -1);
    m_likePending.insert(songId);
    const quint64 epoch = m_epoch;

    m_client->postWeapi("radio/like",
                        QJsonObject{{"alg", QStringLiteral("itembased")},
                                    {"trackId", num(songId)},
                                    {"like", liked ? QStringLiteral("true") : QStringLiteral("false")},
                                    {"time", QStringLiteral("25")}},
                        true,
                        [this, songId, liked, writeIds, writeCount, epoch](const QJsonDocument &doc, bool ok) {
        m_likePending.remove(songId);
        if (epoch != m_epoch)   // 期间退出登录：不要把回滚写到新会话
            return;
        const int code = doc.object().value("code").toInt();
        if (ok && code == 200) {
            if (liked)
                addLikedTrack(songId);
            else
                removeLikedTrack(songId);
            emit likeSucceeded(songId, liked);
            return;
        }

        // 回滚到点击前的状态。接口说会话过期时顺便更新全局状态，页头会显示「重新登录」。
        writeIds(m_personal.value("likedIds").toList(), !liked);
        writeCount(liked ? -1 : 1);
        if (code == 301)
            setSessionExpired(true);

        // 接口的 message 已经是给用户看的中文（例如「操作频繁，请稍候再试」），优先用它。
        const QString serverMessage = doc.object().value("message").toString();
        const QString reason = !ok ? QStringLiteral("网络不可用，请稍后再试")
            : !serverMessage.isEmpty() ? serverMessage
            : code == 301 ? QStringLiteral("登录已过期，请重新登录")
            : code == 250 ? QStringLiteral("这首歌暂时不能收藏")
                          : QStringLiteral("服务器没有接受这次操作");
        emit likeFailed(songId, liked, reason);
        qWarning() << "like failed:" << songId << code;
    });
}

void Store::notePlayed(const QVariantMap &track)
{
    const QVariant raw = m_personal.value("recent");
    if (track.isEmpty() || !raw.isValid() || raw.isNull())
        return;
    QVariantMap recent = raw.toMap();
    const double id = track.value("id").toDouble();
    QVariantList tracks = recent.value("tracks").toList();
    for (qsizetype i = tracks.size() - 1; i >= 0; --i)
        if (tracks.at(i).toMap().value("id").toDouble() == id)
            tracks.removeAt(i);
    tracks.prepend(track);
    recent.insert("tracks", tracks);
    recent.insert("total", std::max(recent.value("total").toDouble(), double(tracks.size())));
    put("recent", recent, true);
}

QVariantMap Store::trackById(double songId) const
{
    const auto in = [songId](const QVariantList &list) -> QVariantMap {
        for (const QVariant &v : list) {
            const QVariantMap track = v.toMap();
            if (track.value("id").toDouble() == songId)
                return track;
        }
        return {};
    };
    QVariantMap found = in(m_personal.value("recent").toMap().value("tracks").toList());
    if (!found.isEmpty())
        return found;
    found = in(m_data.value("tracks").toList());
    if (!found.isEmpty())
        return found;
    found = in(m_data.value("search").toMap().value("songs").toList());
    if (!found.isEmpty())
        return found;
    for (const char *key : {"daily", "heart", "radar", "similar", "fm"}) {
        found = in(m_personal.value(QLatin1String(key)).toMap().value("tracks").toList());
        if (!found.isEmpty())
            return found;
    }
    const QVariantMap details = m_personal.value("playlistDetails").toMap();
    for (auto it = details.constBegin(); it != details.constEnd(); ++it) {
        found = in(it.value().toMap().value("tracks").toList());
        if (!found.isEmpty())
            return found;
    }
    return {};
}

void Store::addLikedTrack(double songId)
{
    const QVariant raw = m_personal.value("liked");
    if (!raw.isValid() || raw.isNull())
        return;
    QVariantMap liked = raw.toMap();
    QVariantList tracks = liked.value("tracks").toList();
    for (const QVariant &v : tracks)
        if (v.toMap().value("id").toDouble() == songId)
            return;
    const QVariantMap known = trackById(songId);
    if (!known.isEmpty()) {
        tracks.prepend(known);
        liked.insert("tracks", tracks);
        put("liked", liked, true);
        return;
    }
    // 本地没有这首歌（比如从播放条直接红心）：取一次详情再插到列表首位
    fetchSongDetails({songId}, true,
                     [this, songId](const QHash<double, QVariantMap> &details, bool ok) {
        if (!ok || !details.contains(songId))
            return;
        const QVariant raw = m_personal.value("liked");
        if (!raw.isValid() || raw.isNull())
            return;
        QVariantMap liked = raw.toMap();
        QVariantList tracks = liked.value("tracks").toList();
        for (const QVariant &v : tracks)
            if (v.toMap().value("id").toDouble() == songId)
                return;
        tracks.prepend(details.value(songId));
        liked.insert("tracks", tracks);
        put("liked", liked, true);
    });
}

void Store::removeLikedTrack(double songId)
{
    const QVariant raw = m_personal.value("liked");
    if (!raw.isValid() || raw.isNull())
        return;
    QVariantMap liked = raw.toMap();
    QVariantList tracks = liked.value("tracks").toList();
    for (qsizetype i = tracks.size() - 1; i >= 0; --i)
        if (tracks.at(i).toMap().value("id").toDouble() == songId)
            tracks.removeAt(i);
    liked.insert("tracks", tracks);
    put("liked", liked, true);
}


void Store::playlistWrite(const QString &path, const QJsonObject &payload, const QString &action,
                          std::function<void(const QJsonObject &)> onSuccess)
{
    if (!m_client->hasMusicU()) {
        emit playlistActionFailed(action, QStringLiteral("登录后才能修改歌单"));
        return;
    }
    const quint64 epoch = m_epoch;
    m_client->postWeapi(path, payload, true,
                        [this, epoch, action, onSuccess](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch)   // 期间退出登录：不要改新会话的数据
            return;
        const QJsonObject resp = doc.object();
        if (ok && resp.value("code").toInt() == 200) {
            onSuccess(resp);
            return;
        }
        QString message = resp.value("message").toString();
        if (message.isEmpty())
            message = resp.value("msg").toString();
        if (message.isEmpty())
            message = ok ? QStringLiteral("服务器没有接受这次操作")
                         : QStringLiteral("网络不可用，请稍后再试");
        emit playlistActionFailed(action, message);
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

void Store::setPlaylistSubscribed(double id, bool subscribed)
{
    const QString action = subscribed ? QStringLiteral("收藏歌单") : QStringLiteral("取消收藏歌单");
    playlistWrite(subscribed ? QStringLiteral("playlist/subscribe") : QStringLiteral("playlist/unsubscribe"),
                  QJsonObject{{"id", num(id)}}, action,
                  [this, id, subscribed](const QJsonObject &) {
        const auto findCard = [this, id]() -> QVariantMap {
            const char *keys[] = {"playlists", "mine", "subscribed"};
            for (const char *key : keys) {
                for (const QVariant &v : m_personal.value(QLatin1String(key)).toList()) {
                    QVariantMap card = v.toMap();
                    if (card.value("id").toDouble() == id)
                        return card;
                }
            }
            for (const QVariant &v : m_data.value("search").toMap().value("playlists").toList()) {
                QVariantMap card = v.toMap();
                if (card.value("id").toDouble() == id)
                    return card;
            }
            return {};
        };

        QVariantList lists = m_personal.value("subscribed").toList();
        for (qsizetype i = lists.size() - 1; i >= 0; --i)
            if (lists.at(i).toMap().value("id").toDouble() == id)
                lists.removeAt(i);
        if (subscribed) {
            QVariantMap card = findCard();
            if (card.isEmpty())
                card = QVariantMap{{"id", id}, {"name", QString{}}, {"cover", QString{}},
                                   {"count", 0.0}, {"reason", QString{}}};
            lists.prepend(card);
        }
        put("subscribed", lists, true);
        emit playlistChanged(id);
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

        const auto addToCards = [this, playlistId, songId, track](const char *key) {
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
                // 卡片可能只带了曲目数、没带曲目（首屏预览）：不要把列表变成“只有这一首”，
                // 保持空表让 Source 按需取完整曲目。空歌单（count=0）可以安全放入第一首。
                if (!present && !track.isEmpty() && (!list.isEmpty() || card.value("count").toDouble() == 0))
                    list.append(track);
                card.insert("tracks", list);
                card.insert("count", card.value("count").toDouble() + 1);
                v = card;
                changed = true;
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
            if (!present && !track.isEmpty())
                list.append(track);
            detail.insert("tracks", list);
            detail.insert("count", std::max(0.0, detail.value("count").toDouble()) + 1);
            details.insert(key, detail);
            put("playlistDetails", details, true);
        }
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
                for (qsizetype i = list.size() - 1; i >= 0; --i)
                    if (list.at(i).toMap().value("id").toDouble() == songId)
                        list.removeAt(i);
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
            for (qsizetype i = list.size() - 1; i >= 0; --i)
                if (list.at(i).toMap().value("id").toDouble() == songId)
                    list.removeAt(i);
            detail.insert("tracks", list);
            detail.insert("count", std::max(0.0, detail.value("count").toDouble() - 1));
            details.insert(key, detail);
            put("playlistDetails", details, true);
        }
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
        emit playlistChanged(id);
    });
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
        emit playlistChanged(id);
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

void Store::resetPersonal()
{
    m_personal = m_emptyPersonal;
    m_cache.remember(personalKey(), m_personal);
    emit personalChanged();
}

void Store::logout()
{
    // 先发出 logout（请求头在调用时已带上旧 cookie），随后立刻清本地。
    // 不在这里等服务器回包：等 15 秒会让旧数据留在界面上，也会让旧请求有机会写回。
    if (m_client->hasMusicU())
        m_client->postWeapi("logout", {}, true, nullptr, /*keepCookies=*/false);
    ++m_epoch;
    m_client->clearAuthCookies();
    resetPersonal();
    m_cache.clear();
    setSessionExpired(false);
    m_likePending.clear();
    m_similarFired = false;
    m_similarSeed.clear();
    refresh();
    emit loggedOut();
}

// 连接状态与会话过期的唯一写入口：请求落地的真相在这里收敛
void Store::requestFinished(bool ok, bool networkFailure)
{
    // 只要服务器回了话（成功、4xx、业务码、坏 JSON 都算），网络就是通的；只有连不上才算离线
    if (networkFailure) {
        setOnline(false);
        m_refreshOwed = true;
        return;
    }
    setOnline(true);
    // 后端不可用时的兜底：网络通了本身就是恢复信号，还掉欠账
    if (ok && m_refreshOwed) {
        m_refreshOwed = false;
        refresh();
    }
}

void Store::setOnline(bool online)
{
    if (m_online == online)
        return;
    m_online = online;
    emit onlineChanged();
}

void Store::setSessionExpired(bool expired)
{
    if (m_sessionExpired == expired)
        return;
    m_sessionExpired = expired;
    emit sessionExpiredChanged();
}

void Store::fetchAlways(const QString &path, const QUrlQuery &q, bool personal,
                        NcmClient::GetCallback callback)
{
    ++m_pending;
    const quint64 epoch = m_epoch;
    m_client->get(path, q, personal, [this, callback, epoch](const QJsonDocument &doc, bool ok) {
        if (epoch == m_epoch)   // 期间退出登录：旧账号的数据不再落地
            callback(doc, ok);
        if (--m_pending == 0)
            emit settled();
    });
}

void Store::fetch(const QString &path, const QUrlQuery &q, bool personal, NcmClient::GetCallback callback,
                  bool acceptNotLoggedIn)
{
    // stale-while-revalidate 的另一半：失败（断网、超时、接口回 301 未登录等）一律不回调，
    // 已经恢复或取到的旧值原样保留，绝不用空结果覆盖。
    // 例外：账号接口要看到 301，才能判断「登录已过期」（acceptNotLoggedIn）
    fetchAlways(path, q, personal, [callback, acceptNotLoggedIn](const QJsonDocument &doc, bool ok) {
        const QJsonValue code = doc.object().value("code");
        const bool success = code.isUndefined() || code.toInt() == 200;
        if (ok && (success || (acceptNotLoggedIn && code.toInt() == 301)))
            callback(doc, ok);
    });
}

// 服务端 playlist.tracks 最多约 1000 条，但 trackIds 总是完整的。
// 先把已有的曲目按 id 建表，再只为缺失的 id 分批取 /api/v3/song/detail，最后按 trackIds 顺序还原。
void Store::fetchPlaylistTracks(double id, bool personal, TracksCallback callback)
{
    fetchAlways("api/v6/playlist/detail", query({{"id", num(id)}, {"n", "1000"}, {"s", "8"}}), personal,
                [this, personal, callback](const QJsonDocument &doc, bool ok) {
        const QJsonValue code = doc.object().value("code");
        if (!ok || (!code.isUndefined() && code.toInt() != 200)) {
            callback({}, {}, false);
            return;
        }
        const QJsonObject playlist = body(doc).value("playlist").toObject();
        if (playlist.isEmpty()) {
            callback({}, {}, false);
            return;
        }
        const QVariantList loaded = ncmTracks(arr(playlist, "tracks"));

        QHash<double, QVariantMap> byId;
        byId.reserve(loaded.size());
        for (const QVariant &v : loaded) {
            const QVariantMap track = v.toMap();
            byId.insert(track.value("id").toDouble(), track);
        }

        QList<double> ids;
        const QJsonArray trackIds = arr(playlist, "trackIds");
        if (!trackIds.isEmpty()) {
            ids.reserve(trackIds.size());
            for (const QJsonValue &v : trackIds)
                ids.append(v.toObject().value("id").toDouble());
        } else {
            ids.reserve(loaded.size());
            for (const QVariant &v : loaded)
                ids.append(v.toMap().value("id").toDouble());
        }

        QList<double> missing;
        for (double trackId : ids)
            if (!byId.contains(trackId))
                missing.append(trackId);

        const auto finish = [ids, playlist, byId, callback](bool detailsOk,
                                                            const QHash<double, QVariantMap> &details) {
            if (!detailsOk) {
                callback({}, playlist, false);
                return;
            }
            QHash<double, QVariantMap> merged = byId;
            for (auto it = details.constBegin(); it != details.constEnd(); ++it)
                merged.insert(it.key(), it.value());
            // 已删除/下架的曲目可能不在详情回包里：跳过，但仍保持原顺序
            QVariantList ordered;
            ordered.reserve(ids.size());
            for (double trackId : ids)
                if (merged.contains(trackId))
                    ordered.append(merged.value(trackId));
            callback(ordered, playlist, true);
        };

        if (missing.isEmpty()) {
            finish(true, {});
            return;
        }
        fetchSongDetails(missing, personal,
                         [finish](const QHash<double, QVariantMap> &details, bool detailsOk) {
            finish(detailsOk, details);
        });
    });
}

void Store::fetchSongDetails(const QList<double> &ids, bool personal,
                             std::function<void(const QHash<double, QVariantMap> &, bool)> callback)
{
    if (ids.isEmpty()) {
        callback({}, true);
        return;
    }

    struct State {
        QHash<double, QVariantMap> tracks;
        int left = 0;
        bool ok = true;
    };
    auto state = std::make_shared<State>();

    // GET 的 URL 长度有限：300 个 id（约 6KB）稳定，350 会回 400。分批更小，失败也能收尾。
    constexpr int Batch = 250;
    for (qsizetype start = 0; start < ids.size(); start += Batch) {
        QStringList fields;
        const qsizetype end = std::min(ids.size(), start + Batch);
        for (qsizetype i = start; i < end; ++i)
            fields.append(QStringLiteral("{\"id\":%1}").arg(num(ids.at(i))));
        const QString c = QLatin1Char('[') + fields.join(u',') + QLatin1Char(']');
        ++state->left;
        fetchAlways("api/v3/song/detail", query({{"c", c}}), personal,
                    [state, callback](const QJsonDocument &doc, bool ok) {
            const QJsonValue code = doc.object().value("code");
            if (!ok || (!code.isUndefined() && code.toInt() != 200))
                state->ok = false;
            for (const QJsonValue &v : arr(body(doc), "songs")) {
                const QVariantMap track = ncmTrack(v.toObject());
                state->tracks.insert(track.value("id").toDouble(), track);
            }
            if (--state->left == 0)
                callback(state->tracks, state->ok);
        });
    }
}

void Store::waitForInitial()
{
    refresh();
    if (m_pending > 0) {
        QEventLoop loop;
        // 兜底：网络挂了也要退出，不悬挂 --dump-store
        QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
        connect(this, &Store::settled, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

void Store::fetchAnonymous()
{
    fetch("api/toplist/detail", {}, false, [this](const QJsonDocument &doc, bool ok) {
        QVariantList toplists;
        if (ok) {
            const QJsonArray list = arr(body(doc), "list");
            for (int i = 0; i < list.size() && i < 12; ++i) {
                const QJsonObject t = list.at(i).toObject();
                QVariantList preview;
                for (const QJsonValue &p : t.value("tracks").toArray()) {
                    const QJsonObject o = p.toObject();
                    preview.append(QVariantMap{{"name", o.value("first").toString()},
                                               {"artist", o.value("second").toString()}});
                }
                toplists.append(QVariantMap{
                    {"id", t.value("id").toDouble()},
                    {"name", t.value("name").toString()},
                    {"cover", ncmSized(t.value("coverImgUrl"), 300)},
                    {"update", t.value("updateFrequency").toString()},
                    {"count", t.value("trackCount").toDouble()},
                    {"preview", preview},
                });
            }
        }
        put("toplists", toplists, false);
    });

    fetchPlaylistTracks(HotChart, false, [this](const QVariantList &tracks, const QJsonObject &hot, bool ok) {
        if (!ok)
            return;
        put("tracks", tracks, false);
        put("playlist", QVariantMap{
                            {"id", hot.value("id").toDouble()},
                            {"name", hot.value("name").toString()},
                            {"cover", ncmSized(hot.value("coverImgUrl"), 400)},
                            {"description", ncmFirstLine(hot.value("description").toString())},
                            {"count", hot.value("trackCount").toDouble()},
                            {"plays", hot.value("playCount").toDouble()},
                        }, false);
        // 相似歌曲的种子：未登录用热歌榜第一首；登录后等红心第一首（fetchHeart）。
        // 歌词不在首屏取：正在播放页按当前歌曲 request("lyrics:<id>")
        if (!tracks.isEmpty()) {
            m_similarSeed = tracks.at(0).toMap();
            if (!m_client->hasMusicU())
                maybeFetchSimilar();
        }
    });

    fetch("api/hotsearchlist/get", {}, false, [this](const QJsonDocument &doc, bool ok) {
        QVariantList hot;
        if (ok) {
            const QJsonArray data = arr(body(doc), "data");
            for (int i = 0; i < data.size() && i < 20; ++i) {
                const QJsonObject h = data.at(i).toObject();
                hot.append(QVariantMap{
                    {"word", h.value("searchWord").toString()},
                    {"note", h.value("content").toString()},
                    {"score", h.value("score").toDouble()},
                });
            }
        }
        put("hotSearches", hot, false);
    });
}

void Store::fetchAccount()
{
    // 账号是个人链的根：daily / liked→heart→radar→推荐歌单→最近/云盘 都挂在它后面
    fetch("api/nuser/account/get", {}, true, [this](const QJsonDocument &doc, bool ok) {
        const QJsonObject profile = body(doc).value("profile").toObject();
        const double uid = profile.value("userId").toDouble();
        // 会话已死（200 且无 profile，或回 301）：个人字段保持匿名替身的形状
        setSessionExpired(ok && uid <= 0);
        // 恢复出来的个人缓存属于另一个账号（或会话已失效）：先清回空骨架，不让旧账号的数据停留。
        // 请求本身失败（ok=false）时不清：失败保留已有的值
        const QString cachedName = m_personal.value("account").toMap().value("name").toString();
        if (ok && !cachedName.isEmpty()
            && (uid <= 0 || cachedName != profile.value("nickname").toString())) {
            m_personal = m_emptyPersonal;
            m_cache.remember(personalKey(), m_personal);
            emit personalChanged();
        }
        if (uid > 0) {
            put("account", QVariantMap{
                               {"name", profile.value("nickname").toString()},
                               {"vip", profile.value("vipType").toDouble() > 0},
                               {"avatar", ncmSized(profile.value("avatarUrl"), 96)},
                               {"background", ncmSized(profile.value("backgroundUrl"), 1200)},
                           }, true);
            // 全部红心曲目 id（播放条与行内小红心）；没有红心时接口也可能回 null
            fetch("api/song/like/get", query({{"uid", num(uid)}}), true,
                  [this](const QJsonDocument &doc, bool /*ok*/) {
                QVariantList ids;
                for (const QJsonValue &v : arr(body(doc), "ids"))
                    ids.append(v.toDouble());
                put("likedIds", ids, true);
            });
            fetchDaily();
            fetchUserLists(uid);
        } else {
            // cookie 在但会话已过期（或请求失败）：个人字段保持匿名替身的形状
            if (ok)
                qWarning() << "session expired, serving anonymous personal data";
            fetchAnonymousPlaylists();
        }
    }, /*acceptNotLoggedIn=*/true);
}

void Store::fetchPublicPersonal()
{
    // 私人漫游：匿名时也是公共流，登录后是个人的；snapshot.py 同样带 cookie 取
    fetch("api/v1/radio/get", {}, true, [this](const QJsonDocument &doc, bool /*ok*/) {
        put("fm", QVariantMap{{"tracks", ncmTracks(arr(body(doc), "data"))}}, true);
    });
}

void Store::fetchAnonymousPlaylists()
{
    // snapshot.py 匿名分支：公共推荐歌单，曲目与简介留空，点开再取
    fetch("api/personalized/playlist", query({{"limit", "12"}}), false, [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList cards;
        for (const QJsonValue &v : arr(body(doc), "result")) {
            QVariantMap card = ncmPlaylistCard(v.toObject(), "picUrl");
            card.insert("tracks", QVariantList{});
            card.insert("description", QString{});
            cards.append(card);
        }
        put("playlists", cards, true);
    });
}

// 给 personal.playlists 里某张卡补上曲目与简介。按 id 找当前值再改，
// 几个详情请求先后返回时不会互相覆盖
void Store::patchPlaylistCard(double id, const QVariantList &tracks, const QString &description)
{
    QVariantList cards = m_personal.value("playlists").toList();
    for (QVariant &v : cards) {
        QVariantMap card = v.toMap();
        if (card.value("id").toDouble() == id) {
            card.insert("tracks", tracks);
            card.insert("description", description);
            v = card;
        }
    }
    put("playlists", cards, true);
}

void Store::fetchDaily()
{
    fetch("api/v3/discovery/recommend/songs", {}, true, [this](const QJsonDocument &doc, bool ok) {
        if (ok) {
            const QDate today = QDate::currentDate();
            // Python 的 strftime("%m月%d日").lstrip("0")：去掉月份的补零
            QString date = today.toString(QStringLiteral("MM月dd日"));
            if (date.startsWith(u'0'))
                date.remove(0, 1);
            const QJsonObject data = body(doc).value("data").toObject();
            // 推荐理由按 songId 对上：data.recommendReasons 里每条 { songId, reason }
            QHash<double, QString> reasons;
            for (const QJsonValue &r : arr(data, "recommendReasons")) {
                const QJsonObject o = r.toObject();
                reasons.insert(o.value("songId").toDouble(), o.value("reason").toString());
            }
            QVariantList tracks;
            const QJsonArray dailySongs = take(arr(data, "dailySongs"), 30);
            tracks.reserve(dailySongs.size());
            for (const QJsonValue &s : dailySongs) {
                const QJsonObject song = s.toObject();
                tracks.append(ncmTrack(song, reasons.value(song.value("id").toDouble())));
            }
            put("daily", QVariantMap{
                             {"date", date},
                             {"day", QString::number(today.day())},
                             {"tracks", tracks},
                         }, true);
        }
    });
}

void Store::fetchUserLists(double uid)
{
    // 第一次请求的「我喜欢的音乐」+ 全部创建/收藏歌单。limit=1000 是接口上限；
    // 这里不再截前 12 个，界面自己决定首屏显示几块，「全部」页拿到的是完整列表。
    fetch("api/user/playlist", query({{"uid", num(uid)}, {"limit", "1000"}, {"offset", "0"}}), true,
          [this, uid](const QJsonDocument &doc, bool ok) {
        const QJsonArray lists = arr(body(doc), "playlist");
        if (!ok || lists.isEmpty())
            return;
        QVariantList mine, subscribed;
        for (int i = 1; i < lists.size(); ++i) {
            const QJsonObject p = lists.at(i).toObject();
            QVariantMap card = ncmPlaylistCard(p, "coverImgUrl");
            const bool owned = p.value("creator").toObject().value("userId").toDouble() == uid;
            (owned ? &mine : &subscribed)->append(card);
        }
        put("mine", mine, true);
        put("subscribed", subscribed, true);
        fetchLiked(lists.at(0).toObject());
    });
}

void Store::fetchLiked(const QJsonObject &likedList)
{
    // 红心往往上千首：playlist.tracks 只回前 1000 条，fetchPlaylistTracks 用 trackIds 补齐。
    fetchPlaylistTracks(likedList.value("id").toDouble(), true,
                        [this, likedList](const QVariantList &tracks, const QJsonObject &liked, bool ok) {
        if (!ok)
            return;
        put("liked", QVariantMap{{"id", liked.value("id").toDouble()},
                                 {"count", liked.value("trackCount").toDouble()},
                                 {"tracks", tracks}}, true);
        // snapshot.py：心动模式以红心第一首为种子；红心为空就没有这一块
        if (!tracks.isEmpty())
            fetchHeart(tracks.at(0).toMap(), likedList);
    });
}

void Store::fetchHeart(const QVariantMap &seed, const QJsonObject &likedList)
{
    m_similarSeed = seed;
    maybeFetchSimilar();
    fetch("api/playmode/intelligence/list",
          query({{"songId", seed.value("id").toString()},
                 {"type", "fromPlayAll"},
                 {"playlistId", num(likedList.value("id").toDouble())},
                 {"startMusicId", seed.value("id").toString()},
                 {"count", "1"}}), true,
          [this, seed](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList tracks;
        for (const QJsonValue &x : arr(body(doc), "data"))
            tracks.append(ncmTrack(x.toObject().value("songInfo").toObject()));
        put("heart", QVariantMap{{"seed", seed}, {"tracks", tracks.mid(0, 30)}}, true);
        fetchRadar();
    });
}

void Store::fetchRadar()
{
    fetchPlaylistTracks(Radar, true,
                        [this](const QVariantList &tracks, const QJsonObject &radar, bool ok) {
        if (ok)
            put("radar", QVariantMap{{"id", radar.value("id").toDouble()},
                                     {"name", QStringLiteral("私人雷达")},
                                     {"description", radar.value("name").toString()},
                                     {"cover", ncmSized(radar.value("coverImgUrl"), 300)},
                                     {"tracks", tracks}}, true);
        fetchTail();
    });
}

void Store::fetchTail()
{
    fetch("api/v1/discovery/recommend/resource", {}, true, [this](const QJsonDocument &doc, bool /*ok*/) {
        QVariantList cards;
        const QJsonArray recommend = arr(body(doc), "recommend");
        for (const QJsonValue &v : recommend) {
            QVariantMap card = ncmPlaylistCard(v.toObject(), "picUrl");
            card.insert("tracks", QVariantList{});
            card.insert("description", QString{});
            cards.append(card);
        }
        put("playlists", cards, true);
        // 前 6 个补完整曲目和一句简介；其余保持空，点开再取。
        // 这是首页磁贴的预览数据，不截 30 首：点进去就是完整歌单，不需要再等一次按需加载。
        for (int i = 0; i < cards.size() && i < 6; ++i) {
            const double id = cards.at(i).toMap().value("id").toDouble();
            fetchPlaylistTracks(id, true,
                                [this, id](const QVariantList &tracks, const QJsonObject &pl, bool detailOk) {
                if (detailOk)
                    patchPlaylistCard(id, tracks, ncmFirstLine(pl.value("description").toString()));
            });
        }
    });

    fetch("api/play-record/song/list", query({{"limit", "300"}}), true,
          [this](const QJsonDocument &doc, bool /*ok*/) {
        const QJsonObject record = body(doc).value("data").toObject();
        QVariantList tracks;
        for (const QJsonValue &x : arr(record, "list"))
            tracks.append(ncmTrack(x.toObject().value("data").toObject()));
        put("recent", QVariantMap{{"total", record.value("total").toDouble()},
                                  {"tracks", tracks}}, true);
    });

    // 云盘用 offset 分页：接口单页上限 200，逐页取到 count 或空页为止（最多 1000 首）
    fetchCloudPage(std::make_shared<CloudState>());
}


struct Store::CloudState {
    QVariantList tracks;
    double count = 0;
    double size = 0;
    double maxSize = 0;
    int offset = 0;
};

void Store::fetchCloudPage(const std::shared_ptr<CloudState> &state)
{
    fetch("api/v1/cloud/get",
          query({{"limit", "200"}, {"offset", QString::number(state->offset)}}), true,
          [this, state](const QJsonDocument &doc, bool ok) {
        const QJsonObject resp = body(doc);
        if (ok) {
            state->count = resp.value("count").toDouble();
            state->size = resp.value("size").toDouble();
            state->maxSize = std::floor(resp.value("maxSize").toDouble());
            const QJsonArray items = arr(resp, "data");
            for (const QJsonValue &x : items)
                state->tracks.append(ncmCloudTrack(x.toObject()));
            state->offset += items.size();
        }
        if (ok && !arr(resp, "data").isEmpty() && state->offset < state->count
            && state->offset < 1000) {
            fetchCloudPage(state);
            return;
        }
        put("cloud", QVariantMap{{"count", state->count},
                                 {"size", state->size},
                                 {"maxSize", state->maxSize},
                                 {"tracks", state->tracks}}, true);
    });
}

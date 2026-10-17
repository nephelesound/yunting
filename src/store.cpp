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
QVariant nullVariant() { return QVariant::fromValue(nullptr); }

// 每种失败一句话：发生了什么 + 该做什么。只有「拒绝」用网易云自己的 message：
// 它本来就是给用户看的中文，而且是唯一知道具体原因的一方（歌单重名、这首歌不能收藏…）
QString failureMessage(NcmClient::Failure failure, const QJsonObject &body)
{
    switch (failure) {
    case NcmClient::Failure::None:
        return {};
    case NcmClient::Failure::Offline:
        return QStringLiteral("网络不可用，请稍后再试");
    case NcmClient::Failure::SessionExpired:
        return QStringLiteral("登录已过期，请重新登录");
    case NcmClient::Failure::RateLimited:
        return QStringLiteral("操作太频繁，请稍后再试");
    case NcmClient::Failure::Rejected:
        break;
    }
    for (const char *key : {"message", "msg"}) {
        const QString message = body.value(QLatin1String(key)).toString();
        if (!message.isEmpty())
            return message;
    }
    return QStringLiteral("网易云没有接受这次操作");
}
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
        {"user", nullVariant()},
        // 只读信箱：私密数据只进内存，不写首屏快照
        {"inbox", nullVariant()},
        {"inboxThread", nullVariant()},
        {"listenTotal", nullVariant()},
        {"listenRealtime", nullVariant()},
        {"listenRank", nullVariant()},
        {"notifications", nullVariant()},
        {"userFollows", nullVariant()},
        {"userFolloweds", nullVariant()},
        {"userLevel", nullVariant()},
        {"userSubcount", nullVariant()},
        {"commentReplies", nullVariant()},
        {"lyricsNew", nullVariant()},
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
    m_client->setCallbackContext(this);
    // QueryCache 的失败只在这里汇总成 QML 可消费的通用失败信号；成功仍走 dataChanged
    connect(&m_cache, &QueryCache::fetchFailed, this, [this](const QString &key) {
        emit requestFailed(key, QStringLiteral("网络不可用，请稍后再试"));
    });
    restoreScreen();
    // 换账号检测只认 userId：昵称会重复（「音乐爱好者」），不能当身份
    if (m_client->hasMusicU())
        m_accountUid = m_cache.peek(QStringLiteral("screen:uid")).toDouble();

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
    // 旧版把整个 m_data 写进 screen:data；只恢复首屏字段，别把历史搜索词带进内存
    static const QStringList screenKeys{QStringLiteral("toplists"), QStringLiteral("playlist"),
                                        QStringLiteral("tracks"), QStringLiteral("hotSearches")};
    QVariantMap cachedData = m_cache.peek("screen:data").toMap();
    for (auto it = cachedData.begin(); it != cachedData.end();) {
        if (screenKeys.contains(it.key()))
            ++it;
        else
            it = cachedData.erase(it);
    }
    overlay(m_data, cachedData);
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
        persistScreenData();
        emit dataChanged();
    }
}

// 首屏快照只留首屏字段。搜索词、评论、歌手/专辑/MV 详情都在别的 key 里，
// 混进 screen:data 会绕过「搜索词不落盘」。
void Store::persistScreenData()
{
    QVariantMap screen;
    for (const char *key : {"toplists", "playlist", "tracks", "hotSearches"}) {
        const QString name = QLatin1String(key);
        if (m_data.contains(name))
            screen.insert(name, m_data.value(name));
    }
    m_cache.remember(QStringLiteral("screen:data"), screen);
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

void Store::resetPersonal()
{
    // 私信与账号绑定：退出 / 换账号时不能留在内存里
    put("inbox", nullVariant(), false);
    put("inboxThread", nullVariant(), false);
    put("notifications", nullVariant(), false);
    put("userLevel", nullVariant(), false);
    put("userSubcount", nullVariant(), false);
    put("listenTotal", nullVariant(), false);
    put("listenRealtime", nullVariant(), false);
    put("listenRank", nullVariant(), false);
    put("userFollows", nullVariant(), false);
    put("userFolloweds", nullVariant(), false);
    m_personal = m_emptyPersonal;
    m_cache.remember(personalKey(), m_personal);
    emit personalChanged();
}

void Store::logout()
{
    // 先发出 logout（请求头在调用时已带上旧 cookie），随后立刻清本地。
    // 不在这里等服务器回包：等 15 秒会让旧数据留在界面上，也会让旧请求有机会写回。
    if (m_client->hasMusicU())
        m_client->postWeapi("logout", {}, true, NcmClient::GetCallback{}, /*keepCookies=*/false);
    ++m_epoch;
    m_client->clearAuthCookies();
    resetPersonal();
    m_cache.clear();
    setSessionExpired(false);
    m_likes.clear();
    m_likeTouched.clear();
    m_accountUid = 0;
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

// 个人请求（写操作与信箱）失败的唯一收尾。放在这里而不是各调用点，301 才不会在某个
// 写路径被忘掉；句子也只有一个来源。
QString Store::requestFailure(const NcmClient::Response &response)
{
    const NcmClient::Failure failure = NcmClient::failureOf(response);
    if (failure == NcmClient::Failure::None)
        return {};
    if (failure == NcmClient::Failure::SessionExpired)
        setSessionExpired(true);
    return failureMessage(failure, response.doc.object());
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
        const QVariantList loaded = ncmTracks(arr(playlist, "tracks"), arr(body(doc), "privileges"));

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
            for (const QVariant &v : ncmTracks(arr(body(doc), "songs"), arr(body(doc), "privileges"))) {
                const QVariantMap track = v.toMap();
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

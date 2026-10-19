#include "listentogether.h"

#include "audioengine.h"
#include "json_util.h"
#include "ncmclient.h"
#include "sdk/listen_together.h"
#include "sdk/track.h"
#include "sdk/track_service.h"
#include "store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cstdlib>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(lcListen, "yunting.listen")

namespace sdk = ncm::sdk;

constexpr int SyncPollMs = 1000;
constexpr int StatusPollMs = 5000;
constexpr int HeartbeatPollMs = 5000;
constexpr int RemoteApplyHoldMs = 250;

QString apiErrorText(const sdk::Error &error, const QString &fallback)
{
    switch (error.kind) {
    case sdk::ErrorKind::Network:
        return QStringLiteral("网络不可用，请稍后再试");
    case sdk::ErrorKind::Api:
        return error.message.isEmpty() ? fallback : error.message;
    case sdk::ErrorKind::Parse:
        return QStringLiteral("服务返回异常，请稍后再试");
    case sdk::ErrorKind::InvalidRequest:
        return fallback;
    }
    return fallback;
}

qint64 variantId(const QVariant &value)
{
    bool ok = false;
    const qint64 id = value.toLongLong(&ok);
    return ok ? id : static_cast<qint64>(qRound64(value.toDouble()));
}

QList<qint64> positiveIds(const QVariantList &values)
{
    QList<qint64> ids;
    ids.reserve(values.size());
    for (const QVariant &value : values) {
        const qint64 id = variantId(value);
        if (id > 0)
            ids.append(id);
    }
    return ids;
}

QVariantList variantIds(const QList<qint64> &ids)
{
    QVariantList values;
    values.reserve(ids.size());
    for (qint64 id : ids)
        values.append(static_cast<double>(id));
    return values;
}

bool sameIds(const QVariantList &current, const QList<qint64> &wanted)
{
    if (current.size() != wanted.size())
        return false;
    for (qsizetype i = 0; i < wanted.size(); ++i) {
        if (variantId(current.at(i)) != wanted.at(i))
            return false;
    }
    return true;
}

QVariantList roomUsers(const QList<sdk::ListenTogetherUser> &users)
{
    QVariantList values;
    values.reserve(users.size());
    for (const sdk::ListenTogetherUser &user : users) {
        values.append(QVariantMap{
            {"userId", static_cast<double>(user.userId)},
            {"name", user.nickname},
            {"avatar", user.avatar},
            {"avatarPendant", user.pendant},
            {"identityIcon", user.identityIcon},
            {"identity", user.identity},
            {"vipLevel", user.vipLevel},
        });
    }
    return values;
}

bool isCommandType(const QString &type, const char *name)
{
    return type.compare(QLatin1String(name), Qt::CaseInsensitive) == 0;
}

} // namespace

ListenTogether::ListenTogether(NcmClient *client, Store *store, AudioEngine *audio,
                               QObject *parent)
    : QObject(parent), m_client(client), m_store(store), m_audio(audio)
{
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(SyncPollMs);
    connect(m_syncTimer, &QTimer::timeout, this, &ListenTogether::fetchSyncPlaylist);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(StatusPollMs);
    connect(m_statusTimer, &QTimer::timeout, this, &ListenTogether::refreshStatus);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(HeartbeatPollMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ListenTogether::sendHeartbeat);

    // 远端指令应用期间 Qt 的属性信号可能排到下一轮事件循环；250ms 内继续抑制回声。
    m_remoteTimer = new QTimer(this);
    m_remoteTimer->setSingleShot(true);
    m_remoteTimer->setInterval(RemoteApplyHoldMs);
    connect(m_remoteTimer, &QTimer::timeout, this, [this] {
        if (!m_remoteApplying)
            return;
        m_remoteApplying = false;
        emit remoteApplyingChanged();
    });

    m_lastSongId = currentSongId();
    updateIdentity();

    connect(m_audio, &AudioEngine::trackChanged, this, &ListenTogether::onAudioTrackChanged);
    connect(m_audio, &AudioEngine::playingChanged, this, &ListenTogether::onAudioPlayingChanged);
    connect(m_audio, &AudioEngine::seeked, this, &ListenTogether::onAudioSeeked);

    connect(m_store, &Store::loggedOut, this, [this] {
        // 在途的 create/join/leave 不能再把刚清掉的房间写回来。
        ++m_actionGeneration;
        setBusy(false);
        clearRoom(true);
        updateIdentity();
    });
    connect(m_store, &Store::personalChanged, this, &ListenTogether::updateIdentity);
    connect(m_store, &Store::dataChanged, this, &ListenTogether::updateIdentity);

    connect(m_client, &NcmClient::ready, this, [this] {
        if (m_client->hasMusicU())
            refresh();
    });
    // ready 可能早于本对象构造：单次补一次，仍然只处理已登录场景。
    if (m_client->isReady() && m_client->hasMusicU())
        QTimer::singleShot(0, this, &ListenTogether::refresh);
}

void ListenTogether::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ListenTogether::setError(const QString &error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

void ListenTogether::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

bool ListenTogether::ensureLogin()
{
    updateIdentity();
    if (!m_client->hasMusicU() || m_store->accountUid() <= 0) {
        setError(QStringLiteral("请先登录"));
        return false;
    }
    return true;
}

void ListenTogether::updateIdentity()
{
    const double rawUid = m_store->accountUid();
    const qint64 uid = rawUid > 0 ? static_cast<qint64>(rawUid) : 0;
    if (uid == m_myUid)
        return;

    m_myUid = uid;
    const bool wasHost = m_host;
    const qint64 oldInviter = m_inviterId;
    m_host = m_inRoom && m_creatorId > 0 && m_creatorId == m_myUid;
    if (m_inRoom && m_inviterId <= 0)
        m_inviterId = m_creatorId > 0 ? m_creatorId : m_myUid;

    bool changed = wasHost != m_host || oldInviter != m_inviterId;
    const QString share = buildShareUrl();
    if (share != m_shareUrl) {
        m_shareUrl = share;
        changed = true;
    }
    if (changed)
        emit stateChanged();
}

QString ListenTogether::buildShareUrl() const
{
    if (!m_inRoom || m_roomId.isEmpty())
        return {};
    qint64 songId = currentSongId();
    if (songId <= 0)
        songId = m_lastSongId;
    if (songId < 0)
        songId = 0;
    const qint64 inviter = m_inviterId > 0 ? m_inviterId
        : (m_creatorId > 0 ? m_creatorId : m_myUid);
    return QStringLiteral(
               "https://st.music.163.com/listen-together/share/"
               "?songId=%1&roomId=%2&inviterId=%3")
        .arg(songId)
        .arg(m_roomId)
        .arg(inviter > 0 ? inviter : 0);
}

void ListenTogether::updateShareUrl()
{
    const QString share = buildShareUrl();
    if (share == m_shareUrl)
        return;
    m_shareUrl = share;
    emit stateChanged();
}

void ListenTogether::adoptRoom(const sdk::ListenTogetherRoom &room, qint64 inviterId)
{
    if (room.roomId.isEmpty())
        return;

    updateIdentity();

    const bool wasInRoom = m_inRoom;
    const bool roomChanged = !wasInRoom || m_roomId != room.roomId;
    const qint64 oldCreator = m_creatorId;
    const qint64 oldInviter = m_inviterId;
    const bool oldHost = m_host;

    if (roomChanged) {
        ++m_generation;
        ++m_queueGeneration;
        stopTimers();
        m_lastServerSeq = 0;
        m_clientSeq = 0;
        m_playlistVersion = 0;
        m_versions.clear();
        m_playMode.clear();
        m_statisticsUid = 0;
        m_privilegeKey.clear();
        if (!m_statistics.isEmpty()) {
            m_statistics.clear();
            emit statisticsChanged();
        }
        if (!m_unplayableIds.isEmpty()) {
            m_unplayableIds.clear();
            emit unplayableChanged();
        }
        m_remoteTimer->stop();
        if (m_remoteApplying) {
            m_remoteApplying = false;
            emit remoteApplyingChanged();
        }
    }

    const qint64 knownCreator = m_roomId == room.roomId ? m_creatorId : 0;
    m_roomId = room.roomId;
    // room/create 有时只回 roomId；房主由本地 create 成功时补上，后续状态刷新不能把它抹掉。
    m_creatorId = room.creatorId > 0 ? room.creatorId : knownCreator;
    if (inviterId > 0)
        m_inviterId = inviterId;
    else if (roomChanged || m_inviterId <= 0)
        m_inviterId = m_creatorId > 0 ? m_creatorId : m_myUid;

    m_inRoom = true;
    m_host = m_creatorId > 0 && m_creatorId == m_myUid;

    const QVariantList users = roomUsers(room.users);
    const bool usersDiffer = users != m_users;
    m_users = users;

    bool tracksCleared = false;
    if (roomChanged) {
        tracksCleared = !m_tracks.isEmpty() || !m_trackIds.isEmpty();
        m_tracks.clear();
        m_trackIds.clear();
        m_lastSongId = currentSongId();
    }

    const QString share = buildShareUrl();
    const bool shareChanged = share != m_shareUrl;
    m_shareUrl = share;

    if (usersDiffer)
        emit usersChanged();
    if (tracksCleared)
        emit tracksChanged();
    if (roomChanged || oldCreator != m_creatorId || oldInviter != m_inviterId
        || oldHost != m_host || shareChanged) {
        emit stateChanged();
    }

    if (roomChanged) {
        startTimers();
        fetchSyncPlaylist();
    }
    // 另一位成员进来了：和他的累计、歌单里他放不了的歌
    refreshStatistics();
    refreshPrivileges();
}

void ListenTogether::clearRoom(bool emitLeft)
{
    const bool hadRoom = m_inRoom || !m_roomId.isEmpty();
    const bool hadUsers = !m_users.isEmpty();
    const bool hadTracks = !m_tracks.isEmpty() || !m_trackIds.isEmpty();
    const bool hadShare = !m_shareUrl.isEmpty();

    // 即使本地还没有房间，也要让在途的 status 回包失效（启动刷新后马上登出）。
    ++m_generation;
    ++m_queueGeneration;
    stopTimers();
    m_remoteTimer->stop();
    const bool remoteChanged = m_remoteApplying;
    m_remoteApplying = false;

    if (!hadRoom) {
        clearError();
        if (hadShare) {
            m_shareUrl.clear();
            emit stateChanged();
        }
        if (remoteChanged)
            emit remoteApplyingChanged();
        return;
    }

    m_inRoom = false;
    m_host = false;
    m_roomId.clear();
    m_inviterId = 0;
    m_creatorId = 0;
    m_users.clear();
    m_tracks.clear();
    m_trackIds.clear();
    m_shareUrl.clear();
    m_lastServerSeq = 0;
    m_clientSeq = 0;
    m_playlistVersion = 0;
    m_versions.clear();
    m_playMode.clear();
    m_statisticsUid = 0;
    m_privilegeKey.clear();
    const bool hadStatistics = !m_statistics.isEmpty();
    const bool hadUnplayable = !m_unplayableIds.isEmpty();
    m_statistics.clear();
    m_unplayableIds.clear();
    clearError();

    emit stateChanged();
    if (hadStatistics)
        emit statisticsChanged();
    if (hadUnplayable)
        emit unplayableChanged();
    if (hadUsers)
        emit usersChanged();
    if (hadTracks)
        emit tracksChanged();
    if (remoteChanged)
        emit remoteApplyingChanged();
    if (emitLeft)
        emit leftRoom();
}

void ListenTogether::startTimers()
{
    if (!m_syncTimer->isActive())
        m_syncTimer->start();
    if (!m_statusTimer->isActive())
        m_statusTimer->start();
    if (!m_heartbeatTimer->isActive())
        m_heartbeatTimer->start();
}

void ListenTogether::stopTimers()
{
    m_syncTimer->stop();
    m_statusTimer->stop();
    m_heartbeatTimer->stop();
    m_syncInFlight = false;
    m_statusInFlight = false;
    m_heartbeatInFlight = false;
}

void ListenTogether::createRoom()
{
    if (!ensureLogin() || m_busy)
        return;

    clearError();
    const quint64 action = ++m_actionGeneration;
    const qint64 inviter = m_myUid;
    setBusy(true);

    sdk::createRoom(*m_client, this,
                    [this, action, inviter](sdk::Result<sdk::ListenTogetherRoom> result) {
        if (action != m_actionGeneration)
            return;
        setBusy(false);
        if (!result) {
            setError(apiErrorText(result.error, QStringLiteral("创建一起听房间失败")));
            return;
        }
        sdk::ListenTogetherRoom room = *result.value;
        if (room.creatorId <= 0)
            room.creatorId = inviter;
        adoptRoom(room, inviter);
    });
}

void ListenTogether::joinRoom(const QString &roomRef, const QString &inviterId)
{
    if (!ensureLogin() || m_busy)
        return;

    QString roomId;
    qint64 inviter = 0;
    QString parseError;
    if (!parseJoinTarget(roomRef, inviterId, &roomId, &inviter, &parseError)) {
        setError(parseError);
        return;
    }

    clearError();
    const quint64 action = ++m_actionGeneration;
    setBusy(true);

    // 先查房间是否还能加入：满员/已失效时服务器会给一句可展示的 copywriting。
    // 直接 accept 会让界面在错误里静默不动，用户会以为点击没有被处理。
    sdk::checkRoom(*m_client, roomId, this,
                   [this, action, roomId, inviter](sdk::Result<sdk::ListenTogetherCheck> check) {
        if (action != m_actionGeneration)
            return;
        if (check && !check.value->joinable) {
            if ((m_inRoom && m_roomId == roomId)
                || check.value->type.compare(QLatin1String("sameRoom"), Qt::CaseInsensitive) == 0) {
                setBusy(false);
                clearError();
                return;   // 已经在这个房间里，点邀请不该报错
            }
            setBusy(false);
            setError(check.value->copywriting.isEmpty()
                         ? QStringLiteral("这个一起听房间现在无法加入")
                         : check.value->copywriting);
            return;
        }
        sdk::acceptRoom(*m_client, roomId, inviter, this,
                        [this, action, inviter](sdk::Result<sdk::ListenTogetherRoom> result) {
            if (action != m_actionGeneration)
                return;
            setBusy(false);
            if (!result) {
                setError(apiErrorText(result.error, QStringLiteral("加入一起听房间失败")));
                return;
            }
            adoptRoom(*result.value, inviter);
        });
    });
}

void ListenTogether::leaveRoom()
{
    if (m_busy)
        return;
    if (!m_inRoom || m_roomId.isEmpty()) {
        setError(QStringLiteral("当前不在房间"));
        return;
    }

    clearError();
    const quint64 action = ++m_actionGeneration;
    const QString roomId = m_roomId;
    setBusy(true);

    sdk::endRoom(*m_client, roomId, this,
                 [this, action, roomId](sdk::Result<sdk::ListenTogetherResult> result) {
        if (action != m_actionGeneration)
            return;
        setBusy(false);
        if (!result) {
            setError(apiErrorText(result.error, QStringLiteral("退出一起听房间失败")));
            return;
        }
        if (!result.value->success) {
            setError(result.value->message.isEmpty()
                         ? QStringLiteral("退出一起听房间失败")
                         : result.value->message);
            return;
        }
        if (m_roomId != roomId)
            return;
        clearRoom(true);
    });
}

void ListenTogether::refresh()
{
    if (!m_client->hasMusicU()) {
        ++m_actionGeneration;
        setBusy(false);
        clearRoom(true);
        return;
    }
    refreshStatus();
}

void ListenTogether::refreshStatus()
{
    if (!m_client->hasMusicU()) {
        ++m_actionGeneration;
        setBusy(false);
        clearRoom(true);
        return;
    }
    if (m_statusInFlight)
        return;

    const quint64 generation = m_generation;
    m_statusInFlight = true;
    sdk::fetchStatus(*m_client, this,
                     [this, generation](sdk::Result<sdk::ListenTogetherStatus> result) {
        if (generation != m_generation)
            return;
        m_statusInFlight = false;
        if (!result)
            return;
        if (result.value->inRoom) {
            adoptRoom(result.value->room, 0);
        } else if (m_inRoom) {
            // 其它端/服务器已经结束房间：本地跟着退，别留在幽灵房间里轮询。
            clearRoom(true);
        }
    });
}

// 官方客户端拉歌单时也带本地队列：服务器用 displayList/randomList 计算差异并返回
// 需要替换的列表。只传 roomId 时只能拿到旧缓存，房主改了队列对方也不会变。
QJsonObject ListenTogether::playlistContext() const
{
    QJsonArray display;
    QJsonArray random;
    qint64 anchorPosition = -1;
    const qint64 nowPlaying = currentSongId();
    for (qsizetype i = 0; i < m_trackIds.size(); ++i) {
        const qint64 id = variantId(m_trackIds.at(i));
        if (id <= 0)
            continue;
        const QString value = QString::number(id);
        display.append(value);
        random.append(value);
        if (id == nowPlaying)
            anchorPosition = i;
    }

    QJsonObject param;
    param.insert(QStringLiteral("displayList"), display);
    param.insert(QStringLiteral("randomList"), random);
    param.insert(QStringLiteral("anchorSongId"), QString::number(nowPlaying));
    param.insert(QStringLiteral("anchorPosition"), static_cast<double>(anchorPosition));
    param.insert(QStringLiteral("clientSeq"), static_cast<double>(m_clientSeq));
    if (!m_playMode.isEmpty())
        param.insert(QStringLiteral("playMode"), m_playMode);
    return param;
}

QJsonArray ListenTogether::playlistVersions() const
{
    QJsonArray versions;
    for (auto it = m_versions.cbegin(); it != m_versions.cend(); ++it) {
        versions.append(QJsonObject{
            {QStringLiteral("userId"), static_cast<double>(it.key())},
            {QStringLiteral("version"), static_cast<double>(it.value())},
        });
    }
    return versions;
}

void ListenTogether::fetchSyncPlaylist()
{
    if (!m_inRoom || m_syncInFlight || m_roomId.isEmpty())
        return;

    const quint64 generation = m_generation;
    const QString roomId = m_roomId;
    m_syncInFlight = true;
    sdk::fetchSyncPlaylist(*m_client, roomId, playlistContext(), this,
                           [this, generation, roomId](sdk::Result<sdk::ListenTogetherSync> result) {
        if (generation != m_generation || roomId != m_roomId)
            return;
        m_syncInFlight = false;
        if (!result) {
            if (expireIfGone(result.error))
                return;
            // 解析失败时不要把旧队列当最新状态；下一次轮询再试。
            qCDebug(lcListen) << "sync playlist failed:" << result.error.message;
            return;
        }
        applySync(*result.value);
    });
}

void ListenTogether::sendHeartbeat()
{
    if (!m_inRoom || m_heartbeatInFlight || m_roomId.isEmpty())
        return;

    const quint64 generation = m_generation;
    const QString roomId = m_roomId;
    const qint64 songId = currentSongId();
    const bool playing = m_audio->playing();
    const QString playStatus = playing ? QStringLiteral("PLAY") : QStringLiteral("PAUSE");
    const qint64 progress = audioProgressMs();

    m_heartbeatInFlight = true;
    sdk::reportHeartbeat(*m_client, roomId, songId, playStatus, progress,
                         playlistVersions(), this,
                         [this, generation, roomId](sdk::Result<sdk::ListenTogetherResult> result) {
        if (generation != m_generation || roomId != m_roomId)
            return;
        m_heartbeatInFlight = false;
        if (!result)
            expireIfGone(result.error);
    });
}

void ListenTogether::applySync(const sdk::ListenTogetherSync &sync)
{
    qCDebug(lcListen) << "sync playlist display" << sync.playlist.displayList.size()
                      << "random" << sync.playlist.randomList.size()
                      << "replace" << sync.playlist.replaced
                      << "command" << sync.command.has_value();
    if (!sync.playlist.versions.isEmpty()) {
        m_versions.clear();
        for (const sdk::ListenTogetherVersion &entry : sync.playlist.versions)
            m_versions.insert(entry.userId, entry.version);
    }
    if (!sync.playlist.playMode.isEmpty())
        m_playMode = sync.playlist.playMode;
    applyPlaylist(sync.playlist);
    if (sync.command)
        applyCommand(*sync.command);
}

void ListenTogether::applyPlaylist(const sdk::ListenTogetherPlaylist &playlist)
{
    // changed=false 表示服务器这次没有给出新的队列；不能用旧列表覆盖本地刚同步的队列。
    if (!playlist.replaced && !playlist.displayChanged && !playlist.randomChanged)
        return;

    // trackIds 原样对应服务器的 displayList；randomList 是随机模式下的另一份顺序，
    // 只有服务器没给 displayList 时才拿它兜底，避免随机列表变化每分钟把队列翻一遍。
    QList<qint64> ids = playlist.displayList;
    if (ids.isEmpty())
        ids = playlist.randomList;
    // replace=false 且没有列表时只是「这次没有变更」，不能把已经拿到的队列清空。
    if (ids.isEmpty() && !playlist.replaced)
        return;

    const bool same = sameIds(m_trackIds, ids);
    if (same && ids.isEmpty()) {
        if (!m_tracks.isEmpty()) {
            ++m_queueGeneration;
            m_tracks.clear();
            emit tracksChanged();
        }
        return;
    }
    const bool complete = queueComplete(ids);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // 同一队列且详情齐全时无事可做；详情取失败（或只补到部分曲目）时，
    // 最多每 10 秒重试一次，避免每 1 秒轮询都重复打歌曲详情。
    if (same && complete)
        return;
    if (same && now - m_lastQueueRetryMs < 10000)
        return;
    if (same)
        m_lastQueueRetryMs = now;
    updateQueue(ids);
}

void ListenTogether::updateQueue(const QList<qint64> &ids)
{
    ++m_queueGeneration;
    const quint64 generation = m_queueGeneration;

    const QVariantList idValues = variantIds(ids);
    if (idValues != m_trackIds) {
        m_trackIds = idValues;
        emit tracksChanged();
        refreshPrivileges();
    }

    if (ids.isEmpty()) {
        if (!m_tracks.isEmpty()) {
            m_tracks.clear();
            emit tracksChanged();
        }
        return;
    }

    QHash<qint64, QVariantMap> known;
    for (const QVariant &value : std::as_const(m_tracks)) {
        const QVariantMap track = value.toMap();
        const qint64 id = variantId(track.value(QStringLiteral("id")));
        if (id > 0)
            known.insert(id, track);
    }

    QVariantList ordered;
    ordered.reserve(ids.size());
    for (qint64 id : ids) {
        if (known.contains(id))
            ordered.append(known.value(id));
    }
    if (ordered != m_tracks) {
        m_tracks = ordered;
        emit tracksChanged();
    }

    QList<qint64> missing;
    for (qint64 id : ids) {
        if (id > 0 && !known.contains(id) && !missing.contains(id))
            missing.append(id);
    }
    if (missing.isEmpty())
        return;

    fetchSongDetails(missing,
                     [this, generation, ids, known](bool /*ok*/,
                                                    const QHash<qint64, QVariantMap> &details) {
        if (!m_inRoom || generation != m_queueGeneration)
            return;
        QHash<qint64, QVariantMap> merged = known;
        for (auto it = details.constBegin(); it != details.constEnd(); ++it)
            merged.insert(it.key(), it.value());
        // 详情回包可能比指令补歌晚到：保留回调等待期间追加的曲目。
        for (const QVariant &value : std::as_const(m_tracks)) {
            const QVariantMap track = value.toMap();
            const qint64 id = variantId(track.value(QStringLiteral("id")));
            if (id > 0 && !merged.contains(id))
                merged.insert(id, track);
        }

        QSet<qint64> queuedIds;
        queuedIds.reserve(ids.size());
        for (qint64 id : ids)
            queuedIds.insert(id);

        QVariantList orderedTracks;
        orderedTracks.reserve(ids.size());
        for (qint64 id : ids) {
            if (merged.contains(id))
                orderedTracks.append(merged.value(id));
        }
        QSet<qint64> appendedExtras;
        for (const QVariant &value : std::as_const(m_tracks)) {
            const QVariantMap track = value.toMap();
            const qint64 id = variantId(track.value(QStringLiteral("id")));
            if (id > 0 && !queuedIds.contains(id) && !appendedExtras.contains(id)) {
                orderedTracks.append(track);
                appendedExtras.insert(id);
            }
        }
        if (orderedTracks == m_tracks)
            return;
        m_tracks = orderedTracks;
        qCDebug(lcListen) << "sync queue ready" << m_tracks.size() << "tracks";
        emit tracksChanged();
    });
}

void ListenTogether::fetchSongDetails(
    const QList<qint64> &ids,
    std::function<void(bool, const QHash<qint64, QVariantMap> &)> callback)
{
    sdk::fetchSongDetails(*m_client, ids, true, this,
        [callback = std::move(callback)](sdk::Result<QList<sdk::Track>> result) {
        QHash<qint64, QVariantMap> tracks;
        if (result) {
            for (const QVariant &value : sdk::tracksToVariantList(*result.value)) {
                const QVariantMap track = value.toMap();
                const qint64 id = variantId(track.value(QStringLiteral("id")));
                if (id > 0)
                    tracks.insert(id, track);
            }
        }
        callback(result.ok(), tracks);
    });
}

void ListenTogether::applyCommand(const sdk::ListenTogetherCommand &command)
{
    if (!m_inRoom || command.serverSeq <= m_lastServerSeq)
        return;
    // 自己的回声也推进序号，但不再应用。
    m_lastServerSeq = command.serverSeq;

    if (m_myUid > 0 && command.userId == m_myUid)
        return;

    const QString &type = command.commandType;
    if (!isCommandType(type, "GOTO") && !isCommandType(type, "PLAY")
        && !isCommandType(type, "PAUSE") && !isCommandType(type, "seek")
        && !isCommandType(type, "NEXT") && !isCommandType(type, "PREV")) {
        return;
    }

    if (command.targetSongId > 0 && !queueContains(command.targetSongId)) {
        const quint64 generation = m_generation;
        const sdk::ListenTogetherCommand pending = command;
        // 指令里的歌不在当前队列：先补这一首的详情，取不到也照样通知界面。
        fetchSongDetails({command.targetSongId},
                         [this, generation, pending](bool /*ok*/,
                                                     const QHash<qint64, QVariantMap> &details) {
            if (!m_inRoom || generation != m_generation)
                return;
            if (details.contains(pending.targetSongId)
                && !tracksContain(pending.targetSongId)) {
                m_tracks.append(details.value(pending.targetSongId));
                emit tracksChanged();
            }
            emitCommand(pending);
        });
        return;
    }

    emitCommand(command);
}

void ListenTogether::emitCommand(const sdk::ListenTogetherCommand &command)
{
    // 兼容 PLAY / PLAYING 两种写法；PAUSE 等其余值一律按未播放处理。
    const bool playing =
        command.playStatus.compare(QStringLiteral("PLAY"), Qt::CaseInsensitive) == 0
        || command.playStatus.compare(QStringLiteral("PLAYING"), Qt::CaseInsensitive) == 0;
    // 对方发指令时在 progress 处；正在放的话，到我们这里已经往前走了一段。serverSeq 是服务器收到
    // 指令的毫秒时间戳：在十分钟以内才认（不像时间戳、或时钟差得离谱时不补，按原位置）
    qint64 progress = command.progress;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (playing && command.serverSeq > 1000000000000LL && std::abs(now - command.serverSeq) < 600000)
        progress += std::max<qint64>(0, now - command.serverSeq);
    emit commandReceived(command.commandType, static_cast<double>(command.targetSongId),
                         static_cast<double>(progress), playing);
}

bool ListenTogether::expireIfGone(const sdk::Error &error)
{
    if (error.code != 488 || !m_inRoom)
        return false;
    clearRoom(true);
    setError(QStringLiteral("一起听已失效，可以邀请好友进入新的房间"));
    return true;
}

qint64 ListenTogether::otherUid() const
{
    for (const QVariant &value : m_users) {
        const qint64 uid = variantId(value.toMap().value(QStringLiteral("userId")));
        if (uid > 0 && uid != m_myUid)
            return uid;
    }
    return 0;
}

void ListenTogether::refreshStatistics()
{
    const qint64 other = otherUid();
    if (!m_inRoom || m_myUid <= 0 || other <= 0 || other == m_statisticsUid)
        return;
    m_statisticsUid = other;
    const quint64 generation = m_generation;
    sdk::fetchRelationStatistics(*m_client, m_roomId, {m_creatorId > 0 ? m_creatorId : m_myUid, other}, this,
                                 [this, generation, other](sdk::Result<sdk::ListenTogetherStatistics> result) {
        if (generation != m_generation || other != m_statisticsUid || !result)
            return;   // 取不到就不显示：这是锦上添花的一行，不提示错误
        m_statistics = {{"listenCount", static_cast<double>(result.value->listenCount)},
                        {"totalSeconds", static_cast<double>(result.value->totalConnectionTime)}};
        emit statisticsChanged();
    });
}

void ListenTogether::refreshPrivileges()
{
    const qint64 other = otherUid();
    QList<qint64> ids;
    for (const QVariant &value : std::as_const(m_trackIds)) {
        const qint64 id = variantId(value);
        if (id > 0)
            ids.append(id);
    }
    if (!m_inRoom || other <= 0 || ids.isEmpty())
        return;
    QStringList parts{QString::number(other)};
    for (qint64 id : std::as_const(ids))
        parts.append(QString::number(id));
    const QString key = parts.join(u',');
    if (key == m_privilegeKey)
        return;
    m_privilegeKey = key;
    const quint64 generation = m_generation;
    sdk::fetchPrivileges(*m_client, other, ids, this,
                         [this, generation, key](sdk::Result<QList<sdk::ListenTogetherPrivilege>> result) {
        if (generation != m_generation || key != m_privilegeKey)
            return;
        if (!result) {
            m_privilegeKey.clear();   // 下次歌单或成员变化时再查
            return;
        }
        QVariantList unplayable;
        for (const sdk::ListenTogetherPrivilege &privilege : std::as_const(*result.value)) {
            if (!privilege.playable && privilege.songId > 0)
                unplayable.append(static_cast<double>(privilege.songId));
        }
        if (unplayable != m_unplayableIds) {
            m_unplayableIds = unplayable;
            emit unplayableChanged();
        }
    });
}

void ListenTogether::onAudioTrackChanged()
{
    const qint64 target = currentSongId();
    const qint64 former = m_lastSongId;
    m_lastSongId = target;
    reportLocalCommand(QStringLiteral("GOTO"), former, target, audioProgressMs(),
                       m_audio->playing());
    updateShareUrl();
}

void ListenTogether::onAudioPlayingChanged()
{
    const bool playing = m_audio->playing();
    reportLocalCommand(playing ? QStringLiteral("PLAY") : QStringLiteral("PAUSE"), m_lastSongId,
                       currentSongId(), audioProgressMs(), playing);
}

void ListenTogether::onAudioSeeked()
{
    reportLocalCommand(QStringLiteral("seek"), m_lastSongId, currentSongId(), audioProgressMs(),
                       m_audio->playing());
}

void ListenTogether::reportLocalCommand(const QString &type, qint64 formerSongId,
                                        qint64 targetSongId, qint64 progressMs, bool playing)
{
    if (!canReport() || targetSongId <= 0)
        return;

    QJsonObject info;
    info.insert(QStringLiteral("commandType"), type);
    info.insert(QStringLiteral("progress"), static_cast<double>(progressMs));
    info.insert(QStringLiteral("playStatus"),
                playing ? QStringLiteral("PLAY") : QStringLiteral("PAUSE"));
    // The reference client sends these ids as strings; the server accepts both,
    // but strings preserve the exact API shape.
    info.insert(QStringLiteral("formerSongId"), QString::number(formerSongId));
    info.insert(QStringLiteral("targetSongId"), QString::number(targetSongId));
    info.insert(QStringLiteral("clientSeq"), static_cast<double>(++m_clientSeq));

    // 上报失败不打断播放，也不覆盖界面上的上一条错误。
    const quint64 generation = m_generation;
    sdk::reportPlayCommand(*m_client, m_roomId, info, this,
                           [this, generation](sdk::Result<sdk::ListenTogetherResult> result) {
        if (generation == m_generation && !result)
            expireIfGone(result.error);
    });
}

bool ListenTogether::canReport() const
{
    return m_inRoom && !m_remoteApplying && m_myUid > 0 && !m_roomId.isEmpty();
}

bool ListenTogether::queueComplete(const QList<qint64> &ids) const
{
    for (qint64 id : ids) {
        if (id > 0 && !tracksContain(id))
            return false;
    }
    return true;
}

bool ListenTogether::queueContains(qint64 songId) const
{
    for (const QVariant &value : m_trackIds) {
        if (variantId(value) == songId)
            return true;
    }
    return false;
}

bool ListenTogether::tracksContain(qint64 songId) const
{
    for (const QVariant &value : m_tracks) {
        if (variantId(value.toMap().value(QStringLiteral("id"))) == songId)
            return true;
    }
    return false;
}

qint64 ListenTogether::currentSongId() const
{
    return variantId(m_audio->track().value(QStringLiteral("id")));
}

qint64 ListenTogether::audioProgressMs() const
{
    const double position = m_audio->position();
    return position > 0 ? static_cast<qint64>(qRound64(position * 1000.0)) : 0;
}

void ListenTogether::syncQueue(const QVariantList &songIds)
{
    updateIdentity();
    if (!m_inRoom || m_roomId.isEmpty()) {
        setError(QStringLiteral("当前不在房间"));
        return;
    }
    if (!m_host) {
        setError(QStringLiteral("只有房主可以同步歌单"));
        return;
    }
    if (m_myUid <= 0) {
        setError(QStringLiteral("请先登录"));
        return;
    }

    clearError();
    const QList<qint64> ids = positiveIds(songIds);
    QJsonArray randomList;
    QJsonArray displayList;
    qint64 anchorPosition = -1;
    const qint64 nowPlaying = currentSongId();
    for (qsizetype i = 0; i < ids.size(); ++i) {
        const QString id = QString::number(ids.at(i));
        randomList.append(id);
        displayList.append(id);
        if (ids.at(i) == nowPlaying)
            anchorPosition = i;
    }

    // 服务器用 version 合并多方对歌单的修改：把同步回来的其他成员版本原样带上，
    // 自己的版本 +1；只带自己的版本时对方的列表不会发生变化。
    const qint64 playlistVersion = m_versions.value(m_myUid) + 1;
    m_playlistVersion = playlistVersion;
    QHash<qint64, qint64> versions = m_versions;
    versions.insert(m_myUid, playlistVersion);
    QJsonArray version;
    for (auto it = versions.cbegin(); it != versions.cend(); ++it) {
        version.append(QJsonObject{
            {QStringLiteral("userId"), static_cast<double>(it.key())},
            {QStringLiteral("version"), static_cast<double>(it.value())},
        });
    }

    const qint64 clientSeq = ++m_clientSeq;
    const quint64 syncRequestSeq = ++m_syncRequestSeq;
    QJsonObject playlistParam;
    playlistParam.insert(QStringLiteral("commandType"), QStringLiteral("REPLACE"));
    playlistParam.insert(QStringLiteral("version"), version);
    playlistParam.insert(QStringLiteral("anchorSongId"), QString::number(nowPlaying));
    playlistParam.insert(QStringLiteral("anchorPosition"), static_cast<double>(anchorPosition));
    playlistParam.insert(QStringLiteral("randomList"), randomList);
    playlistParam.insert(QStringLiteral("displayList"), displayList);
    playlistParam.insert(QStringLiteral("clientSeq"), static_cast<double>(clientSeq));
    if (!m_playMode.isEmpty())
        playlistParam.insert(QStringLiteral("playMode"), m_playMode);

    const quint64 generation = m_generation;
    sdk::reportPlaylist(*m_client, m_roomId, playlistParam, clientSeq, this,
                        [this, generation, playlistVersion, syncRequestSeq, ids](sdk::Result<sdk::ListenTogetherResult> result) {
        // 后一次 syncQueue 已经发出时，旧回包不能再覆盖新的队列。
        if (generation != m_generation || playlistVersion != m_playlistVersion
            || syncRequestSeq != m_syncRequestSeq)
            return;
        if (!result || !result.value->success) {
            setError(!result ? apiErrorText(result.error, QStringLiteral("同步歌单失败"))
                             : result.value->message.isEmpty() ? QStringLiteral("同步歌单失败")
                                                               : result.value->message);
            return;
        }
        m_versions.insert(m_myUid, playlistVersion);
        clearError();
        updateQueue(ids);
    });
}

void ListenTogether::inviteUser(double userId)
{
    updateIdentity();
    const qint64 uid = qRound64(userId);
    if (!m_inRoom || m_roomId.isEmpty()) {
        setError(QStringLiteral("先开始一起听，才能邀请好友"));
        return;
    }
    if (uid <= 0) {
        setError(QStringLiteral("好友信息无效"));
        return;
    }
    if (m_busy)
        return;

    clearError();
    const quint64 generation = m_generation;
    const QString roomId = m_roomId;
    setBusy(true);
    sdk::inviteUsers(*m_client, roomId, QList<qint64>{uid}, this,
                     [this, generation, roomId, uid](sdk::Result<sdk::ListenTogetherResult> result) {
        if (generation != m_generation || roomId != m_roomId)
            return;
        setBusy(false);
        if (!result || !result.value->success) {
            setError(!result ? apiErrorText(result.error, QStringLiteral("邀请好友失败"))
                             : result.value->message.isEmpty() ? QStringLiteral("邀请好友失败")
                                                               : result.value->message);
            return;
        }
        qCDebug(lcListen) << "invited user" << uid << "to room" << roomId;
        emit userInvited(static_cast<double>(uid));
    });
}

void ListenTogether::setRemoteApplying(bool applying)
{
    if (applying) {
        m_remoteTimer->stop();
        if (!m_remoteApplying) {
            m_remoteApplying = true;
            emit remoteApplyingChanged();
        }
        return;
    }

    if (!m_remoteApplying) {
        m_remoteTimer->stop();
        return;
    }
    // 让 setTrack/seek 等属性信号先落地，再恢复本地上报。
    m_remoteTimer->start();
}

bool ListenTogether::parseJoinTarget(const QString &roomRef, const QString &inviterId,
                                     QString *roomId, qint64 *parsedInviterId,
                                     QString *error) const
{
    const QString ref = roomRef.trimmed();
    if (ref.isEmpty()) {
        *error = QStringLiteral("邀请链接或房间号无效");
        return false;
    }

    QString rawRoom;
    QString rawInviter = inviterId.trimmed();
    const bool looksLikeUrl = ref.contains(QStringLiteral("://")) || ref.contains(u'?')
        || ref.startsWith(QStringLiteral("st.music.163.com"), Qt::CaseInsensitive);

    if (looksLikeUrl) {
        const QUrl url(ref);
        if (!url.isValid()) {
            *error = QStringLiteral("邀请链接无效");
            return false;
        }
        const QUrlQuery params(url);
        rawRoom = params.queryItemValue(QStringLiteral("roomId"), QUrl::FullyDecoded).trimmed();
        const QString linkInviter =
            params.queryItemValue(QStringLiteral("inviterId"), QUrl::FullyDecoded).trimmed();
        if (!linkInviter.isEmpty())
            rawInviter = linkInviter;
        if (rawRoom.isEmpty()) {
            *error = QStringLiteral("邀请链接缺少房间信息");
            return false;
        }
    } else {
        rawRoom = ref;
    }

    bool validRoom = !rawRoom.isEmpty();
    for (QChar c : rawRoom) {
        if (c.isSpace()) {
            validRoom = false;
            break;
        }
    }
    if (!validRoom) {
        *error = QStringLiteral("邀请链接或房间号无效");
        return false;
    }

    bool numeric = false;
    qint64 inviter = rawInviter.toLongLong(&numeric);
    if (!numeric) {
        const double value = rawInviter.toDouble(&numeric);
        if (numeric && value > 0 && value < 9.0e18)
            inviter = static_cast<qint64>(value);
        else
            numeric = false;
    }
    if (!numeric || inviter <= 0) {
        *error = QStringLiteral("邀请信息不完整");
        return false;
    }

    *roomId = rawRoom;
    *parsedInviterId = inviter;
    return true;
}

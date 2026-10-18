#include "listentogether.h"

#include "audioengine.h"
#include "json_util.h"
#include "ncmclient.h"
#include "sdk/listen_together.h"
#include "sdk/sdk_json.h"
#include "sdk/track.h"
#include "store.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <memory>
#include <utility>

namespace {

namespace sdk = ncm::sdk;

constexpr int SyncPollMs = 1000;
constexpr int StatusPollMs = 5000;
constexpr int HeartbeatPollMs = 5000;
constexpr int RemoteApplyHoldMs = 250;
constexpr int DetailBatch = 250;

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
            {"identity", user.identity},
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
    clearError();

    emit stateChanged();
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

void ListenTogether::fetchSyncPlaylist()
{
    if (!m_inRoom || m_syncInFlight || m_roomId.isEmpty())
        return;

    const quint64 generation = m_generation;
    const QString roomId = m_roomId;
    m_syncInFlight = true;
    sdk::fetchSyncPlaylist(*m_client, roomId, this,
                           [this, generation, roomId](sdk::Result<sdk::ListenTogetherSync> result) {
        if (generation != m_generation || roomId != m_roomId)
            return;
        m_syncInFlight = false;
        if (!result)
            return;
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
    sdk::reportHeartbeat(*m_client, roomId, songId, playStatus, progress, this,
                         [this, generation, roomId](sdk::Result<sdk::ListenTogetherResult>) {
        if (generation != m_generation || roomId != m_roomId)
            return;
        m_heartbeatInFlight = false;
    });
}

void ListenTogether::applySync(const sdk::ListenTogetherSync &sync)
{
    applyPlaylist(sync.playlist);
    if (sync.command)
        applyCommand(*sync.command);
}

void ListenTogether::applyPlaylist(const sdk::ListenTogetherPlaylist &playlist)
{
    // trackIds 原样对应服务器的 displayList；非正数只用来占位，不参与取详情。
    QList<qint64> ids = playlist.displayList;
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
        emit tracksChanged();
    });
}

void ListenTogether::fetchSongDetails(
    const QList<qint64> &ids,
    std::function<void(bool, const QHash<qint64, QVariantMap> &)> callback)
{
    if (ids.isEmpty()) {
        callback(true, {});
        return;
    }

    struct State {
        QHash<qint64, QVariantMap> tracks;
        int remaining = 0;
        bool ok = true;
    };
    const auto state = std::make_shared<State>();
    QPointer<ListenTogether> self(this);

    // GET 的 URL 长度有限；与 Store 的补齐逻辑一致，每批最多 250 个 id。
    for (qsizetype start = 0; start < ids.size(); start += DetailBatch) {
        const qsizetype end = std::min<qsizetype>(ids.size(), start + DetailBatch);
        QStringList fields;
        fields.reserve(end - start);
        for (qsizetype i = start; i < end; ++i)
            fields.append(QStringLiteral("{\"id\":%1}").arg(ids.at(i)));
        const QString c = QStringLiteral("[") + fields.join(u',') + QStringLiteral("]");

        ++state->remaining;
        m_client->get(QStringLiteral("api/v3/song/detail"), ncm::query({{"c", c}}), true,
                      [self, state, callback](const QJsonDocument &doc, bool ok) {
            if (!self)
                return;

            sdk::JsonReader reader(doc.object(), QStringLiteral("songDetail"));
            const qint64 code = reader.integer("code", 200);
            const QJsonArray songs = reader.array("songs");
            const QJsonArray privileges = reader.array("privileges");
            if (!ok || (reader.has("code") && code != 200) || !reader.has("songs")
                || !reader.ok()) {
                state->ok = false;
            } else {
                const auto parsed = sdk::parseTracks(songs, privileges);
                if (parsed) {
                    for (const QVariant &value : sdk::tracksToVariantList(*parsed.value)) {
                        const QVariantMap track = value.toMap();
                        const qint64 id = variantId(track.value(QStringLiteral("id")));
                        if (id > 0)
                            state->tracks.insert(id, track);
                    }
                } else {
                    state->ok = false;
                }
            }
            if (--state->remaining == 0)
                callback(state->ok, state->tracks);
        });
    }
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
        && !isCommandType(type, "PAUSE") && !isCommandType(type, "seek")) {
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
    emit commandReceived(command.commandType, static_cast<double>(command.targetSongId),
                         static_cast<double>(command.progress), playing);
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
    sdk::reportPlayCommand(*m_client, m_roomId, info, this,
                           [](sdk::Result<sdk::ListenTogetherResult>) {});
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
    for (qint64 id : ids) {
        randomList.append(QString::number(id));
        displayList.append(QString::number(id));
    }

    const qint64 playlistVersion = ++m_playlistVersion;
    QJsonObject versionEntry;
    versionEntry.insert(QStringLiteral("userId"), static_cast<double>(m_myUid));
    versionEntry.insert(QStringLiteral("version"), static_cast<double>(playlistVersion));
    QJsonArray version;
    version.append(versionEntry);

    QJsonObject playlistParam;
    playlistParam.insert(QStringLiteral("commandType"), QStringLiteral("REPLACE"));
    playlistParam.insert(QStringLiteral("version"), version);
    playlistParam.insert(QStringLiteral("anchorSongId"), QString());
    playlistParam.insert(QStringLiteral("anchorPosition"), -1.0);
    playlistParam.insert(QStringLiteral("randomList"), randomList);
    playlistParam.insert(QStringLiteral("displayList"), displayList);

    const quint64 generation = m_generation;
    sdk::reportPlaylist(*m_client, m_roomId, playlistParam, this,
                        [this, generation, playlistVersion,
                         ids](sdk::Result<sdk::ListenTogetherResult> result) {
        // 后一次 syncQueue 已经发出时，旧回包不能再覆盖新的队列。
        if (generation != m_generation || playlistVersion != m_playlistVersion)
            return;
        if (!result) {
            setError(apiErrorText(result.error, QStringLiteral("同步歌单失败")));
            return;
        }
        if (!result.value->success) {
            setError(result.value->message.isEmpty() ? QStringLiteral("同步歌单失败")
                                                     : result.value->message);
            return;
        }
        clearError();
        updateQueue(ids);
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

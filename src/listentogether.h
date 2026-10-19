#pragma once
// ListenTogether：一起听房间的 QML 接缝。房间接口走 ncm::sdk 的
// listen_together 服务；歌曲详情走 NcmClient（SDK 没有该服务），字段读取仍用
// SDK 的 JsonReader，归一化用 ncmTracks。这里只维护房间状态、轮询节奏与播放指令的收发。
//
// 轮询：同步歌单 1s、房间状态 5s、心跳 5s，各自用 in-flight 标志防止重叠。
// 服务器 playCommand 按 serverSeq 单调应用；本地播放变化反向上报，但远端正在
// 应用时（remoteApplying）不回声。
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class AudioEngine;
class NcmClient;
class Store;
class QTimer;

namespace ncm::sdk {
struct ListenTogetherCommand;
struct ListenTogetherPlaylist;
struct ListenTogetherRoom;
struct ListenTogetherSync;
}

class ListenTogether : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool inRoom READ inRoom NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool host READ host NOTIFY stateChanged)
    Q_PROPERTY(bool remoteApplying READ remoteApplying NOTIFY remoteApplyingChanged)
    Q_PROPERTY(QString roomId READ roomId NOTIFY stateChanged)
    Q_PROPERTY(double inviterId READ inviterId NOTIFY stateChanged)
    Q_PROPERTY(double creatorId READ creatorId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList users READ users NOTIFY usersChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList trackIds READ trackIds NOTIFY tracksChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString shareUrl READ shareUrl NOTIFY stateChanged)
public:
    explicit ListenTogether(NcmClient *client, Store *store, AudioEngine *audio,
                            QObject *parent = nullptr);

    bool inRoom() const { return m_inRoom; }
    bool busy() const { return m_busy; }
    bool host() const { return m_host; }
    bool remoteApplying() const { return m_remoteApplying; }
    QString roomId() const { return m_roomId; }
    double inviterId() const { return static_cast<double>(m_inviterId); }
    double creatorId() const { return static_cast<double>(m_creatorId); }
    QVariantList users() const { return m_users; }
    QVariantList tracks() const { return m_tracks; }
    QVariantList trackIds() const { return m_trackIds; }
    QString error() const { return m_error; }
    QString shareUrl() const { return m_shareUrl; }

    Q_INVOKABLE void createRoom();
    // roomRef 可以是分享链接（roomId/inviterId 查询参数）或裸房间号。
    Q_INVOKABLE void joinRoom(const QString &roomRef, const QString &inviterId = {});
    Q_INVOKABLE void leaveRoom();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void syncQueue(const QVariantList &songIds);
    // 邀请互关好友；房间必须已创建，调用方可在未入房时先 createRoom()。
    Q_INVOKABLE void inviteUser(double userId);
    Q_INVOKABLE void setRemoteApplying(bool applying);

signals:
    void stateChanged();
    void busyChanged();
    void usersChanged();
    void tracksChanged();
    void remoteApplyingChanged();
    void errorChanged();
    void leftRoom();
    void userInvited(double userId);
    // 服务器下发的播放指令；type 保留服务器原样（QML 自己转小写）。
    void commandReceived(QString type, double songId, double progressMs, bool playing);

private:
    void setBusy(bool busy);
    void setError(const QString &error);
    void clearError();
    bool ensureLogin();
    void updateIdentity();
    void updateShareUrl();
    QString buildShareUrl() const;

    void adoptRoom(const ncm::sdk::ListenTogetherRoom &room, qint64 inviterId);
    void clearRoom(bool emitLeft);
    void startTimers();
    void stopTimers();

    void refreshStatus();
    QJsonObject playlistContext() const;
    QJsonArray playlistVersions() const;
    void fetchSyncPlaylist();
    void sendHeartbeat();
    void applySync(const ncm::sdk::ListenTogetherSync &sync);
    void applyPlaylist(const ncm::sdk::ListenTogetherPlaylist &playlist);
    void applyCommand(const ncm::sdk::ListenTogetherCommand &command);
    void emitCommand(const ncm::sdk::ListenTogetherCommand &command);

    void updateQueue(const QList<qint64> &ids);
    void fetchSongDetails(const QList<qint64> &ids,
                          std::function<void(bool, const QHash<qint64, QVariantMap> &)> callback);

    void onAudioTrackChanged();
    void onAudioPlayingChanged();
    void onAudioSeeked();
    void reportLocalCommand(const QString &type, qint64 formerSongId, qint64 targetSongId,
                            qint64 progressMs, bool playing);
    bool canReport() const;
    bool queueContains(qint64 songId) const;
    bool tracksContain(qint64 songId) const;
    qint64 currentSongId() const;
    qint64 audioProgressMs() const;

    bool parseJoinTarget(const QString &roomRef, const QString &inviterId,
                         QString *roomId, qint64 *parsedInviterId, QString *error) const;

    NcmClient *m_client;
    Store *m_store;
    AudioEngine *m_audio;

    QTimer *m_syncTimer = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_remoteTimer = nullptr;

    bool m_inRoom = false;
    bool m_busy = false;
    bool m_host = false;
    bool m_remoteApplying = false;
    QString m_roomId;
    qint64 m_inviterId = 0;
    qint64 m_creatorId = 0;
    QVariantList m_users;
    QVariantList m_tracks;
    QVariantList m_trackIds;
    QString m_error;
    QString m_shareUrl;

    qint64 m_myUid = 0;
    // 房间代次：换房/退房/登出后，旧回包一律丢弃。
    quint64 m_generation = 0;
    // 用户动作代次：登出会作废在途的 create/join/leave 回包。
    quint64 m_actionGeneration = 0;
    // 歌单详情代次：新的 displayList 让旧的歌曲详情回包失效。
    quint64 m_queueGeneration = 0;

    bool m_syncInFlight = false;
    bool m_statusInFlight = false;
    bool m_heartbeatInFlight = false;

    qint64 m_lastServerSeq = 0;
    qint64 m_playlistVersion = 0;      // 本机最近一次发出的歌单版本（迟到的回包不覆盖新的）
    // 各成员的歌单版本（userId → version，同步回包里的）与播放模式：改歌单时原样带回，自己的 +1。
    // 只带自己的版本时服务器不认这次改动，对方的列表就不变
    QHash<qint64, qint64> m_versions;
    QString m_playMode;
    qint64 m_clientSeq = 0;
    quint64 m_syncRequestSeq = 0;   // 每次上报歌单递增，迟到的旧回包不能覆盖新队列
    qint64 m_lastSongId = 0;
    qint64 m_lastQueueRetryMs = 0;
    bool queueComplete(const QList<qint64> &ids) const;
};

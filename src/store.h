#pragma once
// Store：QML 的数据接缝（prototype/Source.qml 消费 data / personal / request / refresh）。
// 形状定义在 scripts/snapshot.py 的归一化函数里（src/normalize.cpp 移植），契约由
// scripts/check_contract.py 把关。
#include "ncmclient.h"
#include "querycache.h"
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <memory>
#include <QVariantMap>
#include <functional>

class NcmClient;
class QNetworkInformation;

class Store : public QObject {
    Q_OBJECT
    // 与 Data.js / Personal.js 同形；没有 MUSIC_U 时 personal 是匿名替身。
    // 骨架字段（toplists / lyrics / daily / account …）在构造时就位，QML 绑定不会踩到缺键。
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap personal READ personal NOTIFY personalChanged)
    // 任一请求因网络原因（无连接、超时、DNS）失败后为 false；之后任一请求成功回 true
    Q_PROPERTY(bool online READ online NOTIFY onlineChanged)
    // 持有 MUSIC_U 但账号接口说没有会话（无 profile / 301）；登录成功或 refresh 拿到账号后回 false
    Q_PROPERTY(bool sessionExpired READ sessionExpired NOTIFY sessionExpiredChanged)
public:
    explicit Store(NcmClient *client, QObject *parent = nullptr);

    QVariantMap data() const { return m_data; }
    QVariantMap personal() const { return m_personal; }
    bool online() const { return m_online; }
    bool sessionExpired() const { return m_sessionExpired; }

    // 按需加载："lyrics:<id>"、"playlist:<id>"、"search:<query>"、"comments:<id>"；同 key 在途时去重
    Q_INVOKABLE void request(const QString &key);
    // 重新拉全部首屏数据（登录成功后调用）
    Q_INVOKABLE void refresh();
    // 红心：乐观更新 likedIds（界面立刻变），weapi 失败时改回并发 likeFailed
    Q_INVOKABLE void setLiked(double songId, bool liked);
    // 退出：先尽力通知服务器，然后无条件清本地凭据、个人数据与含个人数据的缓存
    Q_INVOKABLE void logout();

    // 歌单写操作：创建 / 收藏 / 加歌 / 移歌 / 改名 / 删除 / 排序。成功后同步本地 personal。
    Q_INVOKABLE void createPlaylist(const QString &name, bool privatePlaylist = false);
    Q_INVOKABLE void setPlaylistSubscribed(double id, bool subscribed);
    Q_INVOKABLE void addSongToPlaylist(double playlistId, double songId);
    Q_INVOKABLE void removeSongFromPlaylist(double playlistId, double songId);
    Q_INVOKABLE void renamePlaylist(double id, const QString &name);
    Q_INVOKABLE void deletePlaylist(double id);
    Q_INVOKABLE void reorderPlaylists(const QVariantList &ids);
    // 歌单内曲目排序：pid + 完整的新顺序 trackIds
    Q_INVOKABLE void reorderPlaylistTracks(double playlistId, const QVariantList &trackIds);
    // 关注歌手 / 收藏专辑：成功只发对应 changed，失败发 followFailed
    Q_INVOKABLE void setArtistSubscribed(double id, bool subscribed);
    Q_INVOKABLE void setAlbumSubscribed(double id, bool subscribed);
    // 评论点赞：乐观更新 comments / commentPage，失败回滚并发 commentLikeFailed
    Q_INVOKABLE void setCommentLiked(double songId, double commentId, bool liked);

public slots:
    // AudioEngine 打卡成功后调用：本地「最近播放」立刻反映刚听的歌，不必等下次 refresh
    void notePlayed(const QVariantMap &track);

    // --dump-store 用：发 refresh() 并等全部在途首屏请求落地（含兜底超时）
    void waitForInitial();

signals:
    void dataChanged();
    void personalChanged();
    void onlineChanged();
    void sessionExpiredChanged();
    void settled();
    // liked 是用户刚才的意图；失败时个人数据已经改回原状态
    void likeFailed(double songId, bool liked, const QString &message);
    void likeSucceeded(double songId, bool liked);
    void loggedOut();
    void playlistCreated(double id, const QString &name);
    void playlistChanged(double id);
    void playlistActionFailed(const QString &action, const QString &message);
    void artistChanged(double id, bool subscribed);
    void albumChanged(double id, bool subscribed);
    void followFailed(const QString &action, const QString &message);
    void commentLikeFailed(double songId, double commentId, const QString &message);
    // 只读信箱失败（登录失效 / 网络错误）；成功走 dataChanged
    void inboxFailed(const QString &message);
    // 只读/统计请求失败：key 与给用户看的一句话；成功走 dataChanged
    void requestFailed(const QString &key, const QString &message);

private slots:
    // NcmClient 每个请求的落地真相：收敛 online / sessionExpired 与欠账刷新
    void requestFinished(bool ok, bool networkFailure);
    // QNetworkInformation 的离线→在线沿：恢复后拉一次首屏（每次离线期间只欠一次）
    void connectionRegained();

private:
    // 发一条首屏请求并计数；waitForInitial 等计数归零
    void fetch(const QString &path, const QUrlQuery &query, bool personal, NcmClient::GetCallback callback,
               bool acceptNotLoggedIn = false);
    // 与 fetch 相同地计数，但无论成功失败都回调：批量请求要靠失败回调收尾，否则链会永久等待。
    void fetchAlways(const QString &path, const QUrlQuery &query, bool personal, NcmClient::GetCallback callback);
    void fetchAnonymous();
    void fetchAccount();
    void fetchPublicPersonal();
    void fetchAnonymousPlaylists();
    void patchPlaylistCard(double id, const QVariantList &tracks, const QString &description);
    void fetchDaily();
    void fetchUserLists(double uid);
    void fetchLiked(const QJsonObject &likedList);
    void fetchHeart(const QVariantMap &seed, const QJsonObject &likedList);
    void fetchRadar();
    void fetchTail();
    // 云盘分页的共享状态：递归回调只持有 state，不形成 shared_ptr 自环
    struct CloudState;
    void fetchCloudPage(const std::shared_ptr<CloudState> &state);
    // 歌单 / 红心的完整曲目：playlist.tracks 最多约 1000 条，其余用 trackIds + /api/v3/song/detail 补齐
    using TracksCallback = std::function<void(const QVariantList &, const QJsonObject &, bool)>;
    void fetchPlaylistTracks(double id, bool personal, TracksCallback callback);
    // 分批取歌曲详情（每批 500），按请求顺序返回；供歌单补齐与搜索封面补全共用
    void fetchSongDetails(const QList<double> &ids, bool personal,
                          std::function<void(const QHash<double, QVariantMap> &, bool)> callback);
    void resetPersonal();
    // 红心成功后同步 personal.liked.tracks（列表已加载时）：先找本地曲目，找不到再取详情
    void addLikedTrack(double songId);
    void removeLikedTrack(double songId);
    QVariantMap trackById(double songId) const;
    // 红心的本地写入与服务器请求：同一首歌在途时记住最后一次意图，回包落地后追发
    void sendLike(double songId, bool liked);
    void setLikedLocal(double songId, bool liked);
    bool isLiked(double songId) const;
    // 首屏快照落盘：只存首屏字段，搜索/评论/详情等按需数据不写进 screen:data
    void persistScreenData();
    // 个人请求失败的唯一收尾：分类、会话过期置位、每种失败固定一句给用户的话。
    // 返回空串表示服务器已接受（业务码 200）；分类只看传输结果与业务码，不看 message 文本。
    QString requestFailure(const NcmClient::Response &response);
    // 歌单写请求的共同部分：登录检查、epoch 防串号、错误消息
    void playlistWrite(const QString &path, const QJsonObject &payload, const QString &action,
                       std::function<void(const QJsonObject &)> onSuccess);
    void followWrite(const QString &path, const QJsonObject &payload, const QString &action,
                     std::function<void()> onSuccess);
    void requestLyrics(const QString &key, double songId);
    void requestPlaylist(const QString &key, double playlistId);
    void requestSearch(const QString &key, const QString &raw);
    void requestSearchUsers(const QString &key, const QString &raw);
    void requestSearchRadios(const QString &key, const QString &raw);
    void requestComments(const QString &key, double songId);
    void requestCommentPage(const QString &key, double songId, int offset);
    void requestArtist(const QString &key, double artistId);
    void requestArtistSongs(const QString &key, double artistId, int offset);
    void requestAlbum(const QString &key, double albumId);
    void requestMoreFm();
    void requestMv(const QString &key, double mvId);
    void requestUser(const QString &key, double uid);
    // -- 只读信箱：data.inbox / data.inboxThread；不落盘，退出/换号清空
    void requestInbox(int offset);
    void requestInboxThread(double userId, qint64 before);
    // -- 听歌足迹：data.listenTotal / listenRealtime / listenRank；仅登录后
    void requestListenTotal();
    void requestListenRealtime(const QString &type);
    void requestListenRank(const QString &type);
    // -- 通知计数：data.notifications；前端按分类做角标
    void requestNotifications();
    // -- 社交：关注/粉丝/等级/收藏计数；列表只读，不做关注写
    void requestUserFollows(double uid, int offset);
    void requestUserFolloweds(double uid, int offset);
    void requestUserLevel();
    void requestUserSubcount();
    // -- 评论楼中楼：data.commentReplies；只读
    void requestCommentReplies(double songId, double commentId, qint64 before);
    // -- 新版歌词：data.lyricsNew；yrc/klyric/romalrc + 普通 LRC
    void requestLyricsNew(double songId);
    // 首屏的 stale-while-revalidate：启动时先放上次的值，refresh() 的结果陆续覆盖
    void restoreScreen();
    QString personalKey() const;
    // 相似歌曲的种子（红心第一首，未登录退回热歌榜第一首）到齐后发一次
    void maybeFetchSimilar();

    // 归一化结果写回 m_data / m_personal 并 NOTIFY
    void put(const char *key, const QVariant &value, bool personalSide);
    void setOnline(bool online);
    void setSessionExpired(bool expired);

    NcmClient *m_client;
    QVariantMap m_data;
    QVariantMap m_personal;
    QVariantMap m_emptyPersonal;     // 个人骨架（换账号时清回它）
    QVariantMap m_similarSeed;
    bool m_similarFired = false;
    bool m_online = true;
    bool m_sessionExpired = false;
    QNetworkInformation *m_netInfo = nullptr;
    bool m_refreshOwed = false;      // 离线期间欠下的恢复刷新；到账后只还一次
    QueryCache m_cache;              // 按需数据与首屏快照的缓存（~/.cache/yunting/queries.json）
    int m_pending = 0;               // 首屏在途请求计数
    quint64 m_epoch = 0;             // 退出登录/换账号时递增，旧账号的回包不再写入
    quint64 m_searchGeneration = 0;   // 每次搜索递增：旧搜索的晚到结果不能覆盖新词
    double m_accountUid = 0;         // 上次拉到的账号 userId（昵称不可靠，换账号看它）
    struct LikeState {
        bool serverState = false;   // 服务器已确认的状态
        bool desired = false;       // 用户最后一次意图
    };
    QHash<double, LikeState> m_likes;   // 同一首歌的红心请求在途时排队最后一次意图
    quint64 m_likeGeneration = 0;       // 每次红心改动递增：旧的 like/get 快照要与本地合并
    QSet<double> m_likeTouched;         // 上次快照之后改过的歌
    QSet<QString> m_commentLikePending; // songId:commentId 在途，防连点重复发
};

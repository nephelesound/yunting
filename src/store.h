#pragma once
// Store：QML 的数据接缝（prototype/Source.qml 消费 data / personal / request / refresh）。
// 形状定义在 scripts/snapshot.py 的归一化函数里（src/normalize.cpp 移植），契约由
// scripts/check_contract.py 把关。
#include "ncmclient.h"
#include "querycache.h"
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

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

    // 按需加载："lyrics:<id>"、"playlist:<id>"、"search:<query>"；同 key 在途时去重
    Q_INVOKABLE void request(const QString &key);
    // 重新拉全部首屏数据（登录成功后调用）
    Q_INVOKABLE void refresh();

    // --dump-store 用：发 refresh() 并等全部在途首屏请求落地（含兜底超时）
    void waitForInitial();

signals:
    void dataChanged();
    void personalChanged();
    void onlineChanged();
    void sessionExpiredChanged();
    void settled();

private slots:
    // NcmClient 每个请求的落地真相：收敛 online / sessionExpired 与欠账刷新
    void requestFinished(bool ok, bool networkFailure);
    // QNetworkInformation 的离线→在线沿：恢复后拉一次首屏（每次离线期间只欠一次）
    void connectionRegained();

private:
    // 发一条首屏请求并计数；waitForInitial 等计数归零
    void fetch(const QString &path, const QUrlQuery &query, bool personal, NcmClient::GetCallback callback,
               bool acceptNotLoggedIn = false);
    void fetchAnonymous();
    void fetchAccount();
    void fetchPublicPersonal();
    void fetchAnonymousPlaylists();
    void patchPlaylistCard(double id, const QVariantList &tracks, const QString &description);
    void fetchDaily();
    void fetchUserLists(double uid);
    void fetchLiked(const QJsonObject &likedList);
    void fetchHeart(const QVariantMap &seed, const QJsonObject &likedList);
    void fetchRadar(const QVariantMap &seed);
    void fetchTail();
    void requestLyrics(const QString &key, double songId);
    void requestPlaylist(const QString &key, double playlistId);
    void requestSearch(const QString &key, const QString &raw);
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
};

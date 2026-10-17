#pragma once
// Store：QML 的数据接缝（prototype/Source.qml 消费 data / personal / request / refresh）。
// 形状定义在 scripts/snapshot.py 的归一化函数里（src/normalize.cpp 移植），契约由
// scripts/check_contract.py 把关。
#include "ncmclient.h"
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

class NcmClient;

class Store : public QObject {
    Q_OBJECT
    // 与 Data.js / Personal.js 同形；没有 MUSIC_U 时 personal 是匿名替身。
    // 骨架字段（toplists / lyrics / daily / account …）在构造时就位，QML 绑定不会踩到缺键。
    Q_PROPERTY(QVariantMap data READ data NOTIFY dataChanged)
    Q_PROPERTY(QVariantMap personal READ personal NOTIFY personalChanged)
public:
    explicit Store(NcmClient *client, QObject *parent = nullptr);

    QVariantMap data() const { return m_data; }
    QVariantMap personal() const { return m_personal; }

    // 按需加载："lyrics:<id>"、"playlist:<id>"、"search:<query>"；同 key 在途时去重
    Q_INVOKABLE void request(const QString &key);
    // 重新拉全部首屏数据（登录成功后调用）
    Q_INVOKABLE void refresh();

    // --dump-store 用：发 refresh() 并等全部在途首屏请求落地（含兜底超时）
    void waitForInitial();

signals:
    void dataChanged();
    void personalChanged();
    void settled();

private:
    // 发一条首屏请求并计数；waitForInitial 等计数归零。
    // sharedKey 非空时，回调落地后递减 request() 的在途引用计数（search 一次发四条）
    void fetch(const QString &path, const QUrlQuery &query, bool personal,
               NcmClient::GetCallback callback, const QString &sharedKey = {});
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
    void fetchPlaylistDetail(double playlistId);
    void fetchLyrics(double songId);
    void fetchSearch(const QString &raw, const QString &sharedKey);
    // 相似歌曲的种子（红心第一首，未登录退回热歌榜第一首）到齐后发一次
    void maybeFetchSimilar();

    // 归一化结果写回 m_data / m_personal 并 NOTIFY
    void put(const char *key, const QVariant &value, bool personalSide);

    NcmClient *m_client;
    QVariantMap m_data;
    QVariantMap m_personal;
    QVariantMap m_similarSeed;
    bool m_similarFired = false;
    QMap<QString, int> m_inFlight;   // request(key) 的在途引用计数（search 一次四条）
    int m_pending = 0;               // 首屏在途请求计数
};

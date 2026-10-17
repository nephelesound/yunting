#pragma once
// QueryCache：按 key 缓存接口结果，策略是 stale-while-revalidate（与前端的 SWR 同义）：
//   - 有缓存就先给（哪怕过期），过期了再在后台重取；新鲜的不发请求
//   - 同一个 key 在途时不重复发请求，回来后通知所有等待者
//   - 新鲜期按 key 的种类定（见 querycache.cpp 的 freshFor），过期仍先给旧值
//   - 落盘用 SQLite，一行一个 key：启动不解析整库，内存未命中时只读那一行，
//     写入只改脏行；值总量超过上限时按 last_used 逐出最久未用的行
// 只管「何时取、取到放哪」，不知道接口长什么样：取数函数由调用方给。
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariant>
#include <functional>
#include <optional>

class QSqlDatabase;

class QueryCache : public QObject {
    Q_OBJECT
public:
    // fetch 取数函数：拿到结果后调用 done(value, ok)；ok=false 时保留旧值
    using Done = std::function<void(const QVariant &value, bool ok)>;
    using Fetcher = std::function<void(Done done)>;
    // 每次有值可用（缓存命中或新取到）都会调用；可能先给旧值、再给新值
    using Deliver = std::function<void(const QVariant &value)>;

    // 落盘数据的格式版本（存在 SQLite 的 PRAGMA user_version 里）。缓存里存的是解析好的结果
    // （normalize.cpp 的输出），解析规则或数据形状一变，旧值就不对了（歌词永不过期，
    // 会一直用下去）。改了解析就把它加一：版本不符的库清空。
    static constexpr int diskFormat = 2;

    explicit QueryCache(const QString &diskPath, QObject *parent = nullptr);
    ~QueryCache() override;

    void query(const QString &key, const Fetcher &fetch, const Deliver &deliver);
    // 过期即失效、旧值绝不能再用到的数据（签名 URL）：不交付缓存值，结果也不入缓存；
    // 同一 key 在途时仍只发一次请求。
    void refresh(const QString &key, const Fetcher &fetch, const Deliver &deliver);

    // 首屏数据由 Store 的请求管线逐块拼成，不是一次 fetch：Store 自己写（remember）、启动时先读（peek）
    QVariant peek(const QString &key) const;
    void remember(const QString &key, const QVariant &value);
    // 写操作成功后让对应结果失效，下一次 query 重新取（歌单写后详情不能再用旧快照）
    void forget(const QString &key);
    // 退出登录：丢掉所有含个人数据的结果（歌单详情、搜索历史之外的缓存也一并清掉）
    void clear();

signals:
    void fetchFailed(const QString &key);

private:
    struct Entry {
        QVariant value;
        qint64 fetchedAt = 0;   // UTC 秒
        qint64 lastUsed = 0;
    };
    static qint64 freshFor(const QString &key);   // 秒；<0 = 永不过期
    static bool persistent(const QString &key);
    static bool isFresh(const QString &key, const Entry &entry);
    // 内存优先，未命中时只从库里读这一行；不动内存（peek 是只读的）
    std::optional<Entry> read(const QString &key) const;
    // 记一次使用：更新内存里的 lastUsed；落盘的行按分钟粒度记一次，避免每次命中都重写值
    void touch(const QString &key, Entry entry);
    // query / refresh 的共同部分：在途去重后执行 fetch；keep=false 时不读写 m_entries
    void startFetch(const QString &key, const Fetcher &fetch, const Deliver &deliver, bool keep);
    bool openDatabase();
    void removeLegacyFile();
    void scheduleFlush();
    void flush();
    void evict(QSqlDatabase &db);

    QString m_diskPath;
    QString m_connection;   // 空 = 库打不开，只做内存缓存
    QHash<QString, Entry> m_entries;
    QHash<QString, QList<Deliver>> m_waiting;   // 在途 key → 等结果的人
    QSet<QString> m_dirty;     // 值变了、待写回的行
    QSet<QString> m_touched;   // 只有 lastUsed 变了、待写回的行
    qint64 m_bytes = -1;       // 库里值的总字节数；-1 = 还没算过
    bool m_flushScheduled = false;
    quint64 m_generation = 0;   // clear() 之后，旧请求的回包不再写回
};

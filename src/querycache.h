#pragma once
// QueryCache：按 key 缓存接口结果，策略是 stale-while-revalidate（与前端的 SWR 同义）：
//   - 有缓存就先给（哪怕过期），过期了再在后台重取；新鲜的不发请求
//   - 同一个 key 在途时不重复发请求，回来后通知所有等待者
//   - 新鲜期按 key 的种类定（见 querycache.cpp 的 freshFor）；可选落盘，重启后仍可用
// 只管「何时取、取到放哪」，不知道接口长什么样：取数函数由调用方给。
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariant>
#include <functional>

class QueryCache : public QObject {
    Q_OBJECT
public:
    // fetch 取数函数：拿到结果后调用 done(value, ok)；ok=false 时保留旧值
    using Done = std::function<void(const QVariant &value, bool ok)>;
    using Fetcher = std::function<void(Done done)>;
    // 每次有值可用（缓存命中或新取到）都会调用；可能先给旧值、再给新值
    using Deliver = std::function<void(const QVariant &value)>;

    explicit QueryCache(const QString &diskPath, QObject *parent = nullptr);
    ~QueryCache() override;

    void query(const QString &key, const Fetcher &fetch, const Deliver &deliver);

    // 首屏数据由 Store 的请求管线逐块拼成，不是一次 fetch：Store 自己写（remember）、启动时先读（peek）
    QVariant peek(const QString &key) const;
    void remember(const QString &key, const QVariant &value);

private:
    struct Entry {
        QVariant value;
        QDateTime fetchedAt;
    };
    static qint64 freshFor(const QString &key);   // 秒；<0 = 永不过期
    static bool persistent(const QString &key);
    bool isFresh(const QString &key) const;
    void load();
    void scheduleSave();
    void save() const;

    QString m_diskPath;
    QHash<QString, Entry> m_entries;
    QHash<QString, QList<Deliver>> m_waiting;   // 在途 key → 等结果的人
    bool m_saveScheduled = false;
};

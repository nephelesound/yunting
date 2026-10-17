#include "querycache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

QueryCache::QueryCache(const QString &diskPath, QObject *parent)
    : QObject(parent), m_diskPath(diskPath)
{
    load();
}

QueryCache::~QueryCache()
{
    if (m_saveScheduled)
        save();
}

// 新鲜期：歌词不会变，永久；歌单与首屏 10 分钟；搜索 5 分钟。过期仍先给旧值再后台重取
qint64 QueryCache::freshFor(const QString &key)
{
    if (key.startsWith(QLatin1String("lyrics:")))
        return -1;
    if (key.startsWith(QLatin1String("search:")))
        return 5 * 60;
    return 10 * 60;
}

// 落盘的只有不涉及隐私又值得留的：歌词与首屏；搜索词不写盘
// （search: / searchUsers: / searchRadios: 都算搜索）
bool QueryCache::persistent(const QString &key)
{
    return !key.startsWith(QLatin1String("search"));
}

bool QueryCache::isFresh(const QString &key) const
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd())
        return false;
    const qint64 ttl = freshFor(key);
    return ttl < 0 || it->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < ttl;
}

void QueryCache::query(const QString &key, const Fetcher &fetch, const Deliver &deliver)
{
    const auto cached = m_entries.constFind(key);
    if (cached != m_entries.constEnd())
        deliver(cached->value);          // 先给缓存（新鲜或过期都给）
    if (isFresh(key))
        return;                          // 新鲜：不发请求

    auto waiting = m_waiting.find(key);
    if (waiting != m_waiting.end()) {    // 已在途：排队等结果，不重复请求
        waiting->append(deliver);
        return;
    }
    m_waiting.insert(key, {deliver});
    const quint64 generation = m_generation;
    fetch([this, key, generation](const QVariant &value, bool ok) {
        if (generation != m_generation)
            return;                      // 期间退出登录：旧请求的结果属于上一个账号
        const QList<Deliver> waiters = m_waiting.take(key);
        if (!ok)
            return;                      // 失败：保留旧值（已经给过了）
        m_entries.insert(key, {value, QDateTime::currentDateTimeUtc()});
        if (persistent(key))
            scheduleSave();
        for (const Deliver &d : waiters)
            d(value);
    });
}

QVariant QueryCache::peek(const QString &key) const
{
    return m_entries.value(key).value;
}

void QueryCache::remember(const QString &key, const QVariant &value)
{
    m_entries.insert(key, {value, QDateTime::currentDateTimeUtc()});
    if (persistent(key))
        scheduleSave();
}

void QueryCache::forget(const QString &key)
{
    if (m_entries.remove(key) > 0 && persistent(key))
        scheduleSave();
}

void QueryCache::clear()
{
    ++m_generation;
    m_entries.clear();
    m_waiting.clear();
    scheduleSave();
}

void QueryCache::load()
{
    QFile f(m_diskPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject e = it.value().toObject();
        m_entries.insert(it.key(), {e.value("value").toVariant(),
                                    QDateTime::fromString(e.value("at").toString(), Qt::ISODate)});
    }
}

// 写盘合并成一次：一屏几十个结果陆续到达，不必每次都写
void QueryCache::scheduleSave()
{
    if (m_saveScheduled)
        return;
    m_saveScheduled = true;
    QTimer::singleShot(2000, this, [this] {
        m_saveScheduled = false;
        save();
    });
}

void QueryCache::save() const
{
    QJsonObject root;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
        if (persistent(it.key()))
            root.insert(it.key(), QJsonObject{{"value", QJsonValue::fromVariant(it->value)},
                                              {"at", it->fetchedAt.toString(Qt::ISODate)}});
    QDir().mkpath(QFileInfo(m_diskPath).path());
    QFile f(m_diskPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // 含个人数据
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

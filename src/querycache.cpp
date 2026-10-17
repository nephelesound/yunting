#include "querycache.h"
#include "perf.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace {
constexpr qint64 kDay = 24 * 60 * 60;
// 值（不含 SQLite 页开销）的总量上限；超过后按 last_used 逐出最久未用的行
constexpr qint64 kValueLimit = 50 * 1024 * 1024;
// 只更新 lastUsed 时按分钟聚合，避免每次命中都写一遍
constexpr qint64 kTouchResolution = 60;

std::optional<QVariant> decodeValue(const QByteArray &stored)
{
    const QJsonObject root = QJsonDocument::fromJson(stored).object();
    if (!root.contains(QStringLiteral("v")))
        return std::nullopt;
    return root.value(QStringLiteral("v")).toVariant();
}

QByteArray encodeValue(const QVariant &value)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("v"), QJsonValue::fromVariant(value)}})
        .toJson(QJsonDocument::Compact);
}

qint64 totalBytes(QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("SELECT COALESCE(SUM(LENGTH(value)), 0) FROM entries"))
        && query.next())
        return query.value(0).toLongLong();
    return 0;
}
} // namespace

QueryCache::QueryCache(const QString &diskPath, QObject *parent)
    : QObject(parent), m_diskPath(diskPath)
{
    removeLegacyFile();
    openDatabase();
}

QueryCache::~QueryCache()
{
    flush();
    if (m_connection.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        if (db.isOpen())
            db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

// 新鲜期按 key 的种类在这一处决定（key 的格式见 store*.cpp 的 request 路由）。
// 过期不等于丢弃：仍先给旧值再后台重取（query）。
// screen:* 是 Store 自己 refresh 覆盖的首屏/个人快照，不走 query；peek 不看新鲜期，
// 所以离线启动仍能用上次的快照，不需要为它们单独缩短寿命。个人数据由 clear() 在退出时清掉。
qint64 QueryCache::freshFor(const QString &key)
{
    if (key.startsWith(QLatin1String("lyrics:"))
        || key.startsWith(QLatin1String("lyricsNew:")))
        return -1;
    if (key.startsWith(QLatin1String("album:")))
        return 30 * kDay;
    if (key.startsWith(QLatin1String("artistSongs:"))
        || key.startsWith(QLatin1String("artist:"))
        || key.startsWith(QLatin1String("mv:")))
        return 7 * kDay;
    if (key.startsWith(QLatin1String("commentCursor:")))
        return 60 * 60;
    if (key.startsWith(QLatin1String("comments:"))) {
        // 热门评论 comments:<id>；分页 comments:<id>:<offset> 随时间追加，短一些
        return key.count(u':') >= 2 ? 60 * 60 : kDay;
    }
    // playlist: / user: / search*（不落盘）与其余按需数据：一天
    return kDay;
}

// 落盘的只有不涉及隐私又值得留的：歌词与首屏；搜索词不写盘
// （search: / searchUsers: / searchRadios: 都算搜索）
bool QueryCache::persistent(const QString &key)
{
    return !key.startsWith(QLatin1String("search"));
}

bool QueryCache::isFresh(const QString &key, const Entry &entry)
{
    const qint64 ttl = freshFor(key);
    return ttl < 0 || QDateTime::currentSecsSinceEpoch() - entry.fetchedAt < ttl;
}

std::optional<QueryCache::Entry> QueryCache::read(const QString &key) const
{
    if (const auto it = m_entries.constFind(key); it != m_entries.constEnd())
        return *it;
    if (m_connection.isEmpty())
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(m_connection, false));
    query.prepare(QStringLiteral("SELECT value, fetched_at, last_used FROM entries WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto value = decodeValue(query.value(0).toByteArray());
    if (!value)
        return std::nullopt;
    return Entry{*value, query.value(1).toLongLong(), query.value(2).toLongLong()};
}

void QueryCache::touch(const QString &key, Entry entry)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // 只更新 LRU 时间戳时单独走 UPDATE，不重写整行的值（歌单一行可能 100 KB）
    const bool persistUse = persistent(key) && now - entry.lastUsed >= kTouchResolution;
    entry.lastUsed = now;
    m_entries.insert(key, entry);
    if (persistUse) {
        m_touched.insert(key);
        scheduleFlush();
    }
}

void QueryCache::query(const QString &key, const Fetcher &fetch, const Deliver &deliver)
{
    const QString kind = key.section(u':', 0, 0);
    const auto cached = read(key);
    const bool fresh = cached && isFresh(key, *cached);
    qCDebug(lcPerf).nospace() << "cache kind=" << kind << " hit=" << cached.has_value()
                              << " fresh=" << fresh;
    if (cached) {
        touch(key, *cached);
        deliver(cached->value);          // 先给缓存（新鲜或过期都给）
        if (fresh)
            return;                      // 新鲜：不发请求
    }
    startFetch(key, fetch, deliver, true);
}

void QueryCache::refresh(const QString &key, const Fetcher &fetch, const Deliver &deliver)
{
    qCDebug(lcPerf).nospace() << "cache refresh kind=" << key.section(u':', 0, 0);
    startFetch(key, fetch, deliver, false);
}

void QueryCache::startFetch(const QString &key, const Fetcher &fetch, const Deliver &deliver,
                            bool keep)
{
    auto waiting = m_waiting.find(key);
    if (waiting != m_waiting.end()) {    // 已在途：排队等结果，不重复请求
        waiting->append(deliver);
        return;
    }
    m_waiting.insert(key, {deliver});
    const quint64 generation = m_generation;
    QElapsedTimer timer;
    timer.start();
    fetch([this, key, generation, timer, keep](const QVariant &value, bool ok) {
        if (generation != m_generation)
            return;                      // 期间退出登录：旧请求的结果属于上一个账号
        const QList<Deliver> waiters = m_waiting.take(key);
        if (!ok) {
            emit fetchFailed(key);       // 失败：保留旧值，通知界面可重试
            return;
        }
        if (keep) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            m_entries.insert(key, {value, now, now});
            if (persistent(key)) {
                m_dirty.insert(key);
                scheduleFlush();
            }
        }
        qCDebug(lcPerf).nospace() << "cache fetch kind=" << key.section(u':', 0, 0)
                                  << " ok=" << ok << " ms=" << timer.elapsed();
        for (const Deliver &d : waiters)
            d(value);
    });
}

QVariant QueryCache::peek(const QString &key) const
{
    const auto cached = read(key);
    return cached ? cached->value : QVariant();
}

void QueryCache::remember(const QString &key, const QVariant &value)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_entries.insert(key, {value, now, now});
    if (!persistent(key))
        return;
    m_dirty.insert(key);
    scheduleFlush();
}

void QueryCache::forget(const QString &key)
{
    m_entries.remove(key);
    m_dirty.remove(key);
    m_touched.remove(key);
    if (m_connection.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection, false));
    query.prepare(QStringLiteral("DELETE FROM entries WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec())
        qWarning() << "query cache: delete failed" << key << query.lastError().text();
    m_bytes = -1;   // 下次写入前重算
}

void QueryCache::clear()
{
    ++m_generation;
    m_entries.clear();
    m_waiting.clear();
    m_dirty.clear();
    m_touched.clear();
    m_bytes = 0;
    if (m_connection.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM entries"))) {
        qWarning() << "query cache: clear failed" << m_diskPath << query.lastError().text();
        m_bytes = -1;
        return;
    }
    // 退出登录清的是含个人数据的缓存：VACUUM 回收页，文件里不留下旧行
    if (!query.exec(QStringLiteral("VACUUM")))
        qWarning() << "query cache: vacuum failed" << m_diskPath << query.lastError().text();
    QFile::setPermissions(m_diskPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool QueryCache::openDatabase()
{
    QDir().mkpath(QFileInfo(m_diskPath).path());
    m_connection = QStringLiteral("querycache-%1")
                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        db.setDatabaseName(m_diskPath);
        if (!db.open()) {
            qWarning() << "query cache: cannot open" << m_diskPath << db.lastError().text();
        } else {
            // 缓存里有个人数据：与旧版 JSON 文件一样只给本用户读写
            QFile::setPermissions(m_diskPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QSqlQuery query(db);
            // 缓存丢了可以重建，不必为每次提交等 fsync
            query.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
            ok = query.exec(QStringLiteral(
                     "CREATE TABLE IF NOT EXISTS entries ("
                     "key TEXT PRIMARY KEY, value BLOB NOT NULL, "
                     "fetched_at INTEGER NOT NULL, last_used INTEGER NOT NULL)"))
                 && query.exec(QStringLiteral(
                     "CREATE INDEX IF NOT EXISTS entries_last_used ON entries(last_used)"));
            if (!ok) {
                qWarning() << "query cache: cannot create schema" << query.lastError().text();
            } else {
                int version = 0;
                if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
                    version = query.value(0).toInt();
                if (version != diskFormat) {
                    // 解析规则或数据形状变了：旧值不能再交给界面
                    query.exec(QStringLiteral("DELETE FROM entries"));
                    query.exec(QStringLiteral("PRAGMA user_version = %1").arg(diskFormat));
                }
            }
        }
    }
    if (!ok) {
        QSqlDatabase::removeDatabase(m_connection);
        m_connection.clear();
    }
    return ok;
}

void QueryCache::removeLegacyFile()
{
    const QFileInfo target(m_diskPath);
    if (target.fileName() != QLatin1String("queries.db"))
        return;   // 只清理固定缓存目录里的旧文件，别动调用方别处同名文件
    const QString path = target.dir().filePath(QStringLiteral("queries.json"));
    if (!QFileInfo::exists(path))
        return;
    // 旧版整份 JSON 缓存不迁移（它是缓存）：格式已经不同，留着只会占地方
    if (!QFile::remove(path))
        qWarning() << "query cache: cannot remove legacy cache" << path;
}

// 写盘合并成一次：一屏几十个结果陆续到达，不必每次都提交
void QueryCache::scheduleFlush()
{
    if (m_flushScheduled)
        return;
    m_flushScheduled = true;
    QTimer::singleShot(2000, this, [this] {
        m_flushScheduled = false;
        flush();
    });
}

void QueryCache::flush()
{
    if (m_dirty.isEmpty() && m_touched.isEmpty())
        return;
    if (m_connection.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    if (!db.isOpen())
        return;
    if (m_bytes < 0)
        m_bytes = totalBytes(db);

    const QSet<QString> dirty = m_dirty;
    const QSet<QString> touched = m_touched;
    m_dirty.clear();
    m_touched.clear();
    if (!db.transaction()) {
        m_dirty.unite(dirty);
        m_touched.unite(touched);
        return;
    }

    bool ok = true;
    for (const QString &key : dirty) {
        const auto it = m_entries.constFind(key);
        if (it == m_entries.constEnd())
            continue;
        qint64 oldBytes = 0;
        QSqlQuery old(db);
        old.prepare(QStringLiteral("SELECT LENGTH(value) FROM entries WHERE key = ?"));
        old.addBindValue(key);
        if (old.exec() && old.next())
            oldBytes = old.value(0).toLongLong();

        const QByteArray stored = encodeValue(it->value);
        QSqlQuery write(db);
        write.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO entries (key, value, fetched_at, last_used) VALUES (?, ?, ?, ?)"));
        write.addBindValue(key);
        write.addBindValue(stored);
        write.addBindValue(it->fetchedAt);
        write.addBindValue(it->lastUsed);
        if (!write.exec()) {
            qWarning() << "query cache: write failed" << key << write.lastError().text();
            ok = false;
            break;
        }
        m_bytes += stored.size() - oldBytes;
    }
    if (ok) {
        for (const QString &key : touched) {
            if (dirty.contains(key))
                continue;
            const auto it = m_entries.constFind(key);
            if (it == m_entries.constEnd())
                continue;
            QSqlQuery write(db);
            write.prepare(QStringLiteral("UPDATE entries SET last_used = ? WHERE key = ?"));
            write.addBindValue(it->lastUsed);
            write.addBindValue(key);
            if (!write.exec()) {
                qWarning() << "query cache: touch failed" << key << write.lastError().text();
                ok = false;
                break;
            }
        }
    }
    if (ok && db.commit()) {
        evict(db);
        return;
    }
    db.rollback();
    m_dirty.unite(dirty);
    m_touched.unite(touched);
    m_bytes = -1;   // 事务失败后字节数不再可信
}

// 逐出时连内存一起丢：否则内存里的旧值会绕开上限继续被命中
void QueryCache::evict(QSqlDatabase &db)
{
    while (m_bytes > kValueLimit) {
        QSqlQuery select(db);
        if (!select.exec(QStringLiteral(
                "SELECT key, LENGTH(value) FROM entries ORDER BY last_used ASC LIMIT 64")))
            return;
        QStringList keys;
        QVariantList keyValues;
        qint64 freed = 0;
        while (select.next()) {
            keys.append(select.value(0).toString());
            keyValues.append(select.value(0));
            freed += select.value(1).toLongLong();
        }
        if (keys.isEmpty()) {
            m_bytes = 0;
            return;
        }
        select.finish();

        QString placeholders;
        for (qsizetype i = 0; i < keys.size(); ++i)
            placeholders += i == 0 ? QStringLiteral("?") : QStringLiteral(",?");
        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("DELETE FROM entries WHERE key IN (%1)").arg(placeholders));
        for (const QVariant &key : keyValues)
            remove.addBindValue(key);
        if (!remove.exec())
            return;
        for (const QString &key : keys)
            m_entries.remove(key);
        m_bytes -= freed;
    }
}

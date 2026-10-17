// QueryCache 的行为测试：只经它的公开接口（query / peek / remember），取数函数用计数的假实现。
// 测新鲜期时直接改落盘行的生成时间：公开接口只会写「现在」。
#include "../querycache.h"

#include <QDateTime>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

class QueryCacheTest : public QObject {
    Q_OBJECT

    // 假取数：记次数；hold=true 时把 done 留着，由测试决定何时、以何结果回来
    struct FakeFetch {
        int calls = 0;
        bool hold = false;
        QueryCache::Done pending;
        QVariant next = 1;
        bool ok = true;
        QueryCache::Fetcher fn()
        {
            return [this](QueryCache::Done done) {
                ++calls;
                if (hold)
                    pending = done;
                else
                    done(next, ok);
            };
        }
    };

    static bool withDatabase(const QString &path, const std::function<bool(QSqlDatabase &)> &work)
    {
        const QString connection = QStringLiteral("querycache-test-%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(path);
            if (db.open())
                ok = work(db);
        }
        QSqlDatabase::removeDatabase(connection);
        return ok;
    }

    // 经公开接口写入并落盘，再把生成时间改到 ageSeconds 秒之前
    static bool seedAged(const QString &path, const QHash<QString, QVariant> &entries,
                         qint64 ageSeconds)
    {
        {
            QueryCache cache(path);
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
                cache.remember(it.key(), it.value());
        }
        return withDatabase(path, [&](QSqlDatabase &db) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("UPDATE entries SET fetched_at = ? WHERE key = ?"));
            for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
                query.bindValue(0, QDateTime::currentSecsSinceEpoch() - ageSeconds);
                query.bindValue(1, it.key());
                if (!query.exec() || query.numRowsAffected() != 1)
                    return false;
            }
            return true;
        });
    }

private slots:

    // 解析规则变了（旧版本或没写版本的文件）：整份丢弃，歌词这类永久缓存不会一直用旧的解析结果
    void otherFormatFileIsDiscarded()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        {
            QueryCache cache(path);
            cache.remember("lyrics:7", "[00:36.19]la");
        }
        QVERIFY(withDatabase(path, [](QSqlDatabase &db) {
            QSqlQuery query(db);
            return query.exec(QStringLiteral("PRAGMA user_version = 1"));
        }));
        QVERIFY(!QueryCache(path).peek("lyrics:7").isValid());
    }

    // 旧版整份 JSON 缓存不迁移：它只是缓存，留在盘上白占地方
    void legacyJsonCacheIsRemoved()
    {
        QTemporaryDir dir;
        const QString legacy = dir.filePath("queries.json");
        QFile file(legacy);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"_format\":2}");
        file.close();

        QueryCache cache(dir.filePath("queries.db"));
        QVERIFY(!QFile::exists(legacy));
    }

    void freshHitDoesNotFetchAgain()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        FakeFetch fetch;
        QVariantList got;
        const auto deliver = [&](const QVariant &v) { got.append(v); };
        cache.query("playlist:1", fetch.fn(), deliver);
        cache.query("playlist:1", fetch.fn(), deliver);
        QCOMPARE(fetch.calls, 1);
        QCOMPARE(got, (QVariantList{1, 1}));
    }

    // refresh：缓存里有旧值也不交付、新值也不入缓存（签名 URL 每次都要新取）
    void refreshNeverServesOrKeepsCachedValue()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        cache.remember("mvUrl:7", "old");
        FakeFetch fetch;
        fetch.next = "new";
        QVariantList got;
        cache.refresh("mvUrl:7", fetch.fn(), [&](const QVariant &v) { got.append(v); });
        QCOMPARE(fetch.calls, 1);
        QCOMPARE(got, (QVariantList{"new"}));
        QCOMPARE(cache.peek("mvUrl:7"), QVariant("old"));   // 刷新结果不写回缓存
    }

    void concurrentRequestsShareOneFetch()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        FakeFetch fetch;
        fetch.hold = true;
        int deliveredA = 0, deliveredB = 0;
        cache.query("search:x", fetch.fn(), [&](const QVariant &) { ++deliveredA; });
        cache.query("search:x", fetch.fn(), [&](const QVariant &) { ++deliveredB; });
        QCOMPARE(fetch.calls, 1);
        fetch.pending(42, true);
        QCOMPARE(deliveredA, 1);
        QCOMPARE(deliveredB, 1);
    }

    // 过期的缓存：先给旧值，再后台重取；重取失败时旧值原样保留
    void staleValueIsServedThenKeptWhenRevalidationFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        QVERIFY(seedAged(path, {{"playlist:1", "old"}}, 2 * 24 * 60 * 60));

        QueryCache cache(path);
        FakeFetch failing;
        failing.ok = false;
        QVariantList got;
        cache.query("playlist:1", failing.fn(), [&](const QVariant &v) { got.append(v); });
        QCOMPARE(failing.calls, 1);                 // 过期 → 重取
        QCOMPARE(got, (QVariantList{"old"}));       // 只拿到旧值，失败不覆盖
        QCOMPARE(cache.peek("playlist:1"), QVariant("old"));
    }

    void staleValueIsReplacedWhenRevalidationSucceeds()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        QVERIFY(seedAged(path, {{"playlist:1", "old"}}, 2 * 24 * 60 * 60));

        QueryCache cache(path);
        FakeFetch fetch;
        fetch.next = "new";
        QVariantList got;
        cache.query("playlist:1", fetch.fn(), [&](const QVariant &v) { got.append(v); });
        QCOMPARE(got, (QVariantList{"old", "new"}));   // 先旧后新
        QCOMPARE(cache.peek("playlist:1"), QVariant("new"));
    }

    // 新鲜期按 key 的种类：album 30 天。界内不发请求；界外先给旧值、同时发一次请求
    void freshnessFollowsKeyKind()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        QVERIFY(seedAged(path, {{"album:9", "old"}}, 20 * 24 * 60 * 60));
        {
            QueryCache cache(path);
            FakeFetch fetch;
            QVariantList got;
            cache.query("album:9", fetch.fn(), [&](const QVariant &v) { got.append(v); });
            QCOMPARE(fetch.calls, 0);                   // 20 天内：只给缓存
            QCOMPARE(got, (QVariantList{"old"}));
        }

        QVERIFY(seedAged(path, {{"album:9", "old"}}, 31 * 24 * 60 * 60));
        QueryCache cache(path);
        FakeFetch fetch;
        fetch.next = "new";
        QVariantList got;
        cache.query("album:9", fetch.fn(), [&](const QVariant &v) { got.append(v); });
        QCOMPARE(fetch.calls, 1);                       // 过期：发一次
        QCOMPARE(got, (QVariantList{"old", "new"}));    // 先旧后新
        QCOMPARE(cache.peek("album:9"), QVariant("new"));
    }

    void missingKeyFetchesAndFailureDeliversNothing()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        FakeFetch failing;
        failing.ok = false;
        int delivered = 0;
        cache.query("playlist:9", failing.fn(), [&](const QVariant &) { ++delivered; });
        QCOMPARE(failing.calls, 1);
        QCOMPARE(delivered, 0);
        QVERIFY(!cache.peek("playlist:9").isValid());
    }

    void clearDropsPersonalData()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        {
            QueryCache cache(path);
            cache.remember("playlist:1", "private");
            cache.remember("lyrics:1", "public");
            cache.clear();
            QVERIFY(!cache.peek("playlist:1").isValid());
            QVERIFY(!cache.peek("lyrics:1").isValid());
        }
        QueryCache reopened(path);
        QVERIFY(!reopened.peek("playlist:1").isValid());   // 库里也清了
    }

    void clearIgnoresInFlightResult()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        FakeFetch fetch;
        fetch.hold = true;
        int delivered = 0;
        cache.query("playlist:1", fetch.fn(), [&](const QVariant &) { ++delivered; });
        cache.clear();
        fetch.pending("old account", true);
        QCOMPARE(delivered, 0);
        QVERIFY(!cache.peek("playlist:1").isValid());
    }

    void lyricsSurviveRestartButSearchDoesNot()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("cache.db");
        {
            QueryCache cache(path);
            cache.remember("lyrics:7", "la la");
            cache.remember("search:secret", "result");
            cache.remember("searchUsers:someone", "result");
            cache.remember("searchRadios:station", "result");
        } // 析构时把待写的落盘
        QueryCache reopened(path);
        QCOMPARE(reopened.peek("lyrics:7"), QVariant("la la"));
        // 所有 search* 前缀都不写盘，不只 search:
        QVERIFY(!reopened.peek("search:secret").isValid());
        QVERIFY(!reopened.peek("searchUsers:someone").isValid());
        QVERIFY(!reopened.peek("searchRadios:station").isValid());
    }

    void forgetMakesTheNextQueryRefetch()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("cache.db"));
        FakeFetch fetch;
        int delivered = 0;
        cache.remember("playlist:1", "old");
        cache.forget("playlist:1");
        cache.query("playlist:1", fetch.fn(), [&](const QVariant &) { ++delivered; });
        QCOMPARE(fetch.calls, 1);      // 失效后不再命中
        QCOMPARE(delivered, 1);        // 只拿到新值
    }
};

QTEST_GUILESS_MAIN(QueryCacheTest)
#include "querycache_test.moc"

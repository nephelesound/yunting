// QueryCache 的行为测试：只经它的公开接口（query / peek / remember），取数函数用计数的假实现。
#include "../querycache.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

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

private slots:
    void freshHitDoesNotFetchAgain()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("q.json"));
        FakeFetch fetch;
        QVariantList got;
        const auto deliver = [&](const QVariant &v) { got.append(v); };
        cache.query("playlist:1", fetch.fn(), deliver);
        cache.query("playlist:1", fetch.fn(), deliver);
        QCOMPARE(fetch.calls, 1);
        QCOMPARE(got, (QVariantList{1, 1}));
    }

    void concurrentRequestsShareOneFetch()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("q.json"));
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
        const QString path = dir.filePath("q.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"playlist:1": {"value": "old", "at": "2000-01-01T00:00:00Z"}})");
        f.close();

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
        const QString path = dir.filePath("q.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"playlist:1": {"value": "old", "at": "2000-01-01T00:00:00Z"}})");
        f.close();

        QueryCache cache(path);
        FakeFetch fetch;
        fetch.next = "new";
        QVariantList got;
        cache.query("playlist:1", fetch.fn(), [&](const QVariant &v) { got.append(v); });
        QCOMPARE(got, (QVariantList{"old", "new"}));   // 先旧后新
        QCOMPARE(cache.peek("playlist:1"), QVariant("new"));
    }

    void missingKeyFetchesAndFailureDeliversNothing()
    {
        QTemporaryDir dir;
        QueryCache cache(dir.filePath("q.json"));
        FakeFetch failing;
        failing.ok = false;
        int delivered = 0;
        cache.query("playlist:9", failing.fn(), [&](const QVariant &) { ++delivered; });
        QCOMPARE(failing.calls, 1);
        QCOMPARE(delivered, 0);
        QVERIFY(!cache.peek("playlist:9").isValid());
    }

    void lyricsSurviveRestartButSearchDoesNot()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("q.json");
        {
            QueryCache cache(path);
            cache.remember("lyrics:7", "la la");
            cache.remember("search:secret", "result");
        } // 析构时把待写的落盘
        QueryCache reopened(path);
        QCOMPARE(reopened.peek("lyrics:7"), QVariant("la la"));
        QVERIFY(!reopened.peek("search:secret").isValid());   // 搜索词不写盘
    }
};

QTEST_GUILESS_MAIN(QueryCacheTest)
#include "querycache_test.moc"

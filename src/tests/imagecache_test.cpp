// 封面缓存按最近使用淘汰：看过的封面在缓存满了时留下，没再看的先删
#include "../imagecache.h"

#include <QDateTime>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class ImageCacheTest : public QObject {
    Q_OBJECT

    static QUrl url(const char *name) { return QUrl(QStringLiteral("https://p1.music.126.net/%1.jpg?param=800y800").arg(QLatin1String(name))); }
    static QDateTime refreshedAt(QNetworkDiskCache &cache, const QUrl &url)
    {
        return cache.metaData(url).attributes().value(QNetworkRequest::User).toDateTime();
    }

    // 前几天写下的条目：用普通的 QNetworkDiskCache 写，刷新时间由调用方给
    static void put(QNetworkDiskCache &cache, const QUrl &url, const QDateTime &refreshed = {})
    {
        QNetworkCacheMetaData meta;
        meta.setUrl(url);
        meta.setSaveToDisk(true);
        meta.setRawHeaders({{"Content-Type", "image/jpeg"}});   // 没有响应头的条目，读的时候会被当成坏文件删掉
        if (refreshed.isValid())
            meta.setAttributes({{QNetworkRequest::User, refreshed}});
        QIODevice *device = cache.prepare(meta);
        QVERIFY(device);
        device->write(QByteArray(10 * 1024, 'x'));
        cache.insert(device);
        QTest::qWait(20);   // 条目之间的创建时间要分得开
    }

    static bool read(QNetworkDiskCache &cache, const QUrl &url)
    {
        std::unique_ptr<QIODevice> device(cache.data(url));
        return device && device->readAll().size() == 10 * 1024;
    }

private slots:
    void readEntrySurvivesExpiry()
    {
        QTemporaryDir dir;
        {
            QNetworkDiskCache earlier;
            earlier.setCacheDirectory(dir.path());
            const QDateTime twoDaysAgo = QDateTime::currentDateTimeUtc().addDays(-2);
            put(earlier, url("a"), twoDaysAgo);
            put(earlier, url("b"), twoDaysAgo);
            put(earlier, url("c"));   // 旧版本写的条目没有刷新时间
        }
        CoverCache cache;
        cache.setCacheDirectory(dir.path());
        cache.setMaximumCacheSize(1024 * 1024);

        QVERIFY(read(cache, url("a")));   // 最早写入的 a 今天又看了一次

        cache.setMaximumCacheSize(25 * 1024);   // 只放得下两条：删掉最久没用的 b
        QVERIFY(read(cache, url("a")));
        QVERIFY(!read(cache, url("b")));
        QVERIFY(read(cache, url("c")));
    }

    void entryUsedTodayIsNotRewritten()
    {
        QTemporaryDir dir;
        const QDateTime earlierToday = QDateTime::currentDateTimeUtc().addSecs(-60);
        {
            QNetworkDiskCache earlier;
            earlier.setCacheDirectory(dir.path());
            put(earlier, url("a"), earlierToday);
        }
        CoverCache cache;
        cache.setCacheDirectory(dir.path());
        QVERIFY(read(cache, url("a")));
        QCOMPARE(refreshedAt(cache, url("a")), earlierToday);

        put(cache, url("new"));   // 新下载的条目一写进来就是今天用过的
        QVERIFY(refreshedAt(cache, url("new")).isValid());
    }
};

QTEST_GUILESS_MAIN(ImageCacheTest)
#include "imagecache_test.moc"

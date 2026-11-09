// 封面缓存按最近使用淘汰：看过的封面在缓存满了时留下，没再看的先删
#include "../imagecache.h"

#include <QBuffer>
#include <QDateTime>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

class ImageCacheTest : public QObject {
    Q_OBJECT

    static QUrl url(const char *name) { return QUrl(QStringLiteral("https://p1.music.126.net/%1.jpg?param=800y800").arg(QLatin1String(name))); }

    static void put(CoverCache &cache, const QUrl &url, const QDateTime &refreshedAt = {})
    {
        QNetworkCacheMetaData meta;
        meta.setUrl(url);
        meta.setSaveToDisk(true);
        meta.setRawHeaders({{"Content-Type", "image/jpeg"}});   // 没有响应头的条目，读的时候会被当成坏文件删掉
        if (refreshedAt.isValid())
            meta.setAttributes({{QNetworkRequest::User, refreshedAt}});
        QIODevice *device = cache.prepare(meta);
        QVERIFY(device);
        device->write(QByteArray(10 * 1024, 'x'));
        cache.insert(device);
        QTest::qWait(20);   // 条目之间的创建时间要分得开
    }

    static bool read(CoverCache &cache, const QUrl &url)
    {
        std::unique_ptr<QIODevice> device(cache.data(url));
        return device && device->readAll().size() == 10 * 1024;
    }

private slots:
    void readEntrySurvivesExpiry()
    {
        QTemporaryDir dir;
        CoverCache cache;
        cache.setCacheDirectory(dir.path());
        cache.setMaximumCacheSize(1024 * 1024);
        put(cache, url("a"));
        put(cache, url("b"));
        put(cache, url("c"));

        QVERIFY(read(cache, url("a")));   // 最早写入的 a 被看过一次

        cache.setMaximumCacheSize(25 * 1024);   // 只放得下两条：删掉最久没用的 b
        QVERIFY(read(cache, url("a")));
        QVERIFY(!read(cache, url("b")));
        QVERIFY(read(cache, url("c")));
    }

    void freshEntryIsNotRewritten()
    {
        QTemporaryDir dir;
        CoverCache cache;
        cache.setCacheDirectory(dir.path());
        const QDateTime earlier = QDateTime::currentDateTimeUtc().addSecs(-60);
        put(cache, url("a"), earlier);

        QVERIFY(read(cache, url("a")));
        QCOMPARE(cache.metaData(url("a")).attributes().value(QNetworkRequest::User).toDateTime(), earlier);
    }
};

QTEST_GUILESS_MAIN(ImageCacheTest)
#include "imagecache_test.moc"

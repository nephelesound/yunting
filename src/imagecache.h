#pragma once
// 封面图片的磁盘缓存：QML 的 Image 都经引擎的网络管理器取图，这里给它挂上磁盘缓存。
// 网易的封面响应带 max-age 一年，地址里又有图片指纹和尺寸参数，所以看过的封面一年内不再发请求，离线也能显示。
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlNetworkAccessManagerFactory>

// 按最近使用淘汰的磁盘缓存。QNetworkDiskCache 满了按文件的创建时间删最老的，常看的封面反而最先被删；
// 命中时重写一次条目（updateMetaData 会新建文件），它就成了最新的。重写一张大封面要几百 KB，所以一天最多一次
class CoverCache : public QNetworkDiskCache {
public:
    using QNetworkDiskCache::QNetworkDiskCache;

    QIODevice *data(const QUrl &url) override;
    QIODevice *prepare(const QNetworkCacheMetaData &metaData) override;
    void updateMetaData(const QNetworkCacheMetaData &metaData) override;

private:
    bool m_refreshing = false;   // updateMetaData 自己会再调 data()
};

class ImageCacheFactory : public QQmlNetworkAccessManagerFactory {
public:
    explicit ImageCacheFactory(QString cacheDir);
    QNetworkAccessManager *create(QObject *parent) override;

private:
    QString m_cacheDir;
};

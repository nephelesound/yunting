#pragma once
// 封面图片的磁盘缓存：QML 的 Image 都经引擎的网络管理器取图，这里给它挂上标准的 QNetworkDiskCache。
// 网易的封面地址里带图片指纹和尺寸参数，同一个地址的内容永远不变，所以优先用缓存（PreferCache）：
// 看过的封面再也不发请求，离线时也能显示。
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkRequest>
#include <QQmlNetworkAccessManagerFactory>
#include <QStandardPaths>

class ImageCacheFactory : public QQmlNetworkAccessManagerFactory {
    class CachingManager : public QNetworkAccessManager {
    public:
        using QNetworkAccessManager::QNetworkAccessManager;

    protected:
        QNetworkReply *createRequest(Operation op, const QNetworkRequest &original, QIODevice *data) override
        {
            QNetworkRequest req(original);
            req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
            return QNetworkAccessManager::createRequest(op, req, data);
        }
    };

public:
    QNetworkAccessManager *create(QObject *parent) override
    {
        auto *nam = new CachingManager(parent);
        auto *cache = new QNetworkDiskCache(nam);
        cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/images");
        cache->setMaximumCacheSize(512LL * 1024 * 1024);
        nam->setCache(cache);
        return nam;
    }
};

#include "imagecache.h"

#include <QDateTime>
#include <QNetworkRequest>

#include <utility>

namespace {
// 条目上次被刷新成「最新」的时刻，存在缓存元数据里
constexpr auto RefreshedAt = QNetworkRequest::User;
constexpr qint64 RefreshEverySecs = 24 * 60 * 60;
constexpr qint64 MaximumSize = 1024LL * 1024 * 1024;

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
} // namespace

QIODevice *CoverCache::data(const QUrl &url)
{
    QIODevice *device = QNetworkDiskCache::data(url);
    if (!device || m_refreshing)
        return device;

    QNetworkCacheMetaData meta = metaData(url);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime refreshed = meta.attributes().value(RefreshedAt).toDateTime();
    if (refreshed.isValid() && refreshed.secsTo(now) < RefreshEverySecs)
        return device;

    auto attributes = meta.attributes();
    attributes.insert(RefreshedAt, now);
    meta.setAttributes(attributes);
    m_refreshing = true;
    updateMetaData(meta);
    m_refreshing = false;
    return device;
}

ImageCacheFactory::ImageCacheFactory(QString cacheDir)
    : m_cacheDir(std::move(cacheDir))
{
}

QNetworkAccessManager *ImageCacheFactory::create(QObject *parent)
{
    auto *nam = new CachingManager(parent);
    auto *cache = new CoverCache(nam);
    cache->setCacheDirectory(m_cacheDir);
    cache->setMaximumCacheSize(MaximumSize);
    nam->setCache(cache);
    return nam;
}

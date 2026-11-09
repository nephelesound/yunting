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

    const QNetworkCacheMetaData meta = metaData(url);
    const QDateTime refreshed = meta.attributes().value(RefreshedAt).toDateTime();
    if (!refreshed.isValid() || refreshed.secsTo(QDateTime::currentDateTimeUtc()) >= RefreshEverySecs)
        updateMetaData(meta);   // prepare() 记上现在
    return device;
}

// 每次写条目（新下载的、刷新的、Qt 收到 304 改写的）都记上现在：刚写的条目就是最新的，第一次命中不必再重写
QIODevice *CoverCache::prepare(const QNetworkCacheMetaData &metaData)
{
    QNetworkCacheMetaData stamped = metaData;
    auto attributes = stamped.attributes();
    attributes.insert(RefreshedAt, QDateTime::currentDateTimeUtc());
    stamped.setAttributes(attributes);
    return QNetworkDiskCache::prepare(stamped);
}

void CoverCache::updateMetaData(const QNetworkCacheMetaData &metaData)
{
    m_refreshing = true;
    QNetworkDiskCache::updateMetaData(metaData);
    m_refreshing = false;
}

ImageCacheFactory::ImageCacheFactory(QString cacheDir)
    : m_cacheDir(std::move(cacheDir))
{
}

QNetworkAccessManager *ImageCacheFactory::create(QObject *parent)
{
    // 每个要联网的线程调一次。QNetworkDiskCache 不能多个实例共用一个目录；
    // 实测（Qt 6.12）只有 Qt Quick 的图片读取线程会来要，网络图片都经它取，所以只有一个实例
    auto *nam = new CachingManager(parent);
    auto *cache = new CoverCache(nam);
    cache->setCacheDirectory(m_cacheDir);
    cache->setMaximumCacheSize(MaximumSize);
    nam->setCache(cache);
    return nam;
}

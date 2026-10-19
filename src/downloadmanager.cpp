#include "downloadmanager.h"

#include "downloadpaths.h"
#include "ncmclient.h"
#include "sdk/download.h"
#include "sdk/track.h"
#include "sdk/track_service.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <utility>

namespace {
constexpr int DownloadTimeoutMs = 30 * 1000;

qint64 variantId(const QVariant &value)
{
    bool ok = false;
    const qint64 id = value.toLongLong(&ok);
    return ok ? id : static_cast<qint64>(qRound64(value.toDouble()));
}

QVariantMap normalizeTrack(const QVariantMap &track)
{
    QVariantMap meta;
    meta.insert(QStringLiteral("id"), static_cast<double>(variantId(track.value(QStringLiteral("id")))));
    meta.insert(QStringLiteral("name"), track.value(QStringLiteral("name")).toString());
    meta.insert(QStringLiteral("artist"), track.value(QStringLiteral("artist")).toString());
    meta.insert(QStringLiteral("album"), track.value(QStringLiteral("album")).toString());
    meta.insert(QStringLiteral("cover"), track.value(QStringLiteral("cover")).toString());
    meta.insert(QStringLiteral("dur"), track.value(QStringLiteral("dur")).toDouble());
    return meta;
}
} // namespace

DownloadManager::DownloadManager(NcmClient *client, QObject *parent)
    : QObject(parent), m_client(client), m_network(new QNetworkAccessManager(this))
{
    QDir().mkpath(directory());
    load();
}

DownloadManager::~DownloadManager()
{
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        if (it->reply)
            it->reply->abort();
        if (it->file) {
            it->file->close();
            it->file->remove();
            delete it->file;
        }
    }
    m_jobs.clear();
}

QVariantList DownloadManager::active() const
{
    QVariantList items;
    items.reserve(m_jobs.size());
    for (auto it = m_jobs.cbegin(); it != m_jobs.cend(); ++it) {
        const Job &job = it.value();
        items.append(QVariantMap{
            {QStringLiteral("id"), static_cast<double>(it.key())},
            {QStringLiteral("name"), job.meta.value(QStringLiteral("name")).toString()},
            {QStringLiteral("artist"), job.meta.value(QStringLiteral("artist")).toString()},
            {QStringLiteral("progress"), job.total > 0
                 ? static_cast<double>(job.received) / static_cast<double>(job.total)
                 : 0.0},
            {QStringLiteral("level"), job.meta.value(QStringLiteral("level")).toString()},
        });
    }
    return items;
}

QString DownloadManager::directory() const
{
    return yuntingDownloadsDir();
}

int DownloadManager::indexOf(double songId) const
{
    for (qsizetype i = 0; i < m_downloads.size(); ++i) {
        if (variantId(m_downloads.at(i).toMap().value(QStringLiteral("id"))) == qint64(songId))
            return static_cast<int>(i);
    }
    return -1;
}

bool DownloadManager::isDownloaded(double songId) const
{
    const QString path = localPath(songId);
    return !path.isEmpty() && QFileInfo::exists(path);
}

QString DownloadManager::localPath(double songId) const
{
    return yuntingDownloadedFile(static_cast<qint64>(songId));
}

QString DownloadManager::safeExtension(const QString &type) const
{
    QString ext = type.toLower();
    if (ext.startsWith(u'.'))
        ext.remove(0, 1);
    for (QChar c : std::as_const(ext)) {
        if (!c.isLetterOrNumber())
            return QStringLiteral("mp3");
    }
    return ext.isEmpty() ? QStringLiteral("mp3") : ext;
}

void DownloadManager::load()
{
    QFile file(directory() + QStringLiteral("/downloads.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray())
        return;
    QVariantList items;
    for (const QJsonValue &value : doc.array()) {
        QVariantMap item = value.toObject().toVariantMap();
        const qint64 id = variantId(item.value(QStringLiteral("id")));
        if (id > 0) {
            // 旧记录没有 dur：补 0，前端读 map 时不必区分缺失与 0
            item.insert(QStringLiteral("dur"),
                        item.value(QStringLiteral("dur")).toDouble());
            items.append(item);
        }
    }
    m_downloads = items;
}

void DownloadManager::save()
{
    QSaveFile file(directory() + QStringLiteral("/downloads.json"));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonArray::fromVariantList(m_downloads)).toJson(QJsonDocument::Compact));
    file.commit();
}

void DownloadManager::setDownloads(QVariantList downloads)
{
    if (downloads == m_downloads)
        return;
    m_downloads = std::move(downloads);
    save();
    emit downloadsChanged();
}

void DownloadManager::download(double songId, const QString &level)
{
    QVariantMap track;
    track.insert(QStringLiteral("id"), songId);
    start(track, level);
}

void DownloadManager::downloadTrack(const QVariantMap &track, const QString &level)
{
    start(track, level);
}

void DownloadManager::start(const QVariantMap &track, const QString &level)
{
    const qint64 songId = variantId(track.value(QStringLiteral("id")));
    if (songId <= 0)
        return;
    if (isDownloaded(songId)) {
        emit finished(songId, localPath(songId));
        return;
    }
    if (m_jobs.contains(songId))
        return;
    if (track.value(QStringLiteral("dur")).toDouble() > 0) {
        begin(track, level);
        return;
    }

    // 只给了 id（下载菜单直接按 id 取地址）时先取歌曲详情，补时长与展示信息；
    // 取不到也照常下载，记录里的 dur 落 0。
    ncm::sdk::fetchSongDetails(*m_client, {songId}, false, this,
        [this, track, level](ncm::sdk::Result<QList<ncm::sdk::Track>> result) {
        QVariantMap resolved = track;
        if (result && !result.value->isEmpty()) {
            const QVariantMap details =
                ncm::sdk::trackToVariantMap(result.value->first(), {}, 0);
            resolved.insert(QStringLiteral("dur"), details.value(QStringLiteral("dur")));
            for (const char *key : {"name", "artist", "album", "cover"}) {
                const QString field = QString::fromLatin1(key);
                if (resolved.value(field).toString().isEmpty())
                    resolved.insert(field, details.value(field));
            }
        }
        begin(resolved, level);
    });
}

void DownloadManager::begin(const QVariantMap &rawTrack, const QString &level)
{
    // 参数、已下载与在途检查只在 start() 做一次；begin 只负责取地址并落盘。
    const QVariantMap track = normalizeTrack(rawTrack);
    const qint64 songId = variantId(track.value(QStringLiteral("id")));

    const QString requestedLevel = level.isEmpty() ? QStringLiteral("lossless") : level;
    ncm::sdk::fetchSongDownload(*m_client, songId, requestedLevel, this,
        [this, track, songId, requestedLevel](ncm::sdk::Result<ncm::sdk::SongDownload> result) {
        if (!result) {
            emit failed(songId, result.error.kind == ncm::sdk::ErrorKind::Network
                ? QStringLiteral("网络不可用，无法下载")
                : QStringLiteral("取不到下载地址"));
            return;
        }
        const ncm::sdk::SongDownload &info = *result.value;
        if (info.url.isEmpty()) {
            emit failed(songId, QStringLiteral("这首歌没有可用的下载地址"));
            return;
        }

        const QString ext = safeExtension(info.type);
        const QString path = directory() + QLatin1Char('/') + QString::number(songId)
            + QLatin1Char('.') + ext;
        const QString part = path + QStringLiteral(".part");
        auto *file = new QFile(part, this);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            delete file;
            emit failed(songId, QStringLiteral("无法写入下载目录"));
            return;
        }

        QNetworkRequest request(QUrl(info.url));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setTransferTimeout(DownloadTimeoutMs);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Mozilla/5.0 (X11; Linux x86_64)"));
        request.setRawHeader("Referer", "https://music.163.com/");
        QNetworkReply *reply = m_network->get(request);

        Job job;
        job.reply = reply;
        job.file = file;
        job.finalPath = path;
        job.meta = track;
        job.meta.insert(QStringLiteral("size"), static_cast<double>(info.size));
        job.meta.insert(QStringLiteral("level"), info.level.isEmpty() ? requestedLevel : info.level);
        job.meta.insert(QStringLiteral("type"), ext);
        job.meta.insert(QStringLiteral("md5"), info.md5);
        job.meta.insert(QStringLiteral("path"), path);
        m_jobs.insert(songId, job);
        emit activeChanged();

        QObject::connect(reply, &QNetworkReply::readyRead, this, [reply, file] {
            file->write(reply->readAll());
        });
        QObject::connect(reply, &QNetworkReply::downloadProgress, this,
                         [this, songId](qint64 received, qint64 total) {
            auto it = m_jobs.find(songId);
            if (it != m_jobs.end()) {
                it->received = received;
                it->total = total;
                emit activeChanged();
            }
            emit progress(songId, received, total);
        });
        QObject::connect(reply, &QNetworkReply::finished, this,
                         [this, reply, songId] {
            auto it = m_jobs.find(songId);
            if (it == m_jobs.end()) {
                reply->deleteLater();
                return;
            }
            Job job = it.value();
            m_jobs.erase(it);
            emit activeChanged();
            job.file->write(reply->readAll());
            job.file->close();
            delete job.file;
            job.file = nullptr;

            const bool ok = reply->error() == QNetworkReply::NoError
                && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() < 400
                && QFileInfo(job.finalPath + QStringLiteral(".part")).size() > 0;
            reply->deleteLater();
            if (!ok) {
                QFile::remove(job.finalPath + QStringLiteral(".part"));
                emit failed(songId, QStringLiteral("下载中断，请重试"));
                return;
            }
            QFile::remove(job.finalPath);
            if (!QFile::rename(job.finalPath + QStringLiteral(".part"), job.finalPath)) {
                emit failed(songId, QStringLiteral("无法保存下载文件"));
                return;
            }

            job.meta.insert(QStringLiteral("path"), job.finalPath);
            job.meta.insert(QStringLiteral("completedAt"),
                            static_cast<double>(QDateTime::currentSecsSinceEpoch()));
            QVariantList items = m_downloads;
            const int index = indexOf(songId);
            if (index >= 0)
                items[index] = job.meta;
            else
                items.prepend(job.meta);
            setDownloads(items);
            emit finished(songId, job.finalPath);
        });
    });
}

void DownloadManager::cancel(double songId)
{
    const qint64 id = static_cast<qint64>(songId);
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    Job job = it.value();
    m_jobs.erase(it);
    emit activeChanged();
    if (job.reply)
        job.reply->abort();
    if (job.file) {
        job.file->close();
        job.file->remove();
        delete job.file;
    }
    emit failed(id, QStringLiteral("下载已取消"));
}

void DownloadManager::remove(double songId)
{
    cancel(songId);
    const qint64 id = static_cast<qint64>(songId);
    const QString path = localPath(id);
    if (!path.isEmpty())
        QFile::remove(path);
    QVariantList items = m_downloads;
    const int index = indexOf(id);
    if (index >= 0) {
        items.removeAt(index);
        setDownloads(items);
    }
}

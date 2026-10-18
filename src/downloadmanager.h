#pragma once
// DownloadManager: signed-URL download -> persistent file + metadata.
// UI-agnostic; QML reads `downloads` and calls downloadTrack()/remove().
// Files live in yuntingDownloadsDir() and AudioEngine prefers them over network.
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class NcmClient;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

class DownloadManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList downloads READ downloads NOTIFY downloadsChanged)
    Q_PROPERTY(QString directory READ directory CONSTANT)
public:
    explicit DownloadManager(NcmClient *client, QObject *parent = nullptr);
    ~DownloadManager() override;

    QVariantList downloads() const { return m_downloads; }
    QString directory() const;

    // Start from an already-known track map (name/artist/album/cover are preserved).
    Q_INVOKABLE void downloadTrack(const QVariantMap &track, const QString &level = QStringLiteral("lossless"));
    Q_INVOKABLE void download(double songId, const QString &level = QStringLiteral("lossless"));
    Q_INVOKABLE void cancel(double songId);
    Q_INVOKABLE void remove(double songId);
    Q_INVOKABLE bool isDownloaded(double songId) const;
    Q_INVOKABLE QString localPath(double songId) const;

signals:
    void downloadsChanged();
    void progress(double songId, qint64 received, qint64 total);
    void finished(double songId, const QString &path);
    void failed(double songId, const QString &message);

private:
    struct Job {
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
        QVariantMap meta;
        QString finalPath;
    };

    void start(const QVariantMap &track, const QString &level);
    void load();
    void save();
    void setDownloads(QVariantList downloads);
    int indexOf(double songId) const;
    QString safeExtension(const QString &type) const;

    NcmClient *m_client;
    QNetworkAccessManager *m_network;
    QVariantList m_downloads;
    QHash<qint64, Job> m_jobs;
};

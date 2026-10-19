#pragma once
// CloudUploader — the Web-only private-cloud upload pipeline.
//
// Steps (all with the stored Web cookie):
//   1. POST /api/cloud/upload/check       md5/length/bitrate -> needUpload, songId
//   2. POST /weapi/nos/token/alloc        audio token/objectKey/resourceId
//   3. GET  https://wanproxy.127.net/lbs  upload host for the bucket
//   4. POST <lbs>/<bucket>/<objectKey>    raw bytes, x-nos-token + Content-MD5
//   5. POST /api/upload/cloud/info/v2     metadata -> songId
//   6. POST /api/cloud/pub/v2             publish
//
// No eapi, no retries. A failed step aborts the whole upload; partial files are
// not re-used. Maximum accepted size matches the Web limit (100 MiB).
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class NcmClient;
class QNetworkReply;

class CloudUploader : public QObject {
    Q_OBJECT
public:
    explicit CloudUploader(NcmClient *client, QObject *parent = nullptr);

    // Test seam: point LBS and the raw upload host at a local server. Production
    // leaves both empty and uses the LBS response.
    void setTestEndpoints(const QUrl &lbs, const QUrl &nosFallback = {});

    bool busy() const { return m_busy; }
    void upload(const QString &filePath);
    void cancel();

    // Pure helpers, exposed for tests.
    static QString songNameFromPath(const QString &filePath);
    static QString objectNameFromPath(const QString &filePath);
    static QString contentTypeForPath(const QString &filePath);
    static QString fileSuffix(const QString &filePath);

signals:
    void progress(qint64 sent, qint64 total);
    void finished(double songId);
    void failed(const QString &message);

private:
    void fail(const QString &message);
    void checkUpload();
    void allocateToken();
    void fetchLbs();
    void startUpload(const QString &host);
    void saveInfo();
    void publish();
    void reset();

    NcmClient *m_client;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QUrl m_lbsUrl;
    QUrl m_nosFallback;

    bool m_busy = false;
    quint64 m_generation = 0;
    QString m_filePath;
    QString m_filename;
    QString m_ext;
    QString m_md5;
    QString m_objectKey;
    QString m_token;
    QString m_resourceId;
    qint64 m_size = 0;
    qint64 m_songId = 0;
    QByteArray m_data;
};

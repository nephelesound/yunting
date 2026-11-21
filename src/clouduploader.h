#pragma once
// CloudUploader — the Web-only private-cloud upload pipeline.
//
// Steps (all with the stored Web cookie):
//   1. POST /api/cloud/upload/check       md5/length/bitrate -> needUpload, songId
//   2. POST /weapi/nos/token/alloc        empty bucket -> resourceId for step 5
//      POST /weapi/nos/token/alloc        audio bucket -> token/objectKey for step 4
//   3. GET  https://wanproxy.127.net/lbs  upload host for the bucket
//   4. POST <lbs>/<bucket>/<objectKey>    raw bytes, x-nos-token + Content-MD5
//   5. POST /api/upload/cloud/info/v2     metadata -> songId
//   6. POST /weapi/cloud/pub/v2           publish (200 / 201)
// needUpload=false means the service already has the file: instead of steps 2-6,
// POST /api/cloud/upload/check/v2 for the import songId, then
// POST /api/cloud/user/song/import.
//
// No eapi, no retries. A failed step aborts the whole upload; partial files are
// not re-used. Maximum accepted size matches the Web limit (100 MiB).
#include "ncmclient.h"

#include <QByteArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <functional>

class QNetworkReply;

class CloudUploader : public QObject {
    Q_OBJECT
public:
    explicit CloudUploader(NcmClient *client, QObject *parent = nullptr);

    // Test seam: ask a local server for the upload host instead of the real LBS.
    void setTestLbs(const QUrl &lbs);

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
    // Callback for one API step of the current upload: drops replies to a
    // cancelled upload, fails with `failure` as the prefix, else runs `next`.
    NcmClient::ResponseCallback step(const QString &failure,
                                     std::function<void(const QJsonObject &body)> next);
    QJsonObject tokenRequest(const QString &bucket) const;
    void fail(const QString &message);
    void checkUpload();
    void checkImport();
    void importSong(const QString &songId);
    void allocateResource();
    void allocateTransfer();
    void fetchLbs();
    void startUpload(const QString &host);
    void saveInfo();
    void publish(qint64 songId);
    void reset();

    NcmClient *m_client;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QUrl m_lbsUrl;

    bool m_busy = false;
    quint64 m_generation = 0;
    QString m_filePath;
    QString m_filename;
    QString m_ext;
    QString m_md5;
    QString m_checkSongId;
    QString m_resourceId;
    QString m_objectKey;
    QString m_token;
    qint64 m_size = 0;
    QByteArray m_data;
};

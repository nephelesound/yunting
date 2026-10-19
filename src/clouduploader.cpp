#include "clouduploader.h"

#include "ncmclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {

constexpr qint64 MaxCloudUpload = 100 * 1024 * 1024;
constexpr auto DefaultLbs = "https://wanproxy.127.net/lbs";
constexpr auto CloudBucket = "jd-musicrep-privatecloud-audio-public";
constexpr auto Bitrate = "999000";

bool responseOk(const NcmClient::Response &response, QJsonObject *body, QString *message)
{
    if (!response.ok) {
        if (response.networkFailure)
            *message = QStringLiteral("网络不可用");
        else if (response.httpStatus > 0)
            *message = QStringLiteral("服务器返回 HTTP %1").arg(response.httpStatus);
        else
            *message = QStringLiteral("上传请求失败");
        return false;
    }
    const QJsonObject object = response.doc.object();
    const int code = object.value(QStringLiteral("code")).toInt();
    if (code != 200) {
        *message = object.value(QStringLiteral("message")).toString();
        if (message->isEmpty())
            *message = object.value(QStringLiteral("msg")).toString();
        if (message->isEmpty())
            *message = QStringLiteral("服务端拒绝上传（%1）").arg(code);
        return false;
    }
    *body = object;
    return true;
}

QString objectKeyPath(const QString &key)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(key));
}

} // namespace

CloudUploader::CloudUploader(NcmClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_lbsUrl(QUrl(QString::fromLatin1(DefaultLbs)))
{
}

void CloudUploader::setTestEndpoints(const QUrl &lbs, const QUrl &nosFallback)
{
    m_lbsUrl = lbs;
    m_nosFallback = nosFallback;
}

QString CloudUploader::fileSuffix(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("mp3") : suffix;
}

QString CloudUploader::songNameFromPath(const QString &filePath)
{
    const QString name = QFileInfo(filePath).completeBaseName().trimmed();
    return name.isEmpty() ? QStringLiteral("未命名歌曲") : name;
}

QString CloudUploader::objectNameFromPath(const QString &filePath)
{
    QString name = QFileInfo(filePath).completeBaseName();
    name.remove(QLatin1Char(' '));
    name.replace(QLatin1Char('.'), QLatin1Char('_'));
    if (name.isEmpty())
        name = QStringLiteral("upload");
    return name;
}

QString CloudUploader::contentTypeForPath(const QString &filePath)
{
    const QString suffix = fileSuffix(filePath);
    if (suffix == QLatin1String("flac"))
        return QStringLiteral("audio/flac");
    if (suffix == QLatin1String("wav"))
        return QStringLiteral("audio/wav");
    if (suffix == QLatin1String("m4a") || suffix == QLatin1String("aac"))
        return QStringLiteral("audio/mp4");
    if (suffix == QLatin1String("ogg"))
        return QStringLiteral("audio/ogg");
    return QStringLiteral("audio/mpeg");
}

void CloudUploader::upload(const QString &filePath)
{
    if (m_busy) {
        emit failed(QStringLiteral("已有上传任务在进行"));
        return;
    }

    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        emit failed(QStringLiteral("找不到可读的音频文件"));
        return;
    }
    if (info.size() <= 0) {
        emit failed(QStringLiteral("音频文件为空"));
        return;
    }
    if (info.size() > MaxCloudUpload) {
        emit failed(QStringLiteral("云盘单文件上限为 100 MB"));
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit failed(QStringLiteral("无法读取音频文件"));
        return;
    }
    m_data = file.readAll();
    if (m_data.size() != info.size()) {
        emit failed(QStringLiteral("读取音频文件不完整"));
        return;
    }

    m_filePath = info.absoluteFilePath();
    m_filename = info.fileName();
    m_ext = fileSuffix(m_filePath);
    m_size = info.size();
    m_songId = 0;
    m_md5 = QString::fromLatin1(QCryptographicHash::hash(m_data, QCryptographicHash::Md5).toHex());
    m_busy = true;
    ++m_generation;
    checkUpload();
}

void CloudUploader::cancel()
{
    if (!m_busy)
        return;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    reset();
    emit failed(QStringLiteral("上传已取消"));
}

void CloudUploader::checkUpload()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("bitrate"), QString::fromLatin1(Bitrate));
    form.addQueryItem(QStringLiteral("ext"), QString());
    form.addQueryItem(QStringLiteral("length"), QString::number(m_size));
    form.addQueryItem(QStringLiteral("md5"), m_md5);
    form.addQueryItem(QStringLiteral("songId"), QStringLiteral("0"));
    form.addQueryItem(QStringLiteral("version"), QStringLiteral("1"));

    const quint64 generation = m_generation;
    m_client->postForm(
        QStringLiteral("api/cloud/upload/check"), form, true,
        [this, generation](const NcmClient::Response &response) {
            if (generation != m_generation)
                return;
            QJsonObject body;
            QString message;
            if (!responseOk(response, &body, &message)) {
                fail(message);
                return;
            }
            m_songId = qint64(body.value(QStringLiteral("songId")).toDouble());
            allocateToken();
        });
}

void CloudUploader::allocateToken()
{
    const QJsonObject payload{
        {QStringLiteral("bucket"), QString::fromLatin1(CloudBucket)},
        {QStringLiteral("ext"), m_ext},
        {QStringLiteral("filename"), objectNameFromPath(m_filePath)},
        {QStringLiteral("local"), false},
        {QStringLiteral("nos_product"), 3},
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("md5"), m_md5},
    };

    const quint64 generation = m_generation;
    m_client->postWeapi(
        QStringLiteral("nos/token/alloc"), payload, true,
        [this, generation](const NcmClient::Response &response) {
            if (generation != m_generation)
                return;
            QJsonObject body;
            QString message;
            if (!responseOk(response, &body, &message)) {
                fail(message);
                return;
            }
            const QJsonObject result = body.value(QStringLiteral("result")).toObject();
            m_objectKey = result.value(QStringLiteral("objectKey")).toString();
            m_token = result.value(QStringLiteral("token")).toString();
            m_resourceId = result.value(QStringLiteral("resourceId")).toString();
            if (m_objectKey.isEmpty() || m_token.isEmpty() || m_resourceId.isEmpty()) {
                fail(QStringLiteral("上传凭据不完整"));
                return;
            }
            fetchLbs();
        });
}

void CloudUploader::fetchLbs()
{
    if (m_nosFallback.isValid()) {
        // 测试接缝：不访问 LBS。
        startUpload(m_nosFallback.toString());
        return;
    }

    QUrl lbs = m_lbsUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("1.0"));
    query.addQueryItem(QStringLiteral("bucketname"), QString::fromLatin1(CloudBucket));
    lbs.setQuery(query);

    const quint64 generation = m_generation;
    m_reply = m_nam.get(QNetworkRequest(lbs));
    connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
        if (!m_reply || generation != m_generation)
            return;
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("获取上传服务器失败：%1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        const QJsonArray upload = body.value(QStringLiteral("upload")).toArray();
        const QString host = upload.isEmpty() ? QString() : upload.first().toString();
        if (host.isEmpty()) {
            fail(QStringLiteral("上传服务器地址无效"));
            return;
        }
        startUpload(host);
    });
}

void CloudUploader::startUpload(const QString &host)
{
    QString base = host;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QString bucket = QString::fromLatin1(CloudBucket);
    QUrl target(base + QLatin1Char('/') + bucket + QLatin1Char('/')
                + objectKeyPath(m_objectKey));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("offset"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("complete"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("version"), QStringLiteral("1.0"));
    target.setQuery(query);

    QNetworkRequest request(target);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      contentTypeForPath(m_filePath));
    request.setRawHeader("x-nos-token", m_token.toUtf8());
    request.setRawHeader("Content-MD5", m_md5.toUtf8());
    request.setHeader(QNetworkRequest::ContentLengthHeader, m_data.size());
    const quint64 generation = m_generation;
    m_reply = m_nam.post(request, m_data);
    connect(m_reply, &QNetworkReply::uploadProgress, this, &CloudUploader::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
        if (!m_reply || generation != m_generation)
            return;
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("文件上传失败：%1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        reply->deleteLater();
        saveInfo();
    });
}

void CloudUploader::saveInfo()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("md5"), m_md5);
    form.addQueryItem(QStringLiteral("songid"), QString::number(m_songId));
    form.addQueryItem(QStringLiteral("filename"), m_filename);
    form.addQueryItem(QStringLiteral("song"), songNameFromPath(m_filePath));
    form.addQueryItem(QStringLiteral("album"), QStringLiteral("未知专辑"));
    form.addQueryItem(QStringLiteral("artist"), QStringLiteral("未知艺术家"));
    form.addQueryItem(QStringLiteral("bitrate"), QString::fromLatin1(Bitrate));
    form.addQueryItem(QStringLiteral("resourceId"), m_resourceId);

    const quint64 generation = m_generation;
    m_client->postForm(
        QStringLiteral("api/upload/cloud/info/v2"), form, true,
        [this, generation](const NcmClient::Response &response) {
            if (generation != m_generation)
                return;
            QJsonObject body;
            QString message;
            if (!responseOk(response, &body, &message)) {
                fail(message);
                return;
            }
            const qint64 songId = qint64(body.value(QStringLiteral("songId")).toDouble());
            if (songId > 0)
                m_songId = songId;
            if (m_songId <= 0) {
                fail(QStringLiteral("云盘没有返回歌曲 id"));
                return;
            }
            publish();
        });
}

void CloudUploader::publish()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("songid"), QString::number(m_songId));

    const quint64 generation = m_generation;
    m_client->postForm(
        QStringLiteral("api/cloud/pub/v2"), form, true,
        [this, generation](const NcmClient::Response &response) {
            if (generation != m_generation)
                return;
            QJsonObject body;
            QString message;
            if (!responseOk(response, &body, &message)) {
                fail(message);
                return;
            }
            const qint64 songId = m_songId;
            reset();
            emit finished(songId);
        });
}

void CloudUploader::fail(const QString &message)
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    reset();
    emit failed(message);
}

void CloudUploader::reset()
{
    ++m_generation;
    m_busy = false;
    m_filePath.clear();
    m_filename.clear();
    m_ext.clear();
    m_md5.clear();
    m_objectKey.clear();
    m_token.clear();
    m_resourceId.clear();
    m_size = 0;
    m_songId = 0;
    m_data.clear();
    m_reply = nullptr;
}

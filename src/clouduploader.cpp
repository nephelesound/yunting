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
constexpr int Bitrate = 999000;
const QString UnknownAlbum = QStringLiteral("未知专辑");
const QString UnknownArtist = QStringLiteral("未知艺术家");

// Why a response is not a success; empty when it is.
QString failureReason(const NcmClient::Response &response)
{
    if (!response.ok) {
        if (response.networkFailure)
            return QStringLiteral("网络不可用");
        if (response.httpStatus > 0)
            return QStringLiteral("服务器返回 HTTP %1").arg(response.httpStatus);
        return QStringLiteral("上传请求失败");
    }
    const QJsonObject object = response.doc.object();
    const int code = object.value(QStringLiteral("code")).toInt();
    // cloud/pub/v2 answers 201 when it publishes the song.
    if (code == 200 || code == 201)
        return {};
    QString message = object.value(QStringLiteral("message")).toString();
    if (message.isEmpty())
        message = object.value(QStringLiteral("msg")).toString();
    if (message.isEmpty())
        message = QStringLiteral("服务端拒绝上传（%1）").arg(code);
    return message;
}

// Song and resource IDs arrive as strings or as integers beyond 2^53, so they
// never pass through double.
QString idText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    return value.isDouble() ? QString::number(value.toInteger()) : QString();
}

QUrlQuery songsForm(const QJsonObject &song)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("0"));
    form.addQueryItem(QStringLiteral("songs"),
                      QString::fromUtf8(QJsonDocument(QJsonArray{song}).toJson(QJsonDocument::Compact)));
    return form;
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

void CloudUploader::setTestLbs(const QUrl &lbs)
{
    m_lbsUrl = lbs;
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

NcmClient::ResponseCallback CloudUploader::step(const QString &failure,
                                                std::function<void(const QJsonObject &)> next)
{
    return [this, generation = m_generation, failure, next = std::move(next)](
               const NcmClient::Response &response) {
        if (generation != m_generation)
            return;
        const QString reason = failureReason(response);
        if (!reason.isEmpty()) {
            fail(QStringLiteral("%1：%2").arg(failure, reason));
            return;
        }
        next(response.doc.object());
    };
}

void CloudUploader::checkUpload()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("bitrate"), QString::number(Bitrate));
    form.addQueryItem(QStringLiteral("ext"), QString());
    form.addQueryItem(QStringLiteral("length"), QString::number(m_size));
    form.addQueryItem(QStringLiteral("md5"), m_md5);
    form.addQueryItem(QStringLiteral("songId"), QStringLiteral("0"));
    form.addQueryItem(QStringLiteral("version"), QStringLiteral("1"));

    m_client->postForm(
        QStringLiteral("api/cloud/upload/check"), form, true,
        step(QStringLiteral("检查文件失败"), [this](const QJsonObject &body) {
            const QJsonValue needUpload = body.value(QStringLiteral("needUpload"));
            if (!needUpload.isBool()) {
                fail(QStringLiteral("文件检查未返回上传标识"));
                return;
            }
            if (!needUpload.toBool()) {
                checkImport();
                return;
            }
            m_checkSongId = idText(body.value(QStringLiteral("songId")));
            if (m_checkSongId.isEmpty()) {
                fail(QStringLiteral("文件检查未返回歌曲登记标识"));
                return;
            }
            allocateResource();
        }));
}

void CloudUploader::checkImport()
{
    const QJsonObject song{{QStringLiteral("md5"), m_md5},
                           {QStringLiteral("songId"), -2},
                           {QStringLiteral("bitrate"), Bitrate},
                           {QStringLiteral("fileSize"), m_size}};
    m_client->postForm(
        QStringLiteral("api/cloud/upload/check/v2"), songsForm(song), true,
        step(QStringLiteral("检查导入失败"), [this](const QJsonObject &body) {
            const QJsonArray items = body.value(QStringLiteral("data")).toArray();
            const QJsonObject item = items.size() == 1 ? items.first().toObject() : QJsonObject();
            // upload: 0 importable, 1 already in the cloud, 2 refused.
            const int status = item.value(QStringLiteral("upload")).toInt(-1);
            if (status == 2) {
                fail(QStringLiteral("服务器不允许将这首歌曲导入云盘"));
                return;
            }
            const QString songId = idText(item.value(QStringLiteral("songId")));
            if ((status != 0 && status != 1) || songId.isEmpty()) {
                fail(QStringLiteral("云盘导入检查结果不完整"));
                return;
            }
            importSong(songId);
        }));
}

void CloudUploader::importSong(const QString &songId)
{
    const QJsonObject song{{QStringLiteral("songId"), songId},
                           {QStringLiteral("bitrate"), Bitrate},
                           {QStringLiteral("song"), songNameFromPath(m_filePath)},
                           {QStringLiteral("artist"), UnknownArtist},
                           {QStringLiteral("album"), UnknownAlbum},
                           {QStringLiteral("fileName"), m_filename}};
    m_client->postForm(
        QStringLiteral("api/cloud/user/song/import"), songsForm(song), true,
        step(QStringLiteral("导入歌曲失败"), [this](const QJsonObject &body) {
            const QJsonArray imported = body.value(QStringLiteral("data")).toObject()
                                            .value(QStringLiteral("successSongs")).toArray();
            const QJsonObject first = imported.isEmpty() ? QJsonObject() : imported.first().toObject();
            const qint64 songId = idText(first.value(QStringLiteral("song")).toObject()
                                             .value(QStringLiteral("songId"))).toLongLong();
            if (songId <= 0) {
                fail(QStringLiteral("服务器未确认歌曲导入成功"));
                return;
            }
            reset();
            emit finished(songId);
        }));
}

QJsonObject CloudUploader::tokenRequest(const QString &bucket) const
{
    return {
        {QStringLiteral("bucket"), bucket},
        {QStringLiteral("ext"), m_ext},
        {QStringLiteral("filename"), objectNameFromPath(m_filePath)},
        {QStringLiteral("local"), false},
        {QStringLiteral("nos_product"), 3},
        {QStringLiteral("type"), QStringLiteral("audio")},
        {QStringLiteral("md5"), m_md5},
    };
}

void CloudUploader::allocateResource()
{
    // Only the empty-bucket allocation yields the resource ID that info/v2
    // registers; the audio-bucket one only authorizes the byte transfer.
    m_client->postWeapi(
        QStringLiteral("nos/token/alloc"), tokenRequest(QString()), true,
        step(QStringLiteral("申请登记凭据失败"), [this](const QJsonObject &body) {
            m_resourceId = idText(body.value(QStringLiteral("result")).toObject()
                                      .value(QStringLiteral("resourceId")));
            if (m_resourceId.isEmpty()) {
                fail(QStringLiteral("歌曲登记凭据不完整"));
                return;
            }
            allocateTransfer();
        }));
}

void CloudUploader::allocateTransfer()
{
    m_client->postWeapi(
        QStringLiteral("nos/token/alloc"), tokenRequest(QString::fromLatin1(CloudBucket)), true,
        step(QStringLiteral("申请上传凭据失败"), [this](const QJsonObject &body) {
            const QJsonObject result = body.value(QStringLiteral("result")).toObject();
            m_objectKey = result.value(QStringLiteral("objectKey")).toString();
            m_token = result.value(QStringLiteral("token")).toString();
            if (m_objectKey.isEmpty() || m_token.isEmpty()) {
                fail(QStringLiteral("上传凭据不完整"));
                return;
            }
            fetchLbs();
        }));
}

void CloudUploader::fetchLbs()
{
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
    form.addQueryItem(QStringLiteral("songid"), m_checkSongId);
    form.addQueryItem(QStringLiteral("filename"), m_filename);
    form.addQueryItem(QStringLiteral("song"), songNameFromPath(m_filePath));
    form.addQueryItem(QStringLiteral("album"), UnknownAlbum);
    form.addQueryItem(QStringLiteral("artist"), UnknownArtist);
    form.addQueryItem(QStringLiteral("bitrate"), QString::number(Bitrate));
    form.addQueryItem(QStringLiteral("resourceId"), m_resourceId);

    m_client->postForm(
        QStringLiteral("api/upload/cloud/info/v2"), form, true,
        step(QStringLiteral("登记歌曲失败"), [this](const QJsonObject &body) {
            const qint64 songId = idText(body.value(QStringLiteral("songId"))).toLongLong();
            if (songId <= 0) {
                fail(QStringLiteral("云盘没有返回歌曲 id"));
                return;
            }
            publish(songId);
        }));
}

void CloudUploader::publish(qint64 songId)
{
    m_client->postWeapi(
        QStringLiteral("cloud/pub/v2"),
        QJsonObject{{QStringLiteral("songid"), QString::number(songId)}}, true,
        step(QStringLiteral("发布歌曲失败"), [this, songId](const QJsonObject &) {
            reset();
            emit finished(songId);
        }));
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
    m_checkSongId.clear();
    m_resourceId.clear();
    m_objectKey.clear();
    m_token.clear();
    m_size = 0;
    m_data.clear();
    m_reply = nullptr;
}

#include "sdk/download.h"

#include "sdk/sdk_json.h"
#include "clientidentity.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto DownloadPath = "api/song/enhance/download/url/v1";

} // namespace

Result<SongDownload> parseSongDownload(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("songDownload");
    QJsonValue first;
    QString itemPath = rootPath + QStringLiteral(".data");
    const QJsonValue data = root.value(QStringLiteral("data"));
    if (data.isObject()) {
        first = data;
    } else if (data.isArray()) {
        const QJsonArray rows = data.toArray();
        first = rows.isEmpty() ? QJsonValue() : rows.at(0);
        itemPath = rootPath + QStringLiteral(".data[0]");
    } else if (data.isUndefined() || data.isNull()) {
        return missingFailure<SongDownload>(QString::fromLatin1(DownloadPath),
                                            rootPath + QStringLiteral(".data"),
                                            QStringLiteral("object or array"));
    } else {
        return itemTypeFailure<SongDownload>(QString::fromLatin1(DownloadPath),
                                             rootPath + QStringLiteral(".data"));
    }
    if (!first.isObject())
        return missingFailure<SongDownload>(QString::fromLatin1(DownloadPath), itemPath,
                                            QStringLiteral("object"));

    JsonReader reader(first.toObject(), itemPath);
    SongDownload download;
    download.id = reader.integer("id");
    download.url = reader.string("url");
    download.size = reader.integer("size");
    download.br = reader.integer("br");
    download.md5 = reader.string("md5");
    download.type = reader.string("type");
    download.level = reader.string("level");
    download.expires = reader.integer("expi");
    if (!reader.ok())
        return parseFailure<SongDownload>(QString::fromLatin1(DownloadPath), reader, itemPath);
    if (download.id <= 0)
        return Result<SongDownload>::failure(endpointError(
            ErrorKind::Parse, QString::fromLatin1(DownloadPath),
            itemPath + QStringLiteral(".id: must be positive"),
            itemPath + QStringLiteral(".id")));
    return Result<SongDownload>::success(std::move(download));
}

void fetchSongDownload(NcmClient &client, qint64 songId, QString level,
                       QObject *context, SongDownloadCallback callback)
{
    if (!callback)
        return;
    if (songId <= 0) {
        callback(Result<SongDownload>::failure(endpointError(
            ErrorKind::InvalidRequest, QString::fromLatin1(DownloadPath),
            QStringLiteral("songId must be positive"))));
        return;
    }
    if (level.isEmpty())
        level = QStringLiteral("lossless");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("id"), QString::number(songId));
    form.addQueryItem(QStringLiteral("level"), level);
    form.addQueryItem(QStringLiteral("immerseType"), QStringLiteral("c51"));
    postFormJson(client, context, QString::fromLatin1(DownloadPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<SongDownload>::failure(root.error));
            return;
        }
        callback(parseSongDownload(*root.value));
    }, true, ncm::client::downloadCookieContext());
}

} // namespace ncm::sdk

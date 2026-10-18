#include "sdk/video.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

// weapi-relative paths: NcmClient::postWeapi prefixes "weapi/".
constexpr auto TimelinePath = "videotimeline/get";
constexpr auto DetailPath = "cloudvideo/v1/video/detail";
constexpr auto PlayUrlPath = "cloudvideo/playurl";

Result<VideoPlayUrl> playUrlFrom(const QJsonObject &object, const QString &path,
                                 const char *endpoint)
{
    JsonReader reader(object, path);
    VideoPlayUrl out;
    out.vid = reader.string("id");
    out.url = reader.string("url");
    out.size = reader.integer("size");
    out.validityTime = reader.integer("validityTime");
    out.needPay = reader.boolean("needPay");
    out.resolution = static_cast<int>(reader.integer("r"));
    if (!reader.ok())
        return Result<VideoPlayUrl>::failure(
            endpointError(ErrorKind::Parse, endpoint, reader.error(), path));
    return Result<VideoPlayUrl>::success(out);
}

Result<Video> videoFrom(const QJsonObject &object, const QString &path, const char *endpoint)
{
    JsonReader reader(object, path);
    Video out;
    out.vid = reader.string("vid");
    out.title = reader.string("title");
    out.description = reader.string("description");
    out.cover = reader.string("coverUrl");
    out.threadId = reader.string("threadId");
    out.duration = reader.integer("durationms") / 1000;
    out.previewDuration = reader.integer("previewDurationms") / 1000;
    out.publishTime = reader.integer("publishTime");
    out.playCount = reader.integer("playTime");
    out.praisedCount = reader.integer("praisedCount");
    out.commentCount = reader.integer("commentCount");
    out.shareCount = reader.integer("shareCount");
    out.subscribeCount = reader.integer("subscribeCount");
    out.praised = reader.boolean("praised");
    out.subscribed = reader.boolean("subscribed");
    out.hasRelatedGameAd = reader.boolean("hasRelatedGameAd");
    out.authType = static_cast<int>(reader.integer("authType"));
    out.width = static_cast<int>(reader.integer("width"));
    out.height = static_cast<int>(reader.integer("height"));
    out.previewUrl = reader.string("previewUrl");

    const QJsonObject creator = reader.object("creator");
    const QJsonObject urlInfo = reader.object("urlInfo");
    const QJsonArray groupValues = reader.array("videoGroup");
    const QJsonArray resolutionValues = reader.array("resolutions");
    if (!reader.ok())
        return Result<Video>::failure(
            endpointError(ErrorKind::Parse, endpoint, reader.error(), path));

    JsonReader creatorReader(creator, path + QStringLiteral(".creator"));
    out.creator.userId = creatorReader.integer("userId");
    out.creator.nickname = creatorReader.string("nickname");
    out.creator.avatarUrl = creatorReader.string("avatarUrl");
    out.creator.signature = creatorReader.string("signature");
    if (!creatorReader.ok())
        return Result<Video>::failure(
            endpointError(ErrorKind::Parse, endpoint, creatorReader.error(),
                          path + QStringLiteral(".creator")));

    for (qsizetype i = 0; i < groupValues.size(); ++i) {
        const QString groupPath = path + QStringLiteral(".videoGroup[%1]").arg(i);
        const QJsonValue value = groupValues.at(i);
        if (!value.isObject())
            return Result<Video>::failure(
                endpointError(ErrorKind::Parse, endpoint,
                              groupPath + QStringLiteral(": expected object"), groupPath));
        JsonReader groupReader(value.toObject(), groupPath);
        VideoGroup group;
        group.id = groupReader.integer("id");
        group.name = groupReader.string("name");
        if (!groupReader.ok())
            return Result<Video>::failure(
                endpointError(ErrorKind::Parse, endpoint, groupReader.error(), groupPath));
        out.groups.append(group);
    }

    for (qsizetype i = 0; i < resolutionValues.size(); ++i) {
        const QString resolutionPath = path + QStringLiteral(".resolutions[%1]").arg(i);
        const QJsonValue value = resolutionValues.at(i);
        if (!value.isObject())
            return Result<Video>::failure(
                endpointError(ErrorKind::Parse, endpoint,
                              resolutionPath + QStringLiteral(": expected object"), resolutionPath));
        JsonReader resolutionReader(value.toObject(), resolutionPath);
        VideoResolution resolution;
        resolution.resolution = static_cast<int>(resolutionReader.integer("resolution"));
        resolution.size = resolutionReader.integer("size");
        if (!resolutionReader.ok())
            return Result<Video>::failure(
                endpointError(ErrorKind::Parse, endpoint, resolutionReader.error(),
                              resolutionPath));
        out.resolutions.append(resolution);
    }

    if (!urlInfo.isEmpty()) {
        const auto parsed = playUrlFrom(urlInfo, path + QStringLiteral(".urlInfo"), endpoint);
        if (!parsed)
            return Result<Video>::failure(parsed.error);
        out.urlInfo = *parsed.value;
    }

    return Result<Video>::success(out);
}

} // namespace

Result<VideoTimelinePage> parseRecommendedVideos(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("video.timeline"));
    const QJsonArray items = reader.array("datas");
    if (!reader.ok())
        return Result<VideoTimelinePage>::failure(
            endpointError(ErrorKind::Parse, TimelinePath, reader.error(),
                          QStringLiteral("video.timeline.datas")));
    if (!reader.has("datas"))
        return Result<VideoTimelinePage>::failure(
            endpointError(ErrorKind::Parse, TimelinePath,
                          QStringLiteral("video.timeline.datas: missing array"),
                          QStringLiteral("video.timeline.datas")));

    VideoTimelinePage page;
    page.more = reader.boolean("hasmore");
    if (!reader.ok())
        return Result<VideoTimelinePage>::failure(
            endpointError(ErrorKind::Parse, TimelinePath, reader.error(),
                          QStringLiteral("video.timeline.hasmore")));

    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString itemPath = QStringLiteral("video.timeline.datas[%1]").arg(i);
        if (!items.at(i).isObject())
            return Result<VideoTimelinePage>::failure(
                endpointError(ErrorKind::Parse, TimelinePath,
                              itemPath + QStringLiteral(": expected object"), itemPath));
        JsonReader item(items.at(i).toObject(), itemPath);
        const QJsonObject data = item.object("data");
        if (!item.ok())
            return Result<VideoTimelinePage>::failure(
                endpointError(ErrorKind::Parse, TimelinePath, item.error(), itemPath));
        // Non-video cards (ads/live entries) may omit data; unknown future card
        // types are skipped instead of failing the whole page.
        if (data.isEmpty())
            continue;
        auto parsed = videoFrom(data, itemPath + QStringLiteral(".data"), TimelinePath);
        if (!parsed)
            return Result<VideoTimelinePage>::failure(parsed.error);
        if (parsed.value->vid.isEmpty())
            continue;
        parsed.value->type = static_cast<int>(item.integer("type"));
        if (!item.ok())
            return Result<VideoTimelinePage>::failure(
                endpointError(ErrorKind::Parse, TimelinePath, item.error(), itemPath));
        page.items.append(*parsed.value);
    }
    return Result<VideoTimelinePage>::success(page);
}

Result<Video> parseVideoDetail(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("video.detail"));
    const QJsonObject data = reader.object("data");
    if (!reader.ok())
        return Result<Video>::failure(
            endpointError(ErrorKind::Parse, DetailPath, reader.error(),
                          QStringLiteral("video.detail.data")));
    if (!reader.has("data") || data.isEmpty())
        return Result<Video>::failure(
            endpointError(ErrorKind::Parse, DetailPath,
                          QStringLiteral("video.detail.data: missing object"),
                          QStringLiteral("video.detail.data")));
    auto parsed = videoFrom(data, QStringLiteral("video.detail.data"), DetailPath);
    if (!parsed)
        return parsed;
    if (parsed.value->vid.isEmpty())
        return Result<Video>::failure(
            endpointError(ErrorKind::Parse, DetailPath,
                          QStringLiteral("video.detail.data: missing vid"),
                          QStringLiteral("video.detail.data.vid")));
    return parsed;
}

Result<QList<VideoPlayUrl>> parseVideoUrls(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("video.playUrls"));
    const QJsonArray items = reader.array("urls");
    if (!reader.ok())
        return Result<QList<VideoPlayUrl>>::failure(
            endpointError(ErrorKind::Parse, PlayUrlPath, reader.error(),
                          QStringLiteral("video.playUrls.urls")));
    if (!reader.has("urls"))
        return Result<QList<VideoPlayUrl>>::failure(
            endpointError(ErrorKind::Parse, PlayUrlPath,
                          QStringLiteral("video.playUrls.urls: missing array"),
                          QStringLiteral("video.playUrls.urls")));

    QList<VideoPlayUrl> urls;
    urls.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("video.playUrls.urls[%1]").arg(i);
        if (!items.at(i).isObject())
            return Result<QList<VideoPlayUrl>>::failure(
                endpointError(ErrorKind::Parse, PlayUrlPath,
                              path + QStringLiteral(": expected object"), path));
        const auto parsed = playUrlFrom(items.at(i).toObject(), path, PlayUrlPath);
        if (!parsed)
            return Result<QList<VideoPlayUrl>>::failure(parsed.error);
        urls.append(*parsed.value);
    }
    return Result<QList<VideoPlayUrl>>::success(urls);
}

void fetchRecommendedVideos(NcmClient &client, int offset, QObject *context,
                            VideoTimelineCallback callback)
{
    const int pageOffset = std::max(0, offset);
    const QJsonObject payload{
        {QStringLiteral("offset"), pageOffset},
        {QStringLiteral("filterLives"), QStringLiteral("[]")},
        {QStringLiteral("withProgramInfo"), QStringLiteral("true")},
        {QStringLiteral("needUrl"), QStringLiteral("1")},
        {QStringLiteral("resolution"), QStringLiteral("480")},
    };
    postWeapiJson(client, context, QString::fromLatin1(TimelinePath), payload, true,
                  [callback = std::move(callback), pageOffset](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<VideoTimelinePage>::failure(root.error));
            return;
        }
        auto page = parseRecommendedVideos(*root.value);
        if (page)
            page.value->offset = pageOffset;
        callback(std::move(page));
    });
}

void fetchVideoDetail(NcmClient &client, QString vid, QObject *context,
                      VideoDetailCallback callback)
{
    if (vid.isEmpty()) {
        if (callback)
            callback(Result<Video>::failure(
                endpointError(ErrorKind::InvalidRequest, QString::fromLatin1(DetailPath),
                              QStringLiteral("vid must not be empty"))));
        return;
    }
    const QJsonObject payload{{QStringLiteral("id"), vid}};
    postWeapiJson(client, context, QString::fromLatin1(DetailPath), payload, true,
                  [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<Video>::failure(root.error));
            return;
        }
        callback(parseVideoDetail(*root.value));
    });
}

void fetchVideoUrls(NcmClient &client, QString vid, int resolution, QObject *context,
                    VideoPlayUrlsCallback callback)
{
    if (vid.isEmpty() || resolution <= 0) {
        if (callback) {
            const QString message = vid.isEmpty()
                ? QStringLiteral("vid must not be empty")
                : QStringLiteral("resolution must be positive");
            callback(Result<QList<VideoPlayUrl>>::failure(
                endpointError(ErrorKind::InvalidRequest, QString::fromLatin1(PlayUrlPath),
                              message)));
        }
        return;
    }
    const QByteArray ids =
        QJsonDocument(QJsonArray{QJsonValue(vid)}).toJson(QJsonDocument::Compact);
    const QJsonObject payload{
        {QStringLiteral("ids"), QString::fromUtf8(ids)},
        {QStringLiteral("resolution"), resolution},
    };
    postWeapiJson(client, context, QString::fromLatin1(PlayUrlPath), payload, true,
                  [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<VideoPlayUrl>>::failure(root.error));
            return;
        }
        callback(parseVideoUrls(*root.value));
    });
}

} // namespace ncm::sdk

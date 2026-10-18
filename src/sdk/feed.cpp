#include "sdk/feed.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto PersonalizedMvsPath = "api/personalized/mv";
constexpr auto PrivateContentsPath = "api/personalized/privatecontent";
constexpr auto HomepagePath = "api/homepage/block/page";

template <typename T>
Result<T> parseFailure(const char *endpoint, const JsonReader &reader, const QString &path)
{
    return Result<T>::failure(
        endpointError(ErrorKind::Parse, endpoint, reader.error(), path));
}

Error objectFailure(const char *endpoint, const QString &path)
{
    return endpointError(ErrorKind::Parse, endpoint,
                         path + QStringLiteral(": expected object"), path);
}

template <typename T>
Result<T> missingFailure(const char *endpoint, const QString &path, const char *what)
{
    return Result<T>::failure(endpointError(
        ErrorKind::Parse, endpoint,
        path + QStringLiteral(": missing ") + QLatin1String(what), path));
}

// Homepage ids are strings in the current API, but the endpoint occasionally
// serves numbers; keep both shapes without weakening JsonReader's field typing.
qint64 numericId(const QJsonObject &object, const char *key)
{
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    if (value.isString()) {
        bool ok = false;
        const qint64 id = value.toString().toLongLong(&ok);
        return ok ? id : 0;
    }
    return 0;
}

Result<PersonalizedMv> mvFrom(const QJsonObject &object, const QString &path,
                              const char *endpoint)
{
    JsonReader reader(object, path);
    PersonalizedMv mv;
    mv.id = reader.integer("id");
    mv.type = static_cast<int>(reader.integer("type"));
    mv.name = reader.string("name");
    mv.copywriter = reader.string("copywriter");
    mv.cover = reader.string("picUrl");
    mv.duration = reader.integer("duration") / 1000;
    mv.playCount = reader.integer("playCount");
    mv.artistId = reader.integer("artistId");
    mv.artistName = reader.string("artistName");
    mv.artists = objectNameList(reader.array("artists"));
    mv.alg = reader.string("alg");
    if (!reader.ok())
        return parseFailure<PersonalizedMv>(endpoint, reader, path);
    return Result<PersonalizedMv>::success(mv);
}

Result<PrivateContent> privateContentFrom(const QJsonObject &object, const QString &path,
                                          const char *endpoint)
{
    JsonReader reader(object, path);
    PrivateContent content;
    content.id = reader.integer("id");
    content.type = static_cast<int>(reader.integer("type"));
    content.name = reader.string("name");
    content.copywriter = reader.string("copywriter");
    content.cover = reader.string("picUrl");
    content.smallCover = reader.string("sPicUrl");
    content.url = reader.string("url");
    content.alg = reader.string("alg");
    if (!reader.ok())
        return parseFailure<PrivateContent>(endpoint, reader, path);
    return Result<PrivateContent>::success(content);
}

Result<HomepageCard> bannerFrom(const QJsonObject &object, const QString &path,
                                const char *endpoint)
{
    JsonReader reader(object, path);
    HomepageCard card;
    card.id = numericId(object, "bannerId");
    card.type = QStringLiteral("banner");
    card.title = reader.string("typeTitle");
    card.subtitle = reader.string("mainTitle");
    card.cover = reader.string("pic");
    card.action = reader.string("url");
    if (!reader.ok())
        return parseFailure<HomepageCard>(endpoint, reader, path);
    return Result<HomepageCard>::success(card);
}

Result<HomepageCard> cardFrom(const QJsonObject &object, const QString &path,
                              const QString &section, const char *endpoint)
{
    JsonReader reader(object, path);
    HomepageCard card;
    card.section = section;
    card.id = numericId(object, "resourceId");
    card.type = reader.string("resourceType");
    card.action = reader.string("action");
    if (card.action.isEmpty())
        card.action = reader.string("resourceUrl");
    if (!reader.ok())
        return parseFailure<HomepageCard>(endpoint, reader, path);

    const QJsonObject ui = reader.object("uiElement");
    JsonReader uiReader(ui, path + QStringLiteral(".uiElement"));
    if (!reader.ok() || !uiReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          reader.ok() ? uiReader : reader, path);

    const QJsonObject mainTitle = uiReader.object("mainTitle");
    JsonReader titleReader(mainTitle, path + QStringLiteral(".uiElement.mainTitle"));
    if (!uiReader.ok() || !titleReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          uiReader.ok() ? titleReader : uiReader, path);
    card.title = titleReader.string("title");
    if (card.title.isEmpty())
        card.title = titleReader.string("titleDesc");
    if (!titleReader.ok())
        return parseFailure<HomepageCard>(endpoint, titleReader, path);

    const QJsonObject subTitle = uiReader.object("subTitle");
    JsonReader subTitleReader(subTitle, path + QStringLiteral(".uiElement.subTitle"));
    if (!uiReader.ok() || !subTitleReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          uiReader.ok() ? subTitleReader : uiReader, path);
    card.subtitle = subTitleReader.string("title");
    if (!subTitleReader.ok())
        return parseFailure<HomepageCard>(endpoint, subTitleReader, path);

    const QJsonObject image = uiReader.object("image");
    JsonReader imageReader(image, path + QStringLiteral(".uiElement.image"));
    if (!uiReader.ok() || !imageReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          uiReader.ok() ? imageReader : uiReader, path);
    card.cover = imageReader.string("imageUrl");
    card.labels = uiReader.stringList("labelTexts");
    if (!imageReader.ok() || !uiReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          imageReader.ok() ? uiReader : imageReader, path);

    const QJsonObject ext = reader.object("resourceExtInfo");
    JsonReader extReader(ext, path + QStringLiteral(".resourceExtInfo"));
    if (!reader.ok() || !extReader.ok())
        return parseFailure<HomepageCard>(endpoint,
                                          reader.ok() ? extReader : reader, path);
    card.playCount = extReader.integer("playCount");
    card.artists = objectNameList(extReader.array("artists"));
    if (!extReader.ok())
        return parseFailure<HomepageCard>(endpoint, extReader, path);

    if (card.artists.isEmpty()) {
        const QJsonObject songData = extReader.object("songData");
        JsonReader songReader(songData,
                              path + QStringLiteral(".resourceExtInfo.songData"));
        if (!extReader.ok() || !songReader.ok())
            return parseFailure<HomepageCard>(endpoint,
                                              extReader.ok() ? songReader : extReader, path);
        card.artists = objectNameList(songReader.array("artists"));
        if (!songReader.ok())
            return parseFailure<HomepageCard>(endpoint, songReader, path);
    }

    return Result<HomepageCard>::success(card);
}

Result<HomepageBlock> blockFrom(const QJsonObject &object, const QString &path,
                                const char *endpoint)
{
    JsonReader reader(object, path);
    HomepageBlock block;
    block.blockCode = reader.string("blockCode");
    block.showType = reader.string("showType");
    if (!reader.ok())
        return parseFailure<HomepageBlock>(endpoint, reader, path);

    const QJsonObject ext = reader.object("extInfo");
    JsonReader extReader(ext, path + QStringLiteral(".extInfo"));
    if (!reader.ok() || !extReader.ok())
        return parseFailure<HomepageBlock>(endpoint,
                                           reader.ok() ? extReader : reader, path);
    const QJsonArray banners = extReader.array("banners");
    if (!extReader.ok())
        return parseFailure<HomepageBlock>(endpoint, extReader, path);
    for (qsizetype i = 0; i < banners.size(); ++i) {
        const QString bannerPath = path + QStringLiteral(".extInfo.banners[%1]").arg(i);
        if (!banners.at(i).isObject())
            return Result<HomepageBlock>::failure(objectFailure(endpoint, bannerPath));
        const auto parsed = bannerFrom(banners.at(i).toObject(), bannerPath, endpoint);
        if (!parsed)
            return Result<HomepageBlock>::failure(parsed.error);
        if (!parsed.value->title.isEmpty() || !parsed.value->cover.isEmpty())
            block.cards.append(*parsed.value);
    }

    const QJsonArray creatives = reader.array("creatives");
    if (!reader.ok())
        return parseFailure<HomepageBlock>(endpoint, reader, path);
    for (qsizetype c = 0; c < creatives.size(); ++c) {
        const QString creativePath = path + QStringLiteral(".creatives[%1]").arg(c);
        if (!creatives.at(c).isObject())
            return Result<HomepageBlock>::failure(objectFailure(endpoint, creativePath));
        JsonReader creativeReader(creatives.at(c).toObject(), creativePath);

        const QJsonObject creativeUi = creativeReader.object("uiElement");
        JsonReader creativeUiReader(creativeUi, creativePath + QStringLiteral(".uiElement"));
        if (!creativeReader.ok() || !creativeUiReader.ok())
            return parseFailure<HomepageBlock>(endpoint,
                                               creativeReader.ok() ? creativeUiReader
                                                                   : creativeReader,
                                               creativePath);
        const QJsonObject creativeTitle = creativeUiReader.object("mainTitle");
        JsonReader creativeTitleReader(
            creativeTitle, creativePath + QStringLiteral(".uiElement.mainTitle"));
        if (!creativeUiReader.ok() || !creativeTitleReader.ok())
            return parseFailure<HomepageBlock>(endpoint,
                                               creativeUiReader.ok() ? creativeTitleReader
                                                                     : creativeUiReader,
                                               creativePath);
        const QString section = creativeTitleReader.string("title");
        if (!creativeTitleReader.ok())
            return parseFailure<HomepageBlock>(endpoint, creativeTitleReader, creativePath);

        const QJsonArray resources = creativeReader.array("resources");
        if (!creativeReader.ok())
            return parseFailure<HomepageBlock>(endpoint, creativeReader, creativePath);
        for (qsizetype r = 0; r < resources.size(); ++r) {
            const QString cardPath =
                creativePath + QStringLiteral(".resources[%1]").arg(r);
            if (!resources.at(r).isObject())
                return Result<HomepageBlock>::failure(objectFailure(endpoint, cardPath));
            const auto parsed =
                cardFrom(resources.at(r).toObject(), cardPath, section, endpoint);
            if (!parsed)
                return Result<HomepageBlock>::failure(parsed.error);
            if (!parsed.value->title.isEmpty() || !parsed.value->cover.isEmpty())
                block.cards.append(*parsed.value);
        }
    }

    return Result<HomepageBlock>::success(block);
}

} // namespace

Result<QList<PersonalizedMv>> parsePersonalizedMvs(const QJsonObject &root)
{
    JsonReader reader(root);
    const QJsonArray items = reader.array("result");
    if (!reader.ok())
        return parseFailure<QList<PersonalizedMv>>(PersonalizedMvsPath, reader,
                                                   QStringLiteral("result"));
    if (!reader.has("result"))
        return missingFailure<QList<PersonalizedMv>>(PersonalizedMvsPath,
                                                     QStringLiteral("result"), "array");

    QList<PersonalizedMv> mvs;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("result[%1]").arg(i);
        if (!items.at(i).isObject())
            return Result<QList<PersonalizedMv>>::failure(
                objectFailure(PersonalizedMvsPath, path));
        const auto parsed = mvFrom(items.at(i).toObject(), path, PersonalizedMvsPath);
        if (!parsed)
            return Result<QList<PersonalizedMv>>::failure(parsed.error);
        if (parsed.value->id > 0)
            mvs.append(*parsed.value);
    }
    return Result<QList<PersonalizedMv>>::success(mvs);
}

Result<QList<PrivateContent>> parsePrivateContents(const QJsonObject &root)
{
    JsonReader reader(root);
    const QJsonArray items = reader.array("result");
    if (!reader.ok())
        return parseFailure<QList<PrivateContent>>(PrivateContentsPath, reader,
                                                   QStringLiteral("result"));
    if (!reader.has("result"))
        return missingFailure<QList<PrivateContent>>(PrivateContentsPath,
                                                     QStringLiteral("result"), "array");

    QList<PrivateContent> contents;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("result[%1]").arg(i);
        if (!items.at(i).isObject())
            return Result<QList<PrivateContent>>::failure(
                objectFailure(PrivateContentsPath, path));
        const auto parsed =
            privateContentFrom(items.at(i).toObject(), path, PrivateContentsPath);
        if (!parsed)
            return Result<QList<PrivateContent>>::failure(parsed.error);
        if (parsed.value->id > 0)
            contents.append(*parsed.value);
    }
    return Result<QList<PrivateContent>>::success(contents);
}

Result<HomepageBlockPage> parseHomepageBlockPage(const QJsonObject &root)
{
    JsonReader reader(root);
    const QJsonObject data = reader.object("data");
    if (!reader.ok())
        return parseFailure<HomepageBlockPage>(HomepagePath, reader,
                                               QStringLiteral("data"));
    if (!reader.has("data"))
        return missingFailure<HomepageBlockPage>(HomepagePath, QStringLiteral("data"),
                                                 "object");

    JsonReader dataReader(data, QStringLiteral("data"));
    const QJsonArray blocks = dataReader.array("blocks");
    if (!dataReader.ok())
        return parseFailure<HomepageBlockPage>(HomepagePath, dataReader,
                                               QStringLiteral("data.blocks"));
    if (!dataReader.has("blocks"))
        return missingFailure<HomepageBlockPage>(HomepagePath,
                                                 QStringLiteral("data.blocks"), "array");

    HomepageBlockPage page;
    page.cursor = dataReader.string("cursor");
    page.hasMore = dataReader.boolean("hasMore");
    if (!dataReader.ok())
        return parseFailure<HomepageBlockPage>(HomepagePath, dataReader,
                                               QStringLiteral("data"));

    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QString path = QStringLiteral("data.blocks[%1]").arg(i);
        if (!blocks.at(i).isObject())
            return Result<HomepageBlockPage>::failure(
                objectFailure(HomepagePath, path));
        const auto parsed = blockFrom(blocks.at(i).toObject(), path, HomepagePath);
        if (!parsed)
            return Result<HomepageBlockPage>::failure(parsed.error);
        if (!parsed.value->blockCode.isEmpty())
            page.blocks.append(*parsed.value);
    }
    return Result<HomepageBlockPage>::success(page);
}

void fetchPersonalizedMvs(NcmClient &client, QObject *context,
                          PersonalizedMvsCallback callback)
{
    postFormJson(client, context, QString::fromLatin1(PersonalizedMvsPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<PersonalizedMv>>::failure(root.error));
            return;
        }
        callback(parsePersonalizedMvs(*root.value));
    }, client.hasMusicU());
}

void fetchPrivateContents(NcmClient &client, QObject *context,
                          PrivateContentsCallback callback)
{
    postFormJson(client, context, QString::fromLatin1(PrivateContentsPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<PrivateContent>>::failure(root.error));
            return;
        }
        callback(parsePrivateContents(*root.value));
    }, client.hasMusicU());
}

void fetchHomepageBlockPage(NcmClient &client, bool refresh, QString cursor,
                            QObject *context, HomepageBlockPageCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("refresh"),
                      refresh ? QStringLiteral("true") : QStringLiteral("false"));
    if (!cursor.isEmpty())
        form.addQueryItem(QStringLiteral("cursor"), cursor);
    postFormJson(client, context, QString::fromLatin1(HomepagePath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<HomepageBlockPage>::failure(root.error));
            return;
        }
        callback(parseHomepageBlockPage(*root.value));
    }, client.hasMusicU());
}

} // namespace ncm::sdk

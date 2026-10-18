#include "sdk/song_wiki.h"

#include "sdk/sdk_json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVariantList>

namespace ncm::sdk {
namespace {

constexpr auto WikiEndpoint = "api/link/page/parent/relation/construct/info";

Error makeError(ErrorKind kind, const QString &message, const QString &path = {})
{
    Error error;
    error.kind = kind;
    error.endpoint = QString::fromLatin1(WikiEndpoint);
    error.path = path;
    error.message = message;
    return error;
}

Result<SongWiki> parseFailure(const QString &message, const QString &path)
{
    return Result<SongWiki>::failure(makeError(ErrorKind::Parse, message, path));
}

qint64 mvIdFromJumpUrl(const QString &url)
{
    static const QRegularExpression re(QStringLiteral("(?:orpheus://mv/|/mv/)(\\d+)"));
    const auto match = re.match(url);
    return match.hasMatch() ? match.captured(1).toLongLong() : 0;
}

QVariantList stringsVariant(const QStringList &values)
{
    QVariantList out;
    out.reserve(values.size());
    for (const QString &value : values)
        out.append(value);
    return out;
}

QString appendBasic(const QJsonObject &info, const QString &path, SongWiki &out)
{
    const QJsonArray elements = info.value(QStringLiteral("wikiSubElementVos")).toArray();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements.at(i).toObject();
        JsonReader reader(element, QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiBasicRow row;
        row.title = reader.string("title");
        row.content = reader.string("content");
        if (!reader.ok())
            return reader.error();
        for (const QJsonValue &meta : element.value(QStringLiteral("wikiSubMetaVos")).toArray())
            row.tags.append(meta.toObject().value(QStringLiteral("text")).toString());
        if (row.tags.isEmpty() && row.content.isEmpty())
            continue;
        out.basic.append(row);
    }
    return {};
}

QString appendMvs(const QJsonObject &info, const QString &path, SongWiki &out)
{
    const QJsonArray elements = info.value(QStringLiteral("wikiSubElementVos")).toArray();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements.at(i).toObject();
        JsonReader reader(element, QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiMv mv;
        mv.jumpUrl = reader.string("jumpUrl");
        mv.id = mvIdFromJumpUrl(mv.jumpUrl);
        mv.name = reader.string("title");
        mv.artist = reader.string("artists");
        mv.cover = reader.string("imgUrl");
        mv.duration = reader.integer("duration") / 1000;
        mv.publishTime = reader.string("publishTime");
        mv.playCount = reader.integer("playNum");
        if (!reader.ok())
            return reader.error();
        if (!mv.name.isEmpty() || mv.id > 0)
            out.mvs.append(mv);
    }
    return {};
}

QString appendReviews(const QJsonObject &info, const QString &path, SongWiki &out)
{
    const QJsonArray elements = info.value(QStringLiteral("wikiSubElementVos")).toArray();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements.at(i).toObject();
        JsonReader reader(element, QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiReview review;
        review.id = reader.integer("id");
        review.type = reader.string("typeName");
        review.content = reader.string("content");
        review.user = reader.string("commenter");
        review.likes = reader.integer("likes");
        if (!reader.ok())
            return reader.error();
        if (!review.content.isEmpty())
            out.reviews.append(review);
    }
    return {};
}

QString appendSheets(const QJsonObject &info, const QString &path, SongWiki &out)
{
    const QJsonArray elements = info.value(QStringLiteral("wikiSubElementVos")).toArray();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements.at(i).toObject();
        JsonReader reader(element, QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiSheet sheet;
        sheet.id = reader.integer("id");
        sheet.name = reader.string("name");
        for (const QJsonValue &type : element.value(QStringLiteral("type")).toArray())
            sheet.types.append(type.toObject().value(QStringLiteral("name")).toString());
        sheet.difficulty = reader.string("difficulty");
        sheet.key = reader.string("musicKey");
        sheet.bpm = reader.integer("bpm");
        sheet.pages = reader.integer("totalPageSize");
        sheet.cover = reader.string("coverImageUrl");
        if (!reader.ok())
            return reader.error();
        if (!sheet.name.isEmpty())
            out.sheets.append(sheet);
    }
    return {};
}

QString appendEntertainments(const QJsonObject &info, const QString &path, SongWiki &out)
{
    const QJsonArray elements = info.value(QStringLiteral("wikiSubElementVos")).toArray();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const QJsonObject element = elements.at(i).toObject();
        JsonReader reader(element, QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiEntertainment item;
        item.title = reader.string("title");
        item.type = reader.string("type");
        item.resourceType = reader.string("resourceType");
        item.cover = reader.string("imgUrl");
        if (!reader.ok())
            return reader.error();
        if (!item.title.isEmpty())
            out.entertainments.append(item);
    }
    return {};
}

QString appendAwards(const QJsonObject &info, const QString &path, SongWiki &out)
{
    JsonReader infoReader(info, path);
    const QJsonArray elements = infoReader.array("wikiSubElementVos");
    if (!infoReader.ok())
        return infoReader.error();
    for (qsizetype i = 0; i < elements.size(); ++i) {
        JsonReader reader(elements.at(i).toObject(),
                          QStringLiteral("%1.wikiSubElementVos[%2]").arg(path).arg(i));
        SongWikiAward award;
        award.title = reader.string("title");
        award.awardName = reader.string("subTitle");
        award.result = reader.string("content");
        award.cover = reader.string("imgUrl");
        award.jumpUrl = reader.string("jumpUrl");
        if (!reader.ok())
            return reader.error();
        if (award.title.isEmpty() && award.awardName.isEmpty() && award.result.isEmpty())
            continue;
        out.awards.append(award);
    }
    return {};
}


QString parseMemory(const QJsonObject &rn, const QString &path, SongWiki &out)
{
    JsonReader reader(rn, path + QStringLiteral(".rnData"));
    const QJsonObject first = reader.object("firstListen");
    const QJsonObject total = reader.object("totalPlay");
    JsonReader firstReader(first, path + QStringLiteral(".rnData.firstListen"));
    JsonReader totalReader(total, path + QStringLiteral(".rnData.totalPlay"));
    out.memory.period = firstReader.string("period");
    out.memory.season = firstReader.string("season");
    out.memory.time = firstReader.string("time");
    out.memory.timestamp = firstReader.integer("timestamp");
    out.memory.playCount = totalReader.integer("playCount");
    out.memory.text = totalReader.string("text");
    if (!reader.ok())
        return reader.error();
    if (!firstReader.ok())
        return firstReader.error();
    if (!totalReader.ok())
        return totalReader.error();
    return {};
}

void parseCreators(const QJsonObject &root, SongWiki &out)
{
    if (root.isEmpty())
        return;
    JsonReader rootReader(root, QStringLiteral("creators"));
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return;
    for (const QJsonValue &roleValue : data.value(QStringLiteral("songCreatorsRoleVos")).toArray()) {
        const QJsonObject roleObject = roleValue.toObject();
        JsonReader roleReader(roleObject, QStringLiteral("creators.data.songCreatorsRoleVos[]"));
        SongWikiCreatorRole role;
        role.role = roleReader.string("roleName");
        const QJsonArray people = roleObject.value(QStringLiteral("creatorMetaVOS")).toArray();
        for (qsizetype i = 0; i < people.size(); ++i) {
            const QJsonObject personObject = people.at(i).toObject();
            JsonReader personReader(personObject,
                                    QStringLiteral("creators.data.songCreatorsRoleVos[].creatorMetaVOS[%1]").arg(i));
            SongWikiCreator person;
            person.artistId = personReader.integer("artistId");
            person.name = personReader.string("artistName");
            person.avatar = personReader.string("artistPic");
            person.subscribed = personReader.boolean("isSub");
            if (!person.name.isEmpty())
                role.people.append(person);
        }
        if (!role.people.isEmpty())
            out.creators.append(role);
    }
}

void parseChorus(const QJsonObject &root, qint64 songId, SongWiki &out)
{
    if (root.isEmpty())
        return;
    QJsonArray items = root.value(QStringLiteral("chorus")).toArray();
    if (items.isEmpty())
        items = root.value(QStringLiteral("data")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (static_cast<qint64>(item.value(QStringLiteral("id")).toDouble()) != songId)
            continue;
        const double start = item.value(QStringLiteral("startTime")).toDouble();
        const double end = item.value(QStringLiteral("endTime")).toDouble();
        if (start >= 0 && end > start) {
            const QJsonValue locked = item.value(QStringLiteral("ugcLocked"));
            out.chorus.start = start / 1000.0;
            out.chorus.end = end / 1000.0;
            out.chorus.locked = locked.isBool() ? locked.toBool() : locked.toInt() != 0;
        }
        break;
    }
}

} // namespace

Result<SongWiki> SongWiki::fromApi(const QJsonObject &wikiRoot,
                                   const QJsonObject &creatorsRoot,
                                   const QJsonObject &chorusRoot,
                                   qint64 songId)
{
    if (songId <= 0)
        return Result<SongWiki>::failure(makeError(
            ErrorKind::InvalidRequest, QStringLiteral("songId must be positive")));

    JsonReader root(wikiRoot, QStringLiteral("wiki"));
    const QJsonObject data = root.object("data");
    if (!root.ok() || !root.has("data"))
        return parseFailure(QStringLiteral("missing wiki.data"), QStringLiteral("wiki.data"));
    JsonReader dataReader(data, QStringLiteral("wiki.data"));
    const QJsonArray blocks = dataReader.array("blocks");
    if (!dataReader.ok() || !data.contains(QStringLiteral("blocks")))
        return parseFailure(QStringLiteral("missing wiki.data.blocks"), QStringLiteral("wiki.data.blocks"));

    SongWiki out;
    out.songId = songId;

    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QJsonObject block = blocks.at(i).toObject();
        const QString path = QStringLiteral("wiki.data.blocks[%1]").arg(i);
        JsonReader blockReader(block, path);
        const QJsonObject rn = blockReader.object("rnData");
        if (!blockReader.ok())
            return parseFailure(blockReader.error(), path);
        if (!rn.isEmpty()) {
            if (rn.contains(QStringLiteral("firstListen")) || rn.contains(QStringLiteral("totalPlay"))) {
                const QString error = parseMemory(rn, path, out);
                if (!error.isEmpty())
                    return parseFailure(error, path);
            }
            if (rn.contains(QStringLiteral("songName"))) {
                JsonReader detail(rn, path + QStringLiteral(".rnData"));
                out.songName = detail.string("songName");
                out.albumName = detail.string("albumName");
                out.artistName = detail.string("artistName");
                out.cover = detail.string("coverImage");
                if (!detail.ok())
                    return parseFailure(detail.error(), path + QStringLiteral(".rnData"));
            }
            const QJsonObject songDetail = rn;
            const QJsonArray subBlocks = songDetail.value(QStringLiteral("blocks")).toArray();
            for (qsizetype j = 0; j < subBlocks.size(); ++j) {
                const QJsonObject sub = subBlocks.at(j).toObject();
                const QString subPath = QStringLiteral("%1.rnData.blocks[%2]").arg(path).arg(j);
                JsonReader subReader(sub, subPath);
                const QString code = subReader.string("blockCode");
                const QJsonObject info = subReader.object("blockInfo");
                if (!subReader.ok())
                    return parseFailure(subReader.error(), subPath);
                QString error;
                if (code == QLatin1String("wikiSubBlockSongInfoVo")) {
                    const QString desc = info.value(QStringLiteral("desc")).toString();
                    if (!desc.isEmpty())
                        out.description = desc;
                } else if (code == QLatin1String("wikiSubBlockBaseInfoVo")) {
                    error = appendBasic(info, subPath + QStringLiteral(".blockInfo"), out);
                } else if (code == QLatin1String("wikiSubBlockMvInfoVo")) {
                    error = appendMvs(info, subPath + QStringLiteral(".blockInfo"), out);
                } else if (code == QLatin1String("wikiSubBlockSongCommentVo")) {
                    error = appendReviews(info, subPath + QStringLiteral(".blockInfo"), out);
                } else if (code == QLatin1String("wikiSubBlockSheetInfoVo")) {
                    error = appendSheets(info, subPath + QStringLiteral(".blockInfo"), out);
                } else if (code == QLatin1String("wikiSubBlockEntertainmentVo")) {
                    error = appendEntertainments(info, subPath + QStringLiteral(".blockInfo"), out);
                } else if (code == QLatin1String("wikiSubBlockAwardRecordVo")
                           || code == QLatin1String("wikiSubBlockAlbumAwardRecordVo")) {
                    error = appendAwards(info, subPath + QStringLiteral(".blockInfo"), out);
                }
                if (!error.isEmpty())
                    return parseFailure(error, subPath);
            }
        }
    }

    parseCreators(creatorsRoot, out);
    parseChorus(chorusRoot, songId, out);
    out.empty = out.songName.isEmpty() && out.description.isEmpty()
        && out.basic.isEmpty() && out.creators.isEmpty() && out.mvs.isEmpty()
        && out.reviews.isEmpty() && out.sheets.isEmpty() && out.entertainments.isEmpty()
        && out.awards.isEmpty() && out.memory.text.isEmpty() && out.memory.time.isEmpty()
        && !out.chorus.valid();
    return Result<SongWiki>::success(std::move(out));
}

QVariantMap SongWiki::toVariantMap() const
{
    QVariantMap memoryMap{
        {"period", memory.period},
        {"season", memory.season},
        {"time", memory.time},
        {"timestamp", memory.timestamp},
        {"playCount", memory.playCount},
        {"text", memory.text},
    };
    QVariantMap chorusMap{
        {"start", chorus.start},
        {"end", chorus.end},
        {"locked", chorus.locked},
    };

    QVariantList basicList;
    for (const SongWikiBasicRow &row : basic)
        basicList.append(QVariantMap{{"title", row.title}, {"content", row.content},
                                     {"tags", stringsVariant(row.tags)}});

    QVariantList creatorList;
    for (const SongWikiCreatorRole &role : creators) {
        QVariantList people;
        for (const SongWikiCreator &person : role.people)
            people.append(QVariantMap{{"id", person.artistId}, {"name", person.name},
                                      {"avatar", person.avatar}, {"subscribed", person.subscribed}});
        creatorList.append(QVariantMap{{"role", role.role}, {"people", people}});
    }

    QVariantList mvList;
    for (const SongWikiMv &mv : mvs)
        mvList.append(QVariantMap{{"id", mv.id}, {"jumpUrl", mv.jumpUrl}, {"name", mv.name},
                                  {"artist", mv.artist}, {"cover", mv.cover},
                                  {"duration", mv.duration}, {"publishTime", mv.publishTime},
                                  {"playCount", mv.playCount}});

    QVariantList reviewList;
    for (const SongWikiReview &review : reviews)
        reviewList.append(QVariantMap{{"id", review.id}, {"type", review.type},
                                      {"content", review.content}, {"user", review.user},
                                      {"likes", review.likes}});

    QVariantList sheetList;
    for (const SongWikiSheet &sheet : sheets)
        sheetList.append(QVariantMap{{"id", sheet.id}, {"name", sheet.name},
                                     {"types", stringsVariant(sheet.types)},
                                     {"difficulty", sheet.difficulty}, {"key", sheet.key},
                                     {"bpm", sheet.bpm}, {"pages", sheet.pages},
                                     {"cover", sheet.cover}});

    QVariantList entertainmentList;
    for (const SongWikiEntertainment &item : entertainments)
        entertainmentList.append(QVariantMap{{"title", item.title}, {"type", item.type},
                                             {"resourceType", item.resourceType},
                                             {"cover", item.cover}});

    QVariantList awardList;
    for (const SongWikiAward &award : awards)
        awardList.append(QVariantMap{{"title", award.title}, {"awardName", award.awardName},
                                     {"result", award.result}, {"cover", award.cover},
                                     {"jumpUrl", award.jumpUrl}});

    return {
        {"songId", songId},
        {"loaded", true},
        {"empty", empty},
        {"songName", songName},
        {"albumName", albumName},
        {"artistName", artistName},
        {"cover", cover},
        {"description", description},
        {"memory", memoryMap},
        {"chorus", chorusMap},
        {"basic", basicList},
        {"creators", creatorList},
        {"mvs", mvList},
        {"reviews", reviewList},
        {"sheets", sheetList},
        {"entertainments", entertainmentList},
        {"awards", awardList},
    };
}

QJsonObject SongWiki::toJson() const
{
    return QJsonObject::fromVariantMap(toVariantMap());
}

} // namespace ncm::sdk

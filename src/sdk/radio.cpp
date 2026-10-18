#include "sdk/radio.h"

#include "ncmclient.h"
#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QUrlQuery>
#include <algorithm>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto RecommendedPath = "api/personalized/djprogram";
constexpr auto ByRadioPath = "api/dj/program/byradio";
constexpr auto ProgramPath = "api/dj/program/detail";
constexpr auto RadioPath = "api/djradio/v2/get";


Result<RadioStation> radioFrom(const QJsonObject &object, const QString &path,
                               const char *endpoint)
{
    JsonReader reader(object, path);
    RadioStation radio;
    radio.id = reader.integer("id");
    radio.name = reader.string("name");
    radio.cover = reader.string("picUrl");
    radio.description = reader.string("desc");
    radio.playCount = reader.integer("playCount");
    radio.programCount = reader.integer("programCount");
    radio.subCount = reader.integer("subCount");
    const QJsonObject dj = reader.object("dj");
    JsonReader djReader(dj, path + QStringLiteral(".dj"));
    radio.djId = djReader.integer("userId");
    radio.djName = djReader.string("nickname");
    radio.djAvatar = djReader.string("avatarUrl");
    if (!reader.ok())
        return Result<RadioStation>::failure(
            endpointError(ErrorKind::Parse, endpoint, reader.error(), path));
    if (!djReader.ok())
        return Result<RadioStation>::failure(
            endpointError(ErrorKind::Parse, endpoint, djReader.error(), path));
    return Result<RadioStation>::success(radio);
}

Result<RadioProgram> programFrom(const QJsonObject &object, const QString &path,
                                 const char *endpoint)
{
    JsonReader reader(object, path);
    RadioProgram program;
    program.id = reader.integer("id");
    program.name = reader.string("name");
    program.description = reader.string("description");
    program.createTime = reader.integer("createTime");
    program.duration = reader.integer("duration") / 1000;
    program.serialNum = static_cast<int>(reader.integer("serialNum"));
    program.cover = reader.string("coverUrl");
    const QJsonObject mainSong = reader.object("mainSong");
    // 主歌本体只经 Track 解析；byradio 有时只给 mainTrackId（与 mainSong.id 同值）
    const auto parsedSong = parseTrack(mainSong);
    if (!parsedSong) {
        return Result<RadioProgram>::failure(endpointError(
            ErrorKind::Parse, endpoint, parsedSong.error.message,
            path + QStringLiteral(".mainSong")));
    }
    const Track &song = *parsedSong.value;
    program.mainSongId = song.id;
    if (program.mainSongId <= 0)
        program.mainSongId = reader.integer("mainTrackId");
    program.mainSongName = song.name;
    program.mainSongDuration = song.duration;
    program.fee = static_cast<int>(song.fee);
    for (const TrackArtist &artist : song.artists)
        if (!artist.name.isEmpty())
            program.artists.append(artist.name);
    const QJsonObject radio = reader.object("radio");
    if (!radio.isEmpty()) {
        const auto parsed = radioFrom(radio, path + QStringLiteral(".radio"), endpoint);
        if (!parsed)
            return Result<RadioProgram>::failure(parsed.error);
        program.radio = *parsed.value;
    }
    if (!reader.ok())
        return Result<RadioProgram>::failure(
            endpointError(ErrorKind::Parse, endpoint, reader.error(), path));
    return Result<RadioProgram>::success(program);
}

} // namespace

Result<QList<RadioProgram>> parseRecommendedPrograms(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("radio.recommended"));
    const QJsonArray items = reader.array("result");
    if (!reader.ok())
        return Result<QList<RadioProgram>>::failure(
            endpointError(ErrorKind::Parse, RecommendedPath, reader.error(),
                          QStringLiteral("radio.recommended")));
    if (!reader.has("result"))
        return Result<QList<RadioProgram>>::failure(
            endpointError(ErrorKind::Parse, RecommendedPath,
                          QStringLiteral("radio.recommended.result: missing array"),
                          QStringLiteral("radio.recommended.result")));

    QList<RadioProgram> programs;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QString path = QStringLiteral("radio.recommended.result[%1]").arg(i);
        JsonReader item(items.at(i).toObject(), path);
        const QJsonObject program = item.object("program");
        if (!item.ok())
            return Result<QList<RadioProgram>>::failure(
                endpointError(ErrorKind::Parse, RecommendedPath, item.error(), path));
        if (program.isEmpty())
            continue;
        const auto parsed = programFrom(program, path + QStringLiteral(".program"), RecommendedPath);
        if (!parsed)
            return Result<QList<RadioProgram>>::failure(parsed.error);
        programs.append(*parsed.value);
    }
    return Result<QList<RadioProgram>>::success(programs);
}

Result<RadioProgramPage> parseRadioProgramPage(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("radio.programPage"));
    const QJsonArray items = reader.array("programs");
    if (!reader.ok())
        return Result<RadioProgramPage>::failure(
            endpointError(ErrorKind::Parse, ByRadioPath, reader.error(),
                          QStringLiteral("radio.programPage")));
    if (!reader.has("programs"))
        return Result<RadioProgramPage>::failure(
            endpointError(ErrorKind::Parse, ByRadioPath,
                          QStringLiteral("radio.programPage.programs: missing array"),
                          QStringLiteral("radio.programPage.programs")));

    RadioProgramPage page;
    page.count = reader.integer("count");
    page.more = reader.boolean("more");
    if (!reader.ok())
        return Result<RadioProgramPage>::failure(
            endpointError(ErrorKind::Parse, ByRadioPath, reader.error(),
                          QStringLiteral("radio.programPage")));
    for (qsizetype i = 0; i < items.size(); ++i) {
        const auto parsed = programFrom(items.at(i).toObject(),
                                        QStringLiteral("radio.programPage.programs[%1]").arg(i),
                                        ByRadioPath);
        if (!parsed)
            return Result<RadioProgramPage>::failure(parsed.error);
        page.items.append(*parsed.value);
    }
    return Result<RadioProgramPage>::success(page);
}

Result<RadioProgram> parseRadioProgram(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("radio.program"));
    const QJsonObject program = reader.object("program");
    if (!reader.ok())
        return Result<RadioProgram>::failure(
            endpointError(ErrorKind::Parse, ProgramPath, reader.error(),
                          QStringLiteral("radio.program")));
    if (program.isEmpty())
        return Result<RadioProgram>::failure(
            endpointError(ErrorKind::Parse, ProgramPath,
                          QStringLiteral("radio.program.program: missing object"),
                          QStringLiteral("radio.program.program")));
    return programFrom(program, QStringLiteral("radio.program.program"), ProgramPath);
}

Track trackFromProgram(const RadioProgram &program, const QString &radioName,
                       const QString &djName, const QString &radioCover)
{
    const QString album = program.radio.name.isEmpty() ? radioName : program.radio.name;
    const QString host = program.radio.djName.isEmpty() ? djName : program.radio.djName;
    QString cover = program.cover;
    if (cover.isEmpty())
        cover = program.radio.cover;
    if (cover.isEmpty())
        cover = radioCover;

    Track track;
    track.id = program.mainSongId;
    // 节目名/主播/电台/封面是 Program 文档里的覆盖，不取主歌自己的元数据
    track.name = program.name;
    track.artists.append(TrackArtist{0, host});
    track.albumName = album;
    track.cover = cover;
    track.duration = program.mainSongDuration;
    track.fee = program.fee;
    return track;
}

QVariantMap programToVariantMap(const RadioProgram &program, const QString &radioName,
                                const QString &djName, const QString &radioCover,
                                qint64 radioId)
{
    QVariantMap map = trackToVariantMap(
        trackFromProgram(program, radioName, djName, radioCover), QString(), 0);
    const qint64 stationId = program.radio.id > 0 ? program.radio.id : radioId;
    map.insert("programId", static_cast<double>(program.id));
    map.insert("radioId", static_cast<double>(stationId));
    map.insert("serial", static_cast<double>(program.serialNum));
    map.insert("createTime", static_cast<double>(program.createTime));
    map.insert("description", program.description);
    return map;
}

Result<RadioStation> parseRadioStation(const QJsonObject &root)
{
    JsonReader reader(root, QStringLiteral("radio.station"));
    const QJsonObject data = reader.object("data");
    if (!reader.ok())
        return Result<RadioStation>::failure(
            endpointError(ErrorKind::Parse, RadioPath, reader.error(),
                          QStringLiteral("radio.station")));
    if (data.isEmpty())
        return Result<RadioStation>::failure(
            endpointError(ErrorKind::Parse, RadioPath,
                          QStringLiteral("radio.station.data: missing object"),
                          QStringLiteral("radio.station.data")));
    return radioFrom(data, QStringLiteral("radio.station.data"), RadioPath);
}

void fetchRecommendedPrograms(NcmClient &client, QObject *context,
                              RadioProgramsCallback callback)
{
    postFormJson(client, context, QString::fromLatin1(RecommendedPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<QList<RadioProgram>>::failure(root.error));
            return;
        }
        callback(parseRecommendedPrograms(*root.value));
    });
}

void fetchRadioPrograms(NcmClient &client, qint64 radioId, int limit, int offset,
                        bool asc, QObject *context, RadioProgramPageCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("radioId"), QString::number(radioId));
    form.addQueryItem(QStringLiteral("limit"), QString::number(std::max(1, limit)));
    form.addQueryItem(QStringLiteral("offset"), QString::number(std::max(0, offset)));
    form.addQueryItem(QStringLiteral("asc"), asc ? QStringLiteral("true") : QStringLiteral("false"));
    postFormJson(client, context, QString::fromLatin1(ByRadioPath), form,
                 [callback = std::move(callback), offset](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<RadioProgramPage>::failure(root.error));
            return;
        }
        auto page = parseRadioProgramPage(*root.value);
        if (page)
            page.value->offset = offset;
        callback(std::move(page));
    });
}

void fetchProgram(NcmClient &client, qint64 programId, QObject *context,
                  RadioProgramCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("id"), QString::number(programId));
    postFormJson(client, context, QString::fromLatin1(ProgramPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<RadioProgram>::failure(root.error));
            return;
        }
        callback(parseRadioProgram(*root.value));
    });
}

void fetchRadio(NcmClient &client, qint64 radioId, QObject *context,
                RadioStationCallback callback)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("id"), QString::number(radioId));
    postFormJson(client, context, QString::fromLatin1(RadioPath), form,
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<RadioStation>::failure(root.error));
            return;
        }
        callback(parseRadioStation(*root.value));
    });
}

} // namespace ncm::sdk

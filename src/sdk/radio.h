#pragma once
// Podcast / radio SDK — typed read models for the audio-program surfaces.
//
// Raw endpoints:
//   POST /api/personalized/djprogram      recommended programs
//   POST /api/dj/program/byradio          programs of one radio
//   POST /api/dj/program/detail           one program
//   POST /api/djradio/v2/get              radio detail (data envelope)
//
// Audio playback is not duplicated here: a program exposes mainSongId, which the
// existing song-URL service can resolve. The SDK only owns metadata.
#include "sdk/sdk_result.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct RadioStation {
    qint64 id = 0;
    QString name;
    QString cover;          // picUrl
    QString description;
    qint64 playCount = 0;
    qint64 programCount = 0;
    qint64 subCount = 0;
    qint64 djId = 0;        // dj.userId
    QString djName;         // dj.nickname
    QString djAvatar;       // dj.avatarUrl
};

struct RadioProgram {
    qint64 id = 0;
    QString name;
    QString description;
    qint64 createTime = 0;   // milliseconds
    qint64 duration = 0;     // seconds
    int serialNum = 0;
    QString cover;           // coverUrl (may be empty; fall back to radio.picUrl)
    qint64 mainSongId = 0;   // mainSong.id (fallback mainTrackId)
    QString mainSongName;
    qint64 mainSongDuration = 0; // seconds
    int fee = 0;
    QStringList artists;     // mainSong.artists[].name
    RadioStation radio;      // nested radio (may be partial from recommendations)
};

struct RadioProgramPage {
    QList<RadioProgram> items;
    qint64 count = 0;
    bool more = false;
    int offset = 0;
};

using RadioProgramsCallback = std::function<void(Result<QList<RadioProgram>>)>;
using RadioProgramPageCallback = std::function<void(Result<RadioProgramPage>)>;
using RadioProgramCallback = std::function<void(Result<RadioProgram>)>;
using RadioStationCallback = std::function<void(Result<RadioStation>)>;

void fetchRecommendedPrograms(NcmClient &client, QObject *context,
                              RadioProgramsCallback callback);
void fetchRadioPrograms(NcmClient &client, qint64 radioId, int limit, int offset,
                        bool asc, QObject *context, RadioProgramPageCallback callback);
void fetchProgram(NcmClient &client, qint64 programId, QObject *context,
                  RadioProgramCallback callback);
void fetchRadio(NcmClient &client, qint64 radioId, QObject *context,
                RadioStationCallback callback);

// Pure parsers, exposed for tests and callers that already hold a response.
Result<QList<RadioProgram>> parseRecommendedPrograms(const QJsonObject &root);
Result<RadioProgramPage> parseRadioProgramPage(const QJsonObject &root);
Result<RadioProgram> parseRadioProgram(const QJsonObject &root);
Result<RadioStation> parseRadioStation(const QJsonObject &root);

} // namespace ncm::sdk

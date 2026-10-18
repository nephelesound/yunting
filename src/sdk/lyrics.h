#pragma once
// Lyrics SDK — typed raw lyric payload for the enhanced (word-timed) lyrics view.
//
// Raw endpoint (plaintext /api, anonymous):
//   GET /api/song/lyric/v1  query: id, cp=false, tv/lv/rv/kv/yv/ytv/yrv=0
//
// The response holds one object per lyric format, each with a `lyric` string:
//   lrc.lyric      line-timed LRC
//   tlyric.lyric   translation aligned with the LRC lines
//   yrc.lyric      word-timed YRC (absent when the song has no word timings)
//   ytlrc.lyric    translation aligned with the YRC lines
// A format the song does not have is omitted, null, or carries an empty string;
// all four are therefore plain non-optional strings defaulting to empty.
//
// This SDK keeps the raw text verbatim and deliberately does not pick between
// LRC and YRC, nor fall ytlrc back to tlyric: line parsing and that fallback are
// Store-boundary policy (ncmParseLrc / ncmParseYrc), whose QML shape must not
// change. songId is supplied by the caller, which already knows the song.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QString>

namespace ncm::sdk {

struct SongLyrics {
    qint64 songId = 0; // caller-supplied; the response does not echo the id
    QString lrc;       // lrc.lyric
    QString tlyric;    // tlyric.lyric
    QString yrc;       // yrc.lyric
    QString ytlrc;     // ytlrc.lyric
    bool pureMusic = false; // pureMusic || nolyric (legacy /api/song/lyric)
};

// Validates the API envelope (code != 200 is an Api error, as the transport
// would classify it) and the four lyric containers. Missing/null containers and
// missing/null lyric strings parse as empty; a wrong type is a Parse error whose
// path names the offending field ("lrc", "tlyric.lyric", ...).
Result<SongLyrics> parseSongLyrics(const QJsonObject &root, qint64 songId = 0);

} // namespace ncm::sdk

#pragma once
// Song Wiki SDK — typed models for the NetEase "music encyclopedia" page.
//
// Raw endpoints (the service combines them):
//   POST /api/link/page/parent/relation/construct/info  form: extJson, positionCode
//   POST /api/song/creators                             form: songId
//   POST /api/song/chorus                               form: ids
//
// The raw wiki response is a UI block tree, not a stable public schema. This SDK
// validates and flattens only the fields that can be rendered; unknown blocks and
// unknown keys are ignored, so additive upstream changes are harmless.
//
// Field comments name the raw JSON path. Raw URL values are deliberately kept
// unsized; the frontend chooses its own image size policy.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace ncm::sdk {

// One row of the "基本信息" block. Exactly one of content/tags is normally set.
struct SongWikiBasicRow {
    QString title;        // .blockInfo.wikiSubElementVos[].title
    QString content;      // .content (语种/发行时间/BPM 等)
    QStringList tags;     // .wikiSubMetaVos[].text (曲风/标签)
};

// .blockInfo.wikiSubElementVos[] of wikiSubBlockSongCommentVo
struct SongWikiReview {
    qint64 id = 0;
    QString type;         // typeName（精选乐评）
    QString content;
    QString user;         // commenter
    qint64 likes = 0;
};

// .blockInfo.wikiSubElementVos[] of wikiSubBlockMvInfoVo
struct SongWikiMv {
    qint64 id = 0;        // parsed from jumpUrl "orpheus://mv/<id>"
    QString jumpUrl;      // original link, not necessarily actionable in a web/native app
    QString name;         // title
    QString artist;       // artists
    QString cover;        // imgUrl
    qint64 duration = 0;  // seconds (raw duration is milliseconds)
    QString publishTime;
    qint64 playCount = 0; // playNum
};

// .blockInfo.wikiSubElementVos[] of wikiSubBlockSheetInfoVo
struct SongWikiSheet {
    qint64 id = 0;
    QString name;
    QStringList types;    // type[].name
    QString difficulty;
    QString key;          // musicKey
    qint64 bpm = 0;
    qint64 pages = 0;     // totalPageSize
    QString cover;        // coverImageUrl
};

// .blockInfo.wikiSubElementVos[] of wikiSubBlockEntertainmentVo
struct SongWikiEntertainment {
    QString title;
    QString type;
    QString resourceType;
    QString cover;        // imgUrl
};

// .blockInfo.wikiSubElementVos[] of award blocks
struct SongWikiAward {
    QString title;     // .title（颁奖礼）
    QString awardName; // .subTitle（奖项）
    QString result;    // .content（获/提名）
    QString cover;     // .imgUrl
    QString jumpUrl;   // .jumpUrl
};

// .rnData.firstListen / .rnData.totalPlay of the "回忆坐标" block
struct SongWikiMemory {
    QString period;       // period（深夜/清晨…）
    QString season;       // season
    QString time;         // time（HH:mm）
    qint64 timestamp = 0; // timestamp in milliseconds
    qint64 playCount = 0; // totalPlay.playCount
    QString text;         // totalPlay.text
};

// .data.chorus[0] or .data[0] of /api/song/chorus
struct SongWikiChorus {
    double start = 0;     // startTime / 1000
    double end = 0;       // endTime / 1000
    bool locked = false;  // ugcLocked
    bool valid() const { return start >= 0 && end > start; }
};

// .data.songCreatorsRoleVos[]; roleName groups people[].
struct SongWikiCreator {
    qint64 artistId = 0;
    QString name;         // artistName
    QString avatar;       // artistPic
    bool subscribed = false; // isSub
};
struct SongWikiCreatorRole {
    QString role;         // roleName（作词/作曲/编曲…）
    QList<SongWikiCreator> people; // creatorMetaVOS[]
};

// Main normalized model. Every field has a deterministic default; core blocks are
// validated, optional creator/chorus endpoints may fail without failing the wiki.
struct SongWiki {
    qint64 songId = 0;
    bool empty = false;

    QString songName;     // songDetailNewSongWiki.rnData.songName
    QString albumName;    // .albumName
    QString artistName;   // .artistName
    QString cover;        // .coverImage (raw URL)
    QString description;  // wikiSubBlockSongInfoVo.blockInfo.desc

    SongWikiMemory memory;
    SongWikiChorus chorus;
    QList<SongWikiBasicRow> basic;
    QList<SongWikiCreatorRole> creators;
    QList<SongWikiMv> mvs;
    QList<SongWikiReview> reviews;
    QList<SongWikiSheet> sheets;
    QList<SongWikiEntertainment> entertainments;
    QList<SongWikiAward> awards;

    // Validates and flattens the three raw successful responses.
    // Returns Parse error with a JSON path when the wiki page itself is malformed;
    // creators/chorus are optional and their malformed content is ignored.
    static Result<SongWiki> fromApi(const QJsonObject &wikiRoot,
                                    const QJsonObject &creatorsRoot,
                                    const QJsonObject &chorusRoot,
                                    qint64 songId);

    // Compatibility boundary for the existing QML Store. New frontend code should
    // use the typed structs or the JSON contract in samples/, not this map.
    QVariantMap toVariantMap() const;
    QJsonObject toJson() const;
};

} // namespace ncm::sdk

#pragma once
// Feed SDK — typed models for the personalized "home / discover" surfaces.
//
// Raw endpoints (plaintext /api; MUSIC_U is attached when the client is signed
// in, anonymous requests still return a curated feed):
//   POST /api/personalized/mv             recommended MVs
//   POST /api/personalized/privatecontent exclusive / private content
//   POST /api/homepage/block/page         homepage feed blocks
//
// The homepage response is a layout tree: blocks contain creative groups and
// each group contains resources. This SDK keeps only the fields that can be
// rendered as a card (identity, title, subtitle, cover, action, counters,
// labels, artists) and deliberately ignores descriptors that only the native
// client's layout engine understands. Unknown future fields, blocks and
// creative types therefore do not break parsing.
//
// Field comments name the raw JSON path. Durations are seconds; the raw song/MV
// duration is milliseconds. Play counts are raw counters. URLs are raw; the
// frontend owns image sizing.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class NcmClient;

namespace ncm::sdk {

// result[] of /api/personalized/mv
struct PersonalizedMv {
    qint64 id = 0;            // result[].id
    int type = 0;             // result[].type
    QString name;             // result[].name
    QString copywriter;       // result[].copywriter
    QString cover;            // result[].picUrl
    qint64 duration = 0;      // result[].duration / 1000
    qint64 playCount = 0;     // result[].playCount
    qint64 artistId = 0;      // result[].artistId
    QString artistName;       // result[].artistName
    QStringList artists;      // result[].artists[].name
    QString alg;              // result[].alg
};

// result[] of /api/personalized/privatecontent
struct PrivateContent {
    qint64 id = 0;            // result[].id
    int type = 0;             // result[].type
    QString name;             // result[].name
    QString copywriter;       // result[].copywriter
    QString cover;            // result[].picUrl
    QString smallCover;       // result[].sPicUrl
    QString url;              // result[].url (may be empty)
    QString alg;              // result[].alg
};

// One renderable homepage card. Banners and block resources are normalized into
// this shape so the consumer does not branch on the upstream layout hierarchy.
struct HomepageCard {
    QString section;          // enclosing creatives[].uiElement.mainTitle.title (may be empty)
    qint64 id = 0;            // resourceId / banners[].bannerId (numeric part; 0 when non-numeric)
    QString type;             // resourceType / "banner"
    QString title;            // uiElement.mainTitle.title, fallback .titleDesc / banners[].typeTitle
    QString subtitle;         // uiElement.subTitle.title / banners[].mainTitle
    QString cover;            // uiElement.image.imageUrl / banners[].pic
    QString action;           // action, fallback resourceUrl / banners[].url
    qint64 playCount = 0;     // resourceExtInfo.playCount
    QStringList labels;       // uiElement.labelTexts
    QStringList artists;      // resourceExtInfo.artists[].name,
                              // fallback resourceExtInfo.songData.artists[].name
};

// data.blocks[]
struct HomepageBlock {
    QString blockCode;        // data.blocks[].blockCode
    QString showType;         // data.blocks[].showType
    QList<HomepageCard> cards; // extInfo.banners[] + creatives[].resources[], in order
};

struct HomepageBlockPage {
    QList<HomepageBlock> blocks; // data.blocks
    QString cursor;              // data.cursor (next page token, may be empty)
    bool hasMore = false;        // data.hasMore
};

using PersonalizedMvsCallback = std::function<void(Result<QList<PersonalizedMv>>)>;
using PrivateContentsCallback = std::function<void(Result<QList<PrivateContent>>)>;
using HomepageBlockPageCallback = std::function<void(Result<HomepageBlockPage>)>;

void fetchPersonalizedMvs(NcmClient &client, QObject *context,
                          PersonalizedMvsCallback callback);
void fetchPrivateContents(NcmClient &client, QObject *context,
                          PrivateContentsCallback callback);
void fetchHomepageBlockPage(NcmClient &client, bool refresh, QString cursor,
                            QObject *context, HomepageBlockPageCallback callback);

// Pure parsers, exposed for tests and callers that already hold a response.
Result<QList<PersonalizedMv>> parsePersonalizedMvs(const QJsonObject &root);
Result<QList<PrivateContent>> parsePrivateContents(const QJsonObject &root);
Result<HomepageBlockPage> parseHomepageBlockPage(const QJsonObject &root);

} // namespace ncm::sdk

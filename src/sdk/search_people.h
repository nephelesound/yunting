#pragma once
// Typed Cloudsearch SDK — user and radio result rows.
//
// Raw endpoint (GET):
//   api/cloudsearch/pc?type=1002|1009&s=<query>&limit=20&offset=0
//
// `type=1002` reads result.userprofiles[]; `type=1009` reads result.djRadios[].
// A missing or empty `result` is a valid empty page. Absent/null arrays and
// fields fall back to defaults; a present value of the wrong type is a Parse
// error. URLs stay raw — Store's boundary applies ncmSized(300).
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QList>
#include <QString>

namespace ncm::sdk {

// result.userprofiles[] (type=1002)
struct SearchUser {
    qint64 id = 0;         // userId
    QString name;          // nickname
    QString cover;         // avatarUrl (raw URL, may be empty)
    QString pendant;       // pendantData.imageUrl (raw)
    QString identityIcon;  // avatarDetail.identityIconUrl (raw)
    bool followed = false; // followed
    qint64 fans = 0;       // followeds
    qint64 follows = 0;    // follows
    qint64 playlists = 0;  // playlistCount
    QString signature;     // signature
};

// result.djRadios[] (type=1009)
struct SearchRadio {
    qint64 id = 0;          // id
    QString name;           // name
    QString cover;          // picUrl (raw URL, may be empty)
    QString description;    // desc
    QString host;           // dj.nickname
    qint64 programCount = 0; // programCount
    qint64 subCount = 0;    // subCount
    qint64 playCount = 0;   // playCount
};

struct SearchUsers {
    QString query;           // caller-supplied query (the response has no echo)
    QList<SearchUser> items; // result.userprofiles[] in server order
};

struct SearchRadios {
    QString query;           // caller-supplied query (the response has no echo)
    QList<SearchRadio> items; // result.djRadios[] in server order
};

// Pure parsers. `root` is the whole response ({ code, result }). Callers are
// responsible for checking the envelope's HTTP status / business code.
Result<SearchUsers> parseSearchUsers(const QJsonObject &root, QString query = {});
Result<SearchRadios> parseSearchRadios(const QJsonObject &root, QString query = {});

} // namespace ncm::sdk

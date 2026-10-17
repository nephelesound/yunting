#include "normalize.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace {
// `or ""` 家族：null 与缺失都回到 Python 的缺省值
QString textOr(const QJsonObject &o, const char *key, const QString &fallback = {})
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : fallback;
}

double numOr(const QJsonObject &o, const char *key, double fallback = 0)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : fallback;
}
} // namespace

QString ncmSized(const QJsonValue &url, int px)
{
    const QString s = url.isString() ? url.toString() : QString();
    return s.isEmpty() ? QString() : s + QStringLiteral("?param=%1y%2").arg(px).arg(px);
}

QVariantMap ncmTrack(const QJsonObject &s)
{
    // 新旧两种歌曲结构（ar/al/dt 与 artists/album/duration）
    const QJsonArray artists = s.value("ar").isArray() ? s.value("ar").toArray()
                                                       : s.value("artists").toArray();
    const QJsonObject album = s.value("al").isObject() ? s.value("al").toObject()
                                                       : s.value("album").toObject();
    QStringList names;
    for (const QJsonValue &a : artists)
        names.append(a.toObject().value("name").toString());
    const double dt = numOr(s, "dt") != 0 ? numOr(s, "dt") : numOr(s, "duration");

    return {
        {"id", numOr(s, "id")},
        {"name", textOr(s, "name")},
        {"artist", names.join(QStringLiteral(" / "))},
        {"album", textOr(album, "name")},
        {"dur", std::floor(dt / 1000)}, // Python 的 // 1000
        {"fee", numOr(s, "fee")},
        {"cover", ncmSized(album.value("picUrl"), 120)},
    };
}

QVariantMap ncmCloudTrack(const QJsonObject &item)
{
    if (item.value("simpleSong").isObject())
        return ncmTrack(item.value("simpleSong").toObject());
    return {
        {"id", numOr(item, "songId")},
        {"name", textOr(item, "songName", textOr(item, "fileName"))},
        {"artist", textOr(item, "artist")},
        {"album", textOr(item, "album")},
        {"dur", 0},
        {"fee", 0},
        {"cover", QString()},
    };
}

QVariantMap ncmPlaylistCard(const QJsonObject &p, const char *coverKey)
{
    const double count = numOr(p, "trackCount");
    return {
        {"id", numOr(p, "id")},
        {"name", textOr(p, "name")},
        {"cover", ncmSized(p.value(QLatin1String(coverKey)), 300)},
        {"count", count},
        // copywriter 是推荐理由（首页磁贴的信息气味），没有时退回曲目数——这个回退在 QML 里做
        {"reason", textOr(p, "copywriter")},
    };
}

QVariantMap ncmSearchArtist(const QJsonObject &a)
{
    QString cover = textOr(a, "picUrl");
    if (cover.isEmpty())
        cover = textOr(a, "img1v1Url");
    return {
        {"id", numOr(a, "id")},
        {"name", textOr(a, "name")},
        {"cover", ncmSized(cover, 300)},
        {"albums", numOr(a, "albumSize")},
    };
}

QVariantMap ncmSearchPlaylist(const QJsonObject &p)
{
    return {
        {"id", numOr(p, "id")},
        {"name", textOr(p, "name")},
        {"cover", ncmSized(p.value("coverImgUrl"), 300)},
        {"count", numOr(p, "trackCount")},
        {"creator", textOr(p.value("creator").toObject(), "nickname")},
    };
}

QVariantMap ncmSearchAlbum(const QJsonObject &a)
{
    const double ms = numOr(a, "publishTime");
    // Python: time.gmtime(ms/1000).tm_year
    const QDate date = QDateTime::fromSecsSinceEpoch(qlonglong(ms / 1000)).date();
    return {
        {"id", numOr(a, "id")},
        {"name", textOr(a, "name")},
        {"artist", textOr(a.value("artist").toObject(), "name")},
        {"cover", ncmSized(a.value("picUrl"), 300)},
        {"year", ms > 0 ? date.year() : 0},
    };
}

QVariantList ncmTracks(const QJsonArray &items)
{
    QVariantList out;
    for (const QJsonValue &v : items)
        out.append(ncmTrack(v.toObject()));
    return out;
}

QVariantList ncmParseLrc(const QString &lrc)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\](.*)"));
    QVariantList out;
    QRegularExpressionMatchIterator it = pattern.globalMatch(lrc);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString text = m.captured(3).trimmed();
        if (text.isEmpty())
            continue;
        const double t = m.captured(1).toDouble() * 60 + m.captured(2).toDouble();
        out.append(QVariantMap{
            {"t", QVariant(round(t * 100) / 100)}, // Python 的 round(..., 2)
            {"text", text},
        });
    }
    return out;
}

QString ncmFirstLine(const QString &text)
{
    return text.section(u'\n', 0, 0);
}

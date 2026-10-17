#include "normalize.h"
#include "json_util.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <cmath>

using namespace ncm;

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

// Python 的 x 真值：null、缺失都算「没有」，对象与非零数字算「有」
bool truthy(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return !v.isNull() && !v.isUndefined() && (!v.isDouble() || v.toDouble() != 0);
}
} // namespace

QString ncmSized(const QJsonValue &url, int px)
{
    const QString s = url.isString() ? url.toString() : QString();
    return s.isEmpty() ? QString() : s + QStringLiteral("?param=%1y%2").arg(px).arg(px);
}

QVariantMap ncmTrack(const QJsonObject &s, const QString &reason)
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
    // 别名/译名取第一项（新结构 alia，旧结构 alias，再退 tns/transNames）；quality 看有没有更高一档音频
    QString alias;
    for (const char *key : {"alia", "alias", "tns", "transNames"}) {
        const QJsonArray list = s.value(QLatin1String(key)).toArray();
        if (!list.isEmpty()) {
            alias = list.first().toString();
            break;
        }
    }
    const QString quality = truthy(s, "hr") || truthy(s, "hrMusic") ? QStringLiteral("hires")
        : truthy(s, "sq") || truthy(s, "sqMusic") ? QStringLiteral("lossless") : QString();

    return {
        {"id", numOr(s, "id")},
        {"name", textOr(s, "name")},
        {"artist", names.join(QStringLiteral(" / "))},
        {"album", textOr(album, "name")},
        {"dur", std::floor(dt / 1000)}, // Python 的 // 1000
        {"fee", numOr(s, "fee")},
        {"cover", ncmSized(album.value("picUrl"), 120)},
        {"alias", alias},
        {"quality", quality},
        {"mv", numOr(s, "mv") != 0 ? numOr(s, "mv") : numOr(s, "mvid")},
        // 推荐理由只有每日推荐带（data.recommendReasons 里 songId 对上的那条），其余为空
        {"reason", reason},
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
        {"alias", QString()},
        {"quality", QString()},
        {"mv", 0},
        {"reason", QString()},
    };
}

QVariantMap ncmHotComments(const QJsonObject &resp, double songId)
{
    // 实测热评在顶层 hotComments（不在 data 里），保持接口给的顺序，最多 10 条
    QVariantList items;
    for (const QJsonValue &v : take(resp.value("hotComments").toArray(), 10)) {
        const QJsonObject c = v.toObject();
        const QJsonObject user = c.value("user").toObject();
        items.append(QVariantMap{
            {"user", textOr(user, "nickname")},
            {"avatar", ncmSized(user.value("avatarUrl"), 96)},
            {"content", textOr(c, "content")},
            {"likes", c.value("likedCount").toDouble()},
            {"time", c.value("time").toDouble()},
        });
    }
    return {{"songId", songId}, {"items", items}};
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
    return ncmParseLrc(lrc, QString());
}

// 译文按取整到两位小数的时间戳对上同一句，没有为 ""
QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\](.*)"));
    // 同一时间戳取先出现的一句（Python 的 dict setdefault）；键是取整后的时间戳
    const auto stampKey = [](double t) { return QString::number(round(t * 100) / 100, 'f', 2); };
    QHash<QString, QString> tr;
    for (QRegularExpressionMatchIterator it = pattern.globalMatch(tlyric); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString line = m.captured(3).trimmed();
        if (line.isEmpty())
            continue;
        const double t = m.captured(1).toDouble() * 60 + m.captured(2).toDouble();
        if (!tr.contains(stampKey(t)))
            tr.insert(stampKey(t), line);
    }
    QVariantList out;
    QSet<QString> seen;
    for (QRegularExpressionMatchIterator it = pattern.globalMatch(lrc); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString text = m.captured(3).trimmed();
        if (text.isEmpty())
            continue;
        const double t = m.captured(1).toDouble() * 60 + m.captured(2).toDouble();
        if (seen.contains(stampKey(t)))   // dict 键唯一：同时间戳只留第一句
            continue;
        seen.insert(stampKey(t));
        out.append(QVariantMap{
            {"t", QVariant(round(t * 100) / 100)}, // Python 的 round(..., 2)
            {"text", text},
            {"tr", tr.value(stampKey(t))},
        });
    }
    return out;
}

QString ncmFirstLine(const QString &text)
{
    return text.section(u'\n', 0, 0);
}

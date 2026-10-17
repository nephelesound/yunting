#include "normalize.h"
#include "json_util.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QMap>
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
    QVariantList artistList; // 每位歌手各自可点：{ id, name }
    for (const QJsonValue &a : artists) {
        const QJsonObject o = a.toObject();
        names.append(o.value("name").toString());
        artistList.append(QVariantMap{{"id", o.value("id").toDouble()}, {"name", o.value("name").toString()}});
    }
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

    const QJsonObject firstArtist = artists.isEmpty() ? QJsonObject() : artists.first().toObject();
    return {
        {"id", numOr(s, "id")},
        {"name", textOr(s, "name")},
        {"artist", names.join(QStringLiteral(" / "))},
        {"artistId", firstArtist.value("id").toDouble()},
        {"artists", artistList},
        {"albumId", album.value("id").toDouble()},
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
        {"artistId", 0.0},
        {"artists", QVariantList{}},
        {"albumId", 0.0},
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
            {"userId", user.value("userId").toDouble()},
            {"avatar", ncmSized(user.value("avatarUrl"), 96)},
            {"content", textOr(c, "content")},
            {"likes", c.value("likedCount").toDouble()},
            {"time", c.value("time").toDouble()},
            {"id", c.value("commentId").toDouble()},
            {"liked", c.value("liked").toBool()},
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
        // copywriter 是推荐理由（首页磁贴的信息气味），没有时退回曲目数，这个回退在 QML 里做
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

QVariantMap ncmSearchMv(const QJsonObject &m)
{
    return {
        {"id", numOr(m, "id")},
        {"name", textOr(m, "name")},
        {"cover", ncmSized(m.value("cover"), 300)},
        {"artist", textOr(m, "artistName")},
        {"artistId", numOr(m, "artistId")},
        {"duration", std::floor(numOr(m, "duration") / 1000)},
        {"playCount", numOr(m, "playCount")},
    };
}

QVariantList ncmTracks(const QJsonArray &items)
{
    QVariantList out;
    for (const QJsonValue &v : items)
        out.append(ncmTrack(v.toObject()));
    return out;
}

// 译文按取整到两位小数的时间戳对上同一句，没有为 ""
namespace {
// LRC 正文 → 按时间排好的（时间, 句子），同一时间只留文件里先出现的一句（与 snapshot.py 的 stamps() 同一规则）。
// 一行开头可以挂多个时间戳：重复的句子（副歌）共用一行文字，[01:22.68][00:36.19]……，每个时间各算一句；
// 小数点也有写成冒号的（[00:50:50]）；逐字时间 <00:12.34> 从正文里去掉。
// [ar:]、[ti:] 这类标签行与网易的 JSON 署名行（{"t":0,"c":[…]}）不以时间戳开头，不算歌词。
QMap<double, QString> lrcStamps(const QString &text)
{
    static const QRegularExpression stamp(QStringLiteral("^\\[(\\d+):(\\d+)(?:[.:](\\d+))?\\]"));
    static const QRegularExpression wordTiming(QStringLiteral("<\\d+:\\d+(?:[.:]\\d+)?>"));
    QMap<double, QString> out;
    for (const QString &raw : text.split(u'\n')) {
        QString line = raw.trimmed();
        QList<double> times;
        for (QRegularExpressionMatch m = stamp.match(line); m.hasMatch(); m = stamp.match(line)) {
            const QString fraction = m.captured(3);
            times.append(m.captured(1).toDouble() * 60 + m.captured(2).toDouble()
                         + (fraction.isEmpty() ? 0 : (QStringLiteral("0.") + fraction).toDouble()));
            line.remove(0, m.capturedLength());
        }
        const QString body = line.remove(wordTiming).trimmed();
        if (body.isEmpty())
            continue;
        for (double t : std::as_const(times)) {
            const double key = round(t * 100) / 100; // Python 的 round(..., 2)
            if (!out.contains(key))
                out.insert(key, body);
        }
    }
    return out;
}
} // namespace

QVariantList ncmParseLrc(const QString &lrc, const QString &tlyric)
{
    const QMap<double, QString> tr = lrcStamps(tlyric);
    const QMap<double, QString> lines = lrcStamps(lrc);
    QVariantList out;
    for (auto it = lines.cbegin(); it != lines.cend(); ++it)
        out.append(QVariantMap{{"t", it.key()}, {"text", it.value()}, {"tr", tr.value(it.key())}});
    return out;
}

QString ncmLyricsToLrc(const QVariantList &lines)
{
    QString out;
    for (const QVariant &v : lines) {
        const QVariantMap line = v.toMap();
        const qint64 centis = qRound64(line.value("t").toDouble() * 100);
        const QString stamp = QStringLiteral("[%1:%2.%3]")
                                  .arg(centis / 6000, 2, 10, QLatin1Char('0'))
                                  .arg(centis / 100 % 60, 2, 10, QLatin1Char('0'))
                                  .arg(centis % 100, 2, 10, QLatin1Char('0'));
        out += stamp + line.value("text").toString() + u'\n';
        const QString tr = line.value("tr").toString();
        if (!tr.isEmpty())
            out += stamp + tr + u'\n';
    }
    return out;
}

QString ncmFirstLine(const QString &text)
{
    return text.section(u'\n', 0, 0);
}

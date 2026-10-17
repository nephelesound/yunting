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

// Python 的 x 真值：null、缺失、false、0 算「没有」，对象与非零数字算「有」
bool truthy(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0;
    return !v.isNull() && !v.isUndefined();
}

struct Playability {
    bool playable;
    QString blockedBy;
};

// privilege 是接口对「当前账号」的回答：st<0 已下架，cs 云盘，pl>0 能播。
// pl 为 0 时用 fee/payed 说明被什么挡住；没有 privilege（如相似歌曲、搜索旧接口）才退回 fee。
Playability trackPlayability(const QJsonObject &song, const QJsonObject &privilege)
{
    const QJsonObject p = privilege.isEmpty() ? song.value("privilege").toObject() : privilege;
    if (p.isEmpty()) {
        const double fee = numOr(song, "fee");
        if (fee == 4)
            return {false, QStringLiteral("paid")};
        if (fee == 1)
            return {false, QStringLiteral("vip")};
        return {true, QString()};
    }
    const double fee = p.contains("fee") ? numOr(p, "fee") : numOr(song, "fee");
    if (numOr(p, "st") < 0)
        return {false, QStringLiteral("unavailable")};
    if (truthy(p, "cs"))
        return {true, QString()};
    if (numOr(p, "pl") > 0)
        return {true, QString()};
    if (fee == 1)
        return {false, QStringLiteral("vip")};
    if (fee == 4 && !truthy(p, "payed"))
        return {false, QStringLiteral("paid")};
    return {false, QStringLiteral("unavailable")};
}
} // namespace

QString ncmSized(const QJsonValue &url, int px)
{
    const QString s = url.isString() ? url.toString() : QString();
    return s.isEmpty() ? QString() : s + QStringLiteral("?param=%1y%2").arg(px).arg(px);
}

QVariantMap ncmTrack(const QJsonObject &s, const QString &reason)
{
    return ncmTrack(s, QJsonObject{}, reason);
}

QVariantMap ncmTrack(const QJsonObject &s, const QJsonObject &privilege, const QString &reason)
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
    const Playability play = trackPlayability(s, privilege);
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
        {"playable", play.playable},
        {"blockedBy", play.blockedBy},
        // 推荐理由只有每日推荐带（data.recommendReasons 里 songId 对上的那条），其余为空
        {"reason", reason},
    };
}

QVariantMap ncmCloudTrack(const QJsonObject &item)
{
    // 云盘是账号自己的上传，接口不给 playable 时也必须能播
    if (item.value("simpleSong").isObject()) {
        QVariantMap track = ncmTrack(item.value("simpleSong").toObject());
        track.insert("playable", true);
        track.insert("blockedBy", QString());
        return track;
    }
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
        {"playable", true},
        {"blockedBy", QString()},
        {"reason", QString()},
    };
}

QVariantMap ncmProfile(const QJsonObject &p, int avatarPx, int backgroundPx)
{
    return {
        {"id", p.value("userId").toDouble()},
        {"name", p.value("nickname").toString()},
        {"avatar", ncmSized(p.value("avatarUrl"), avatarPx)},
        {"background", ncmSized(p.value("backgroundUrl"), backgroundPx)},
        {"signature", p.value("signature").toString()},
        {"followed", p.value("followed").toBool()},
        {"follows", p.value("follows").toDouble()},
        {"followeds", p.value("followeds").toDouble()},
        {"playlistCount", p.value("playlistCount").toDouble()},
        {"vipType", p.value("vipType").toDouble()},
        {"vipRights", p.value("vipRights").toObject().toVariantMap()},
        {"level", p.value("level").toDouble()},
        {"listenSongs", p.value("listenSongs").toDouble()},
        {"createTime", p.value("createTime").toDouble()},
        {"mutual", p.value("mutual").toBool()},
    };
}

QVariantMap ncmComment(const QJsonObject &c)
{
    const QJsonObject user = c.value("user").toObject();
    QVariantList replied;
    for (const QJsonValue &v : c.value("beReplied").toArray()) {
        const QJsonObject r = v.toObject();
        const QJsonObject ru = r.value("user").toObject();
        replied.append(QVariantMap{
            {"id", r.value("beRepliedCommentId").toDouble()},
            {"content", textOr(r, "content")},
            {"userId", ru.value("userId").toDouble()},
            {"user", textOr(ru, "nickname")},
        });
    }
    return {
        {"user", textOr(user, "nickname")},
        {"userId", user.value("userId").toDouble()},
        {"avatar", ncmSized(user.value("avatarUrl"), 96)},
        {"content", textOr(c, "content")},
        {"likes", c.value("likedCount").toDouble()},
        {"time", c.value("time").toDouble()},
        {"id", c.value("commentId").toDouble()},
        {"liked", c.value("liked").toBool()},
        // 楼中楼：parentId 指向父评论；beReplied 是被引用的那条，前端据此画「回复 @某人」
        {"parentId", c.value("parentCommentId").toDouble()},
        {"beReplied", replied},
        // 楼中楼回复数只在新版接口的 showFloorComment 里；没有这个对象（旧接口、无回复）归 0
        {"replyCount", numOr(c.value("showFloorComment").toObject(), "replyCount")},
    };
}

QVariantMap ncmHotComments(const QJsonObject &resp, double songId)
{
    // v2 接口把热评放在 data.comments，保持接口给的顺序，最多 10 条
    QVariantList items;
    const QJsonObject data = resp.value("data").toObject();
    for (const QJsonValue &v : take(data.value("comments").toArray(), 10))
        items.append(ncmComment(v.toObject()));
    return {{"songId", songId}, {"items", items}};
}

// 歌单/推荐卡片：id/name/cover/count/reason 供现有磁贴；description/playCount/creator/updateTime
// 是给详情/悬停信息预留的只读字段，不改变页面当前用法。
QVariantMap ncmPlaylistCard(const QJsonObject &p, const char *coverKey)
{
    const double count = numOr(p, "trackCount");
    const QJsonObject creator = p.value("creator").toObject();
    return {
        {"id", numOr(p, "id")},
        {"name", textOr(p, "name")},
        {"cover", ncmSized(p.value(QLatin1String(coverKey)), 300)},
        {"count", count},
        // 卡片只补只读展示字段；详情页需要的曲目仍由 playlistDetails 提供
        {"description", ncmFirstLine(p.value("description").toString())},
        {"playCount", p.value("playCount").toDouble()},
        {"creatorId", creator.value("userId").toDouble()},
        {"creatorName", textOr(creator, "nickname")},
        {"updateTime", p.value("updateTime").toDouble()},
        {"subscribed", p.value("subscribed").toBool()},
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

// 搜索结果里的歌单：保留 creator 字段兼容旧界面，同时补 creatorId/description/playCount。
QVariantMap ncmSearchPlaylist(const QJsonObject &p)
{
    const QJsonObject creator = p.value("creator").toObject();
    return {
        {"id", numOr(p, "id")},
        {"name", textOr(p, "name")},
        {"cover", ncmSized(p.value("coverImgUrl"), 300)},
        {"count", numOr(p, "trackCount")},
        {"creator", textOr(creator, "nickname")},
        {"creatorId", creator.value("userId").toDouble()},
        {"description", ncmFirstLine(p.value("description").toString())},
        {"playCount", p.value("playCount").toDouble()},
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

QHash<double, QJsonObject> ncmPrivilegeIndex(const QJsonArray &privileges)
{
    QHash<double, QJsonObject> out;
    out.reserve(privileges.size());
    for (const QJsonValue &v : privileges) {
        const QJsonObject p = v.toObject();
        if (!p.isEmpty())
            out.insert(p.value("id").toDouble(), p);
    }
    return out;
}

QVariantList ncmTracks(const QJsonArray &items, const QJsonArray &privileges)
{
    const QHash<double, QJsonObject> byId = ncmPrivilegeIndex(privileges);
    QVariantList out;
    out.reserve(items.size());
    for (const QJsonValue &v : items) {
        const QJsonObject song = v.toObject();
        out.append(ncmTrack(song, byId.value(song.value("id").toDouble())));
    }
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

// YRC 每句形如 [行起ms,行时长ms](字起ms,字长ms,0)字……；词文本紧跟在词标签之后，
// 英文的词间空格就落在前一个词里。{…} 开头的署名行没有时间戳，与 LRC 路径一样跳过。
QVariantList ncmParseYrc(const QString &yrc, const QString &tlyric)
{
    static const QRegularExpression lineStart(QStringLiteral("^\\[(\\d+),(\\d+)\\]"));
    static const QRegularExpression wordTag(QStringLiteral("\\((\\d+),(\\d+),(\\d+)\\)"));
    const QMap<double, QString> tr = lrcStamps(tlyric);
    QMap<double, QVariantMap> out;
    for (const QString &raw : yrc.split(u'\n')) {
        const QString line = raw.trimmed();
        const QRegularExpressionMatch head = lineStart.match(line);
        if (!head.hasMatch())
            continue;
        QVariantList words;
        QString text;
        bool haveWord = false;
        double wordStart = 0;
        double wordDuration = 0;
        qsizetype textFrom = 0;
        for (QRegularExpressionMatchIterator it = wordTag.globalMatch(line, head.capturedEnd());
             it.hasNext();) {
            const QRegularExpressionMatch tag = it.next();
            if (haveWord) {
                const QString word = line.mid(textFrom, tag.capturedStart() - textFrom);
                if (!word.isEmpty()) {
                    words.append(QVariantMap{{"t", wordStart}, {"d", wordDuration},
                                             {"text", word}});
                    text += word;
                }
            }
            haveWord = true;
            wordStart = tag.captured(1).toDouble() / 1000;
            wordDuration = tag.captured(2).toDouble() / 1000;
            textFrom = tag.capturedEnd();
        }
        if (haveWord) {
            const QString word = line.mid(textFrom);
            if (!word.isEmpty()) {
                words.append(QVariantMap{{"t", wordStart}, {"d", wordDuration}, {"text", word}});
                text += word;
            }
        }
        const QString body = text.trimmed();
        if (body.isEmpty())
            continue;
        const double t = round(head.captured(1).toDouble() / 1000 * 100) / 100;
        if (!out.contains(t))
            out.insert(t, QVariantMap{{"t", t}, {"text", body}, {"tr", tr.value(t)},
                                      {"words", words}});
    }
    QVariantList lines;
    for (auto it = out.cbegin(); it != out.cend(); ++it)
        lines.append(it.value());
    return lines;
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

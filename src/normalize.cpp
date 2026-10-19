#include "normalize.h"
#include "json_util.h"
#include "sdk/comment.h"
#include "sdk/track.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QMap>
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

} // namespace

QString ncmSized(const QJsonValue &url, int px)
{
    return sdk::sizedUrl(url.isString() ? url.toString() : QString(), px);
}

QVariantMap ncmCloudTrack(const QJsonObject &item)
{
    // 云盘是账号自己的上传，接口不给 playable 时也必须能播
    sdk::Track track;
    if (item.value("simpleSong").isObject()) {
        const auto parsed = sdk::parseTrack(item.value("simpleSong").toObject());
        if (parsed) {
            track = *parsed.value;
            QVariantMap map = sdk::trackToVariantMap(track);
            map.insert("playable", true);
            map.insert("blockedBy", QString());
            return map;
        }
    }
    track.id = static_cast<qint64>(numOr(item, "songId"));
    track.name = textOr(item, "songName", textOr(item, "fileName"));
    track.albumName = textOr(item, "album");
    QVariantMap map = sdk::trackToVariantMap(track);
    // 云盘条目只有单个 artist 文本，没有可点的歌手 id 列表
    map.insert("artist", textOr(item, "artist"));
    map.insert("artists", QVariantList{});
    map.insert("playable", true);
    map.insert("blockedBy", QString());
    return map;
}

QVariantMap ncmProfile(const QJsonObject &p, int avatarPx, int backgroundPx)
{
    return {
        {"id", p.value("userId").toDouble()},
        {"name", p.value("nickname").toString()},
        {"avatar", ncmSized(p.value("avatarUrl"), avatarPx)},
        {"background", ncmSized(p.value("backgroundUrl"), backgroundPx)},
        {"identityIcon", p.value("avatarDetail").toObject().value("identityIconUrl").toString()},
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

QVariantMap commentToVariantMap(const sdk::Comment &c)
{
    QVariantList replied;
    replied.reserve(c.beReplied.size());
    for (const sdk::RepliedComment &r : c.beReplied) {
        replied.append(QVariantMap{
            {"id", static_cast<double>(r.id)},
            {"content", r.content},
            {"userId", static_cast<double>(r.userId)},
            {"user", r.user},
        });
    }
    return {
        {"user", c.user},
        {"userId", static_cast<double>(c.userId)},
        {"avatar", ncmSized(QJsonValue(c.avatar), 96)},
        {"avatarPendant", c.pendant},
        {"identityIcon", c.identityIcon},
        {"content", c.content},
        {"likes", static_cast<double>(c.likes)},
        {"time", static_cast<double>(c.time)},
        {"id", static_cast<double>(c.id)},
        {"liked", c.liked},
        // 楼中楼：parentId 指向父评论；beReplied 是被引用的那条，前端据此画「回复 @某人」
        {"parentId", static_cast<double>(c.parentId)},
        {"beReplied", replied},
        // 楼中楼回复数只在新版接口的 showFloorComment 里；没有这个对象（旧接口、无回复）归 0
        {"replyCount", static_cast<double>(c.replyCount)},
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
    if (!out.isEmpty())
        return out;
    // 没有时间轴的歌词（网易的「非滚动歌词」）：逐行原样给出，t 记为 -1，界面不跟着高亮。
    // 署名行（{…}）与 [ar:] 这类标签行照样跳过
    for (const QString &raw : lrc.split(u'\n')) {
        const QString line = raw.trimmed();
        if (!line.isEmpty() && !line.startsWith(u'{') && !line.startsWith(u'['))
            out.append(QVariantMap{{"t", -1.0}, {"text", line}, {"tr", QString()}});
    }
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
        if (line.value("t").toDouble() < 0) {   // 没有时间轴的歌词：照原样一行一句
            out += line.value("text").toString() + u'\n';
            continue;
        }
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

NcmPlayedLevel ncmPlayedLevel(const QJsonObject &urlItem, const QString &requestedLevel)
{
    const QString returned = urlItem.value(QStringLiteral("level")).toString();
    const QString level = returned.isEmpty() ? requestedLevel : returned;
    if (level == QLatin1String("hires") || level == QLatin1String("lossless"))
        return {level, 999000};
    if (level == QLatin1String("higher"))
        return {level, 192000};
    if (level == QLatin1String("standard"))
        return {level, 128000};
    return {level, 320000};
}

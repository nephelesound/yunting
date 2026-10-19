#include "link_parser.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

namespace {

LinkKind kindFromName(const QString &name)
{
    const QString lowered = name.trimmed().toLower();
    if (lowered == QLatin1String("song"))
        return LinkKind::Song;
    if (lowered == QLatin1String("playlist"))
        return LinkKind::Playlist;
    if (lowered == QLatin1String("album"))
        return LinkKind::Album;
    if (lowered == QLatin1String("artist"))
        return LinkKind::Artist;
    if (lowered == QLatin1String("user"))
        return LinkKind::User;
    if (lowered == QLatin1String("mv"))
        return LinkKind::Mv;
    return LinkKind::None;
}

OpenLink makeLink(LinkKind kind, qint64 id)
{
    OpenLink link;
    link.kind = kind;
    link.id = id;
    return link;
}

bool positiveId(const QString &text, qint64 &id)
{
    bool ok = false;
    id = text.toLongLong(&ok);
    return ok && id > 0;
}

// "/m/artist/home" 里的 m 是移动端前缀；取路径中第一个已知类型。
LinkKind kindFromSegments(const QStringList &segments)
{
    for (const QString &segment : segments) {
        if (segment.compare(QLatin1String("m"), Qt::CaseInsensitive) == 0)
            continue;
        const LinkKind kind = kindFromName(segment);
        if (kind != LinkKind::None)
            return kind;
    }
    return LinkKind::None;
}

bool idFromQuery(const QString &query, qint64 &id)
{
    const QUrlQuery parsed(query);
    return parsed.hasQueryItem(QStringLiteral("id"))
        && positiveId(parsed.queryItemValue(QStringLiteral("id")), id);
}

// base64 的 JSON 对象：URL 安全与标准两种字母表都认（先统一成标准的），缺的 = 不要紧。
std::optional<QJsonObject> base64Json(QString payload)
{
    payload.replace(u'-', u'+').replace(u'_', u'/');
    const auto decoded = QByteArray::fromBase64Encoding(payload.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return std::nullopt;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(*decoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    return document.object();
}

// orpheus://<base64(JSON)>：先还原百分号编码。末尾的 / 可能是浏览器补的，也可能就是标准 base64 的最后一个字：
// 原样解不开再去掉它试一次
OpenLink parseOrpheusPayload(const QString &text)
{
    const QString prefix = QStringLiteral("orpheus://");
    if (!text.startsWith(prefix, Qt::CaseInsensitive))
        return {};

    const QString payload = QUrl::fromPercentEncoding(text.mid(prefix.size()).toUtf8());
    auto parsed = base64Json(payload);
    if (!parsed && payload.endsWith(u'/'))
        parsed = base64Json(payload.chopped(1));
    if (!parsed)
        return {};

    const QJsonObject object = *parsed;
    const LinkKind kind = kindFromName(object.value(QStringLiteral("type")).toString());
    if (kind == LinkKind::None)
        return {};

    const QJsonValue idValue = object.value(QStringLiteral("id"));
    qint64 id = 0;
    if (idValue.isString()) {
        if (!positiveId(idValue.toString(), id))
            return {};
    } else if (idValue.isDouble()) {
        id = idValue.toInteger();
        if (id <= 0)
            return {};
    } else {
        return {};
    }
    return makeLink(kind, id);
}

// orpheus://nm/artist/home?id=228547&type=artist：客户端内部的路径式链接。
OpenLink parseOrpheusPath(const QString &text)
{
    const QString prefix = QStringLiteral("orpheus://nm/");
    if (!text.startsWith(prefix, Qt::CaseInsensitive))
        return {};

    const QString body = text.mid(prefix.size());
    const qsizetype question = body.indexOf(u'?');
    const QString path = question < 0 ? body : body.left(question);
    const QString query = question < 0 ? QString() : body.mid(question + 1);

    const LinkKind kind = kindFromSegments(path.split(u'/', Qt::SkipEmptyParts));
    qint64 id = 0;
    if (kind == LinkKind::None || !idFromQuery(query, id))
        return {};
    return makeLink(kind, id);
}

bool neteaseHost(const QString &host)
{
    const QString lowered = host.toLower();
    return lowered == QLatin1String("music.163.com")
        || lowered == QLatin1String("www.music.163.com")
        || lowered == QLatin1String("y.music.163.com");
}

// 分享链接：网页路径或 #/ 片段里的路径，id 在主查询或片段查询里。
OpenLink parseShareUrl(const QString &text)
{
    const QUrl url(text);
    if (!url.isValid())
        return {};
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return {};
    if (!neteaseHost(url.host()))
        return {};

    // 先看正常的路径与查询；解不出再看 #/ 片段里的（旧版网页把路由放在片段里）。
    // 顺序不能反：song?id=1#comments 这类片段只是页内锚点
    const auto linkFrom = [](const QString &path, const QString &query) {
        const LinkKind kind = kindFromSegments(path.split(u'/', Qt::SkipEmptyParts));
        qint64 id = 0;
        return kind != LinkKind::None && idFromQuery(query, id) ? makeLink(kind, id) : OpenLink{};
    };
    if (const OpenLink link = linkFrom(url.path(), url.query()); link.valid())
        return link;
    const QString fragment = url.fragment();
    const qsizetype question = fragment.indexOf(u'?');
    return question < 0 ? OpenLink{} : linkFrom(fragment.left(question), fragment.mid(question + 1));
}

} // namespace

OpenLink parseOpenLink(const QString &text)
{
    if (text.startsWith(QStringLiteral("orpheus://"), Qt::CaseInsensitive)) {
        // 路径式链接先认；它不是 base64（"nm/" 不会出现在 JSON 的 base64 开头）。
        if (text.startsWith(QStringLiteral("orpheus://nm/"), Qt::CaseInsensitive))
            return parseOrpheusPath(text);
        return parseOrpheusPayload(text);
    }
    return parseShareUrl(text);
}

FoundLink findLink(const QString &text)
{
    // 链接在空白、引号、中文标点或括号处结束；末尾的英文句读不算链接
    static const QRegularExpression candidate(
        QStringLiteral("(?:orpheus|https?)://[^\\s<>\"'`\\[\\]{}，。；、！？（）()「」『』【】]+"),
        QRegularExpression::CaseInsensitiveOption);
    auto matches = candidate.globalMatch(text);
    while (matches.hasNext()) {
        QString token = matches.next().captured();
        while (!token.isEmpty() && QStringLiteral(".,;:!?").contains(token.back()))
            token.chop(1);
        FoundLink found;
        found.link = parseOpenLink(token);
        if (found.link.valid())
            return found;
        const QUrl url(token);
        const QString scheme = url.scheme().toLower();
        if ((scheme == QLatin1String("http") || scheme == QLatin1String("https"))
            && url.host().toLower() == QLatin1String("163cn.tv") && url.path().size() > 1) {
            found.shortUrl = url;
            return found;
        }
    }
    return {};
}

QString linkKindName(LinkKind kind)
{
    switch (kind) {
    case LinkKind::Song:
        return QStringLiteral("song");
    case LinkKind::Playlist:
        return QStringLiteral("playlist");
    case LinkKind::Album:
        return QStringLiteral("album");
    case LinkKind::Artist:
        return QStringLiteral("artist");
    case LinkKind::User:
        return QStringLiteral("user");
    case LinkKind::Mv:
        return QStringLiteral("mv");
    case LinkKind::None:
        break;
    }
    return {};
}

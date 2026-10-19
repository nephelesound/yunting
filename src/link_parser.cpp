#include "link_parser.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

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

// orpheus://<URL-safe base64(JSON)>：末尾的 / 与百分号编码都先还原。
OpenLink parseOrpheusPayload(const QString &text)
{
    const QString prefix = QStringLiteral("orpheus://");
    if (!text.startsWith(prefix, Qt::CaseInsensitive))
        return {};

    QString payload = text.mid(prefix.size());
    if (payload.endsWith(u'/'))
        payload.chop(1);
    payload = QUrl::fromPercentEncoding(payload.toUtf8());

    const QByteArray decoded = QByteArray::fromBase64(
        payload.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(decoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};

    const QJsonObject object = document.object();
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

    QString path = url.path();
    QString query = url.query();
    const QString fragment = url.fragment();
    if (!fragment.isEmpty()) {
        const qsizetype question = fragment.indexOf(u'?');
        const QString fragmentPath = question < 0 ? fragment : fragment.left(question);
        if (!fragmentPath.isEmpty()) {
            path = fragmentPath;
            query = question < 0 ? QString() : fragment.mid(question + 1);
        }
    }

    const LinkKind kind = kindFromSegments(path.split(u'/', Qt::SkipEmptyParts));
    qint64 id = 0;
    if (kind == LinkKind::None || !idFromQuery(query, id))
        return {};
    return makeLink(kind, id);
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

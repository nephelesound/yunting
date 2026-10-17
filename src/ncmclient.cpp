#include "ncmclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNet, "ncm.net", QtWarningMsg)
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {
constexpr auto ApiBase = "https://music.163.com/";
constexpr auto Referer = "https://music.163.com";
constexpr auto UserAgent = "Mozilla/5.0 (X11; Linux x86_64) Chrome/140.0";
} // namespace

NcmClient::NcmClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
    m_jarPath = qEnvironmentVariable("XDG_DATA_HOME");
    if (m_jarPath.isEmpty())
        m_jarPath = QDir::home().filePath(".local/share");
    m_jarPath += "/ncm-player/cookies.json";
    loadJar();
}

int NcmClient::importGtk4Cookies(const QString &targetPath)
{
    QFile src(QDir::home().filePath(".local/share/netease-cloud-music-gtk4/cookies.json"));
    if (!src.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot read gtk4 cookie file:" << src.errorString();
        return -1;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(src.readAll());
    if (!doc.isArray()) {
        qWarning() << "gtk4 cookie file is not a list";
        return -1;
    }
    // 与 snapshot.py 相同的挑选规则：MUSIC_U、__csrf、NMTID；同名取最后一条
    QMap<QString, QString> jar;
    for (const QJsonValue &v : doc.array()) {
        const QString raw = v.toObject().value("raw_cookie").toString();
        const QString pair = raw.section(u';', 0, 0);
        const QString name = pair.section(u'=', 0, 0).trimmed();
        if (name == "MUSIC_U" || name == "__csrf" || name == "NMTID")
            jar.insert(name, pair.mid(name.length() + 1));
    }
    QFile dst(targetPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "cannot write cookie jar:" << dst.errorString();
        return -1;
    }
    dst.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // 登录凭据：只给本人读写
    QJsonArray out;
    for (auto it = jar.constBegin(); it != jar.constEnd(); ++it)
        out.append(QJsonObject{{it.key(), it.value()}});
    dst.write(QJsonDocument(out).toJson());
    return static_cast<int>(jar.size());
}

void NcmClient::loadJar()
{
    QFile f(m_jarPath);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            m_jar.insert(it.key(), it.value().toString());
    }
}

void NcmClient::saveJar() const
{
    QDir().mkpath(QFileInfo(m_jarPath).path());
    QJsonArray out;
    for (auto it = m_jar.constBegin(); it != m_jar.constEnd(); ++it)
        out.append(QJsonObject{{it.key(), it.value()}});
    QFile f(m_jarPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // 登录凭据：只给本人读写
        f.write(QJsonDocument(out).toJson());
    }
}

// 响应里的 Set-Cookie 只保留我们用的三个（登录成功、令牌续期都走这里）；删除型 cookie（空值）忽略
void NcmClient::keepSetCookies(QNetworkReply *reply)
{
    const auto cookies = reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
    bool changed = false;
    for (const QNetworkCookie &c : cookies) {
        const QString name = QString::fromLatin1(c.name());
        if ((name == "MUSIC_U" || name == "__csrf" || name == "NMTID") && !c.value().isEmpty()
            && m_jar.value(name) != QString::fromLatin1(c.value())) {
            // 匿名请求也会下发 NMTID；只有已登录时才换，避免把匿名设备号写进登录会话
            if (name == "NMTID" && hasMusicU())
                continue;
            m_jar.insert(name, QString::fromLatin1(c.value()));
            changed = true;
        }
    }
    if (changed)
        saveJar();
}

bool NcmClient::hasMusicU() const
{
    return m_jar.contains("MUSIC_U") && !m_jar.value("MUSIC_U").isEmpty();
}

void NcmClient::adoptCookies(const QJsonDocument &jarDoc)
{
    for (const QJsonValue &v : jarDoc.array()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            m_jar.insert(it.key(), it.value().toString());
    }
    saveJar();
}

QString NcmClient::cookieHeader(bool personal) const
{
    if (!personal)
        return {};
    QString header;
    for (const char *name : {"MUSIC_U", "__csrf", "NMTID"}) {
        const QString value = m_jar.value(QLatin1String(name));
        if (!value.isEmpty())
            header += (header.isEmpty() ? QString() : QLatin1String("; "))
                + QLatin1String(name) + u'=' + value;
    }
    return header;
}

void NcmClient::get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback)
{
    QUrl url(QString::fromLatin1(ApiBase) + path);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setRawHeader("Referer", Referer);
    req.setRawHeader("User-Agent", UserAgent);
    req.setTransferTimeout(15 * 1000);
    // 关掉 QNetworkAccessManager 的自动 cookie：否则匿名请求收到的 Set-Cookie（MUSIC_A 等匿名令牌）
    // 会混进之后的个人请求，网易就按匿名处理、回 301。cookie 只由 m_jar 管理（见 keepSetCookies）
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    const QString cookie = cookieHeader(personal);
    if (!cookie.isEmpty()) {
        req.setRawHeader("Cookie", cookie.toUtf8());
    }
    // 请求计数用（--verbose）：只记路径，不记 query（可能含搜索词）和请求头
    qCDebug(lcNet).noquote() << "GET" << url.path();
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback] {
        reply->deleteLater();
        QJsonDocument doc;
        bool ok = false;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError parseError;
            doc = QJsonDocument::fromJson(reply->readAll(), &parseError);
            ok = parseError.error == QJsonParseError::NoError;
            if (!ok)
                qWarning() << "bad JSON from" << reply->url().toString(QUrl::RemoveQuery);
        } else {
            // 只记错误种类与 URL（不含 query，可能带搜索词），请求头一律不打
            qWarning() << "request failed:" << reply->error()
                       << reply->url().toString(QUrl::RemoveQuery);
        }
        keepSetCookies(reply);
        if (callback)
            callback(doc, ok);
    });
}

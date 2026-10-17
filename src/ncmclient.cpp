#include "ncmclient.h"

#include "cookievault.h"
#include "crypto.h"
#include "json_util.h"
#include "perf.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(lcNet, "yunting.net", QtWarningMsg)

namespace {
constexpr auto Referer = "https://music.163.com";
constexpr auto UserAgent = "Mozilla/5.0 (X11; Linux x86_64) Chrome/140.0";

// jar（MUSIC_U / __csrf / NMTID）与钥匙串条目之间的唯一序列化形式：紧凑 JSON 对象
QByteArray jarToJson(const QMap<QString, QString> &jar)
{
    QJsonObject o;
    for (auto it = jar.constBegin(); it != jar.constEnd(); ++it)
        o.insert(it.key(), it.value());
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QMap<QString, QString> jarFromJson(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QMap<QString, QString> jar;
    if (doc.isObject()) {
        const QJsonObject o = doc.object();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            jar.insert(it.key(), it.value().toString());
    }
    return jar;
}

// 旧版 NcmClient 写的文件是 [{name: value}, …] 数组；迁移时兼容读取
QMap<QString, QString> jarFromArrayFile(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QMap<QString, QString> jar;
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            jar.insert(it.key(), it.value().toString());
    }
    return jar;
}
} // namespace

NcmClient::NcmClient(QObject *parent, bool loadCookies)
    : QObject(parent), m_vault(new CookieVault(this)), m_nam(new QNetworkAccessManager(this))
{
    if (!loadCookies) {
        m_ready = true;
        m_persist = false;
        return;
    }
    // legacy 明文文件 → 钥匙串（写成功才删文件），完成后才读最终存储；
    // 读到 jar 即 ready()，main 在那之后才允许 Store 发请求
    m_vault->migrate();
    connect(m_vault, &CookieVault::loaded, this, [this](const QByteArray &data) {
        // 读出之前 jar 只可能因登录或导入而非空（匿名下发的 cookie 在那之前不收）：那份更新，留着
        if (m_jar.isEmpty()) {
            m_jar = jarFromJson(data);
            if (m_jar.isEmpty())
                m_jar = jarFromArrayFile(data);   // 旧版 NcmClient 写的是数组形状
        }
        m_ready = true;
        emit ready();
    });
    // 读存储失败（不是「没有条目」）：不认空 jar，也不进入 ready。
    // 之后 keepSetCookies / persistJar 都因 !m_ready 跳过，匿名 NMTID 不会覆盖真凭据。
    connect(m_vault, &CookieVault::loadFailed, this, [] {
        qCWarning(lcNet) << "cannot read cookie store; credentials stay untouched this run";
    });
    m_vault->load();
}

QMap<QString, QString> NcmClient::importGtk4Cookies()
{
    QFile src(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
              + "/netease-cloud-music-gtk4/cookies.json");
    if (!src.open(QIODevice::ReadOnly)) {
        qWarning() << "cannot read gtk4 cookie file:" << src.errorString();
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(src.readAll());
    if (!doc.isArray()) {
        qWarning() << "gtk4 cookie file is not a list";
        return {};
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
    return jar;
}

// 一次性导入：读 gtk4 文件 → 整包替换 jar → 存储写完才返回（main 的 --import-gtk4-cookies
// 打印完个数就退出，必须等写落地）
int NcmClient::importGtk4()
{
    const QMap<QString, QString> jar = importGtk4Cookies();
    if (jar.isEmpty())
        return -1;
    m_jar = jar;

    QEventLoop loop;
    bool stored = false;
    connect(m_vault, &CookieVault::saved, &loop, [&](bool) {
        stored = true;
        loop.quit();
    });
    m_vault->save(jarToJson(m_jar));
    if (!stored)   // 后备文件是同步发 saved 的；只有钥匙串路径才需要 exec
        loop.exec();
    return m_jar.size();
}

// jar 变了就存一份进 vault（钥匙串或后备文件）。
// 存储还没成功读出时绝不落盘：那会把空 jar 写在仍然完好的登录凭据上。
void NcmClient::persistJar()
{
    if (!m_ready || !m_persist)
        return;
    m_vault->save(jarToJson(m_jar));
}

// 响应里的 Set-Cookie 只保留我们用的三个（登录成功、令牌续期都走这里）；删除型 cookie（空值）忽略
void NcmClient::keepSetCookies(QNetworkReply *reply)
{
    // 存储还没读出（钥匙串慢、在等用户解锁）时发出的请求都是匿名的：这时落盘会排在读之后，
    // 用匿名 jar 盖掉存储里的登录凭据
    if (!m_ready)
        return;
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
        persistJar();
}

QString NcmClient::fallbackPath() const
{
    return m_vault->fallbackPath();
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
    persistJar();
}

void NcmClient::clearAuthCookies()
{
    for (const char *name : {"MUSIC_U", "__csrf", "NMTID"})
        m_jar.remove(QLatin1String(name));
    persistJar();
}

bool NcmClient::flushCredentials(int timeoutMs)
{
    return m_vault->waitForPending(timeoutMs);
}

// POST 的共同部分：写请求不重试，重复发送会产生副作用（红心、打卡、改歌单）。
void NcmClient::postRaw(const QUrl &url, const QByteArray &body, const QString &cookie,
                        ResponseCallback callback, bool keepCookies)
{
    QNetworkRequest req(url);
    req.setRawHeader("Referer", Referer);
    req.setRawHeader("User-Agent", UserAgent);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(15 * 1000);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    if (!cookie.isEmpty())
        req.setRawHeader("Cookie", cookie.toUtf8());

    qCDebug(lcNet).noquote() << "POST" << url.path();
    QElapsedTimer timer;
    timer.start();
    QNetworkReply *reply = m_nam->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, keepCookies, timer, url] {
        reply->deleteLater();
        Response response;
        response.networkFailure = isNetworkError(*reply);
        if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError parseError;
            const QByteArray raw = reply->readAll();
            response.doc = QJsonDocument::fromJson(raw, &parseError);
            response.ok = parseError.error == QJsonParseError::NoError;
            if (!response.ok)
                qWarning() << "bad JSON from" << reply->url().toString(QUrl::RemoveQuery)
                           << raw.left(200);
        } else {
            qWarning() << "write failed:" << reply->error()
                       << reply->url().toString(QUrl::RemoveQuery);
        }
        qCDebug(lcPerf).nospace() << "http method=POST path=" << url.path()
                                  << " status=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
                                  << " ms=" << timer.elapsed() << " network=" << response.networkFailure;
        if (keepCookies)
            keepSetCookies(reply);
        emit requestFinished(response.ok, response.networkFailure);
        if (callback && callbackContextAlive())
            callback(response);
    });
}

void NcmClient::postWeapi(const QString &path, const QJsonObject &payload, bool personal,
                          GetCallback callback, bool keepCookies)
{
    postWeapi(path, payload, personal,
              [callback](const Response &response) {
        if (callback)
            callback(response.doc, response.ok);
    }, keepCookies);
}

void NcmClient::postWeapi(const QString &path, const QJsonObject &payload, bool personal,
                          ResponseCallback callback, bool keepCookies)
{
    const QString csrf = m_jar.value(QStringLiteral("__csrf"));
    QJsonObject body = payload;
    if (!csrf.isEmpty())
        body.insert(QStringLiteral("csrf_token"), csrf);

    QUrl url(m_apiBase + QStringLiteral("weapi/") + path);
    if (!csrf.isEmpty())
        url.setQuery(ncm::query({{"csrf_token", csrf}}));

    // 写接口除了登录 cookie 还要带客户端标识：缺 os/appver 时服务器回 -460「网络环境风险」。
    QString cookie = cookieHeader(personal);
    cookie += (cookie.isEmpty() ? QString() : QStringLiteral("; "))
        + QStringLiteral("os=pc; appver=3.1.17; osver=10");

    const QByteArray form = ncm::weapiForm(body).toLatin1();
    if (form.isEmpty()) {
        // 加密失败属于本机错误：不发请求，交给调用方显示可读原因
        if (callback && callbackContextAlive())
            callback(Response{});
        return;
    }
    postRaw(url, form, cookie, callback, keepCookies);
}

void NcmClient::postForm(const QString &path, const QUrlQuery &form, bool personal,
                         GetCallback callback)
{
    // 鉴权上下文是 Web cookie（MUSIC_U）：只能走 /api 明文或 /weapi，统一用 os=pc，
    // 不混用 macOS/Android 客户端专有标识或 /eapi 接口。
    QString cookie = cookieHeader(personal);
    if (personal)
        cookie += (cookie.isEmpty() ? QString() : QStringLiteral("; "))
            + QStringLiteral("os=pc; appver=3.1.17; osver=10");
    postRaw(QUrl(m_apiBase + path), ncm::formEncoded(form), cookie,
            [callback](const Response &response) {
        if (callback)
            callback(response.doc, response.ok);
    });
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

void NcmClient::postClientLog(const QByteArray &multipart, const QString &boundary,
                                 const QString &cookie, GetCallback callback)
{
    postClientLogAttempt(multipart, boundary, cookie, callback, 0);
}

void NcmClient::postClientLogAttempt(const QByteArray &multipart, const QString &boundary,
                                        const QString &cookie, GetCallback callback, int attempt)
{
    QNetworkRequest request(QUrl(QStringLiteral(
        "https://clientlog3.music.163.com/api/clientlog/encrypt/upload?multiupload=true")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("multipart/form-data; boundary=%1").arg(boundary));
    request.setRawHeader("Referer", "https://music.163.com/di");
    request.setRawHeader("User-Agent",
                         "Mozilla/5.0 (Windows NT 10.0; WOW64) AppleWebKit/537.36 "
                         "(KHTML, like Gecko) Safari/537.36 Chrome/91.0.4472.164 "
                         "NeteaseMusicDesktop/3.1.37");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.8");
    request.setRawHeader("Cookie", cookie.toUtf8());
    QNetworkReply *reply = m_nam->post(request, multipart);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, multipart, boundary,
                                                       cookie, attempt] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool retryable = attempt < 2
            && (reply->error() == QNetworkReply::NoError ? status >= 500
                                                         : isNetworkError(*reply));
        if (retryable && callbackContextAlive()) {
            reply->deleteLater();
            QTimer::singleShot(200 * (attempt + 1), this, [this, multipart, boundary, cookie,
                                                             callback, attempt] {
                postClientLogAttempt(multipart, boundary, cookie, callback, attempt + 1);
            });
            return;
        }
        const QByteArray bytes = reply->readAll();
        const QJsonDocument doc = QJsonDocument::fromJson(bytes);
        const bool ok = reply->error() == QNetworkReply::NoError && doc.isObject();
        if (!ok)
            qWarning() << "clientlog upload failed:" << reply->errorString()
                       << status << bytes.left(300);
        if (callback && callbackContextAlive())
            callback(ok ? doc : QJsonDocument(), ok);
        reply->deleteLater();
    });
}

void NcmClient::setCallbackContext(QObject *context)
{
    m_callbackContext = context;
    m_contextSet = context != nullptr;
}

bool NcmClient::callbackContextAlive() const
{
    return !m_contextSet || !m_callbackContext.isNull();
}

void NcmClient::setApiBase(const QString &base)
{
    m_apiBase = base;
}

NcmClient::Failure NcmClient::failureOf(const Response &response)
{
    if (response.networkFailure)
        return Failure::Offline;
    if (!response.ok)
        return Failure::Rejected;
    switch (response.doc.object().value("code").toInt()) {
    case 200:
        return Failure::None;
    case 301:
        return Failure::SessionExpired;
    case -460:
    case 405:
        return Failure::RateLimited;
    default:
        return Failure::Rejected;
    }
}

namespace {
// 重试节奏：最多 3 次，间隔 0.5 / 1 / 2 秒
constexpr int MaxRetries = 3;
const int RetryDelaysMs[] = {500, 1000, 2000};
} // namespace

bool NcmClient::isNetworkError(const QNetworkReply &reply)
{
    // 「网络原因」= 没拿到 HTTP 状态：连接被拒/无路由、DNS 失败、超时。
    // 服务器明确回的任何状态码（含 5xx）都说明网络通。
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 0
        && reply.error() != QNetworkReply::NoError;
}

void NcmClient::get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback)
{
    startGet(path, query, personal, 0, false, std::move(callback));
}

void NcmClient::startGet(const QString &path, const QUrlQuery &query, bool personal,
                         int attempt, bool retrying, GetCallback callback)
{
    QUrl url(m_apiBase + path);
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
    qCDebug(lcNet).noquote() << "GET" << url.path() << (retrying ? QStringLiteral("(retry %1)").arg(attempt) : QStringLiteral(""));
    QElapsedTimer timer;
    timer.start();
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, path, query, personal, attempt, timer]
    {
        reply->deleteLater();
        const bool networkError = isNetworkError(*reply);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 重试：网络原因（无 HTTP 状态）或服务器端 5xx；4xx 与业务码不重试
        const bool retryable = networkError || status >= 500;
        if (retryable && attempt < MaxRetries) {
            QTimer::singleShot(RetryDelaysMs[attempt], this, [this, path, query, personal, attempt, callback] {
                startGet(path, query, personal, attempt + 1, true, std::move(callback));
            });
            return;
        }
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
        emit requestFinished(ok, networkError);
        if (callback)
            callback(doc, ok);
    });
}

#include "session.h"
#include "ncmclient.h"
#include "perf.h"
#include "store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr int PollMs = 2000;

// 803 的 cookie 可能是 "MUSIC_U=…; __csrf=…" 这一种字符串（新版接口），
// 也可能是 Set-Cookie 风格的属性串；只取我们需要的三个键。
QMap<QString, QString> authCookies(const QString &raw)
{
    QMap<QString, QString> cookies;
    for (const QString &part : raw.split(u';')) {
        const QString pair = part.trimmed();
        const QString name = pair.section(u'=', 0, 0).trimmed();
        if (name == QLatin1String("MUSIC_U") || name == QLatin1String("__csrf")
            || name == QLatin1String("NMTID")) {
            cookies.insert(name, pair.mid(name.length() + 1));
        }
    }
    return cookies;
}

void appendCookies(QJsonArray &jar, const QString &raw)
{
    const QMap<QString, QString> cookies = authCookies(raw);
    for (auto it = cookies.cbegin(); it != cookies.cend(); ++it)
        jar.append(QJsonObject{{it.key(), it.value()}});
}

// 网页端复制的 Cookie：整段 Cookie 头，或只粘 MUSIC_U 的值（浏览器里叫 MUSIC_U 的那一格）。
// 认不出 MUSIC_U 时返回空表。
QMap<QString, QString> credentialsFrom(const QString &raw)
{
    if (!raw.contains(u'=')) {
        const QString token = raw.trimmed();
        if (token.isEmpty())
            return {};
        return {{QStringLiteral("MUSIC_U"), token}};
    }
    const QMap<QString, QString> cookies = authCookies(raw);
    return cookies.value(QStringLiteral("MUSIC_U")).isEmpty() ? QMap<QString, QString>() : cookies;
}
} // namespace

Session::Session(NcmClient *client, Store *store, QObject *parent)
    : QObject(parent), m_client(client), m_store(store)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(PollMs);
    connect(m_timer, &QTimer::timeout, this, &Session::poll);
    // Store 发现 301/无 profile 时只尝试一次刷新；成功/失败都会让 Store 的后续 refresh
    // 给出最终结论，不在信号边上递归刷新。
    connect(m_store, &Store::sessionExpiredChanged, this, [this] {
        if (m_store->sessionExpired() && !m_refreshing)
            refreshSession();
    });
}

void Session::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    qCDebug(lcPerf) << "login state" << state;
    emit stateChanged();
}

void Session::start()
{
    const quint64 generation = ++m_generation;
    m_polling = false;
    m_timer->stop();
    m_unikey.clear();
    // 先清码再通知：否则 QML 会短暂绑定到上一次的二维码
    m_qrContent.clear();
    emit qrContentChanged();
    setState(QStringLiteral("waiting"));

    // 网易当前要求 type=3（NeteaseCloudMusicApi 维护分支、SPlayer、YesPlayMusic 用的 @neteaseapireborn/api
    // 都是这一个）；缺了这个参数会回 HTTP 200 + code 400「参数错误」。
    m_client->get("api/login/qrcode/unikey", QUrlQuery{{QStringLiteral("type"), QStringLiteral("3")}}, false,
                  [this, generation](const QJsonDocument &doc, bool ok) {
        if (generation != m_generation)
            return;   // 期间 cancel() 或重新 start()：这份 unikey 已经过时
        const QString unikey = ok ? doc.object().value("unikey").toString() : QString();
        if (unikey.isEmpty()) {
            qWarning() << "cannot get login qrcode";
            // 不能停在 idle：对话框会把「没有二维码」当成加载中，永远转圈。
            setState(QStringLiteral("error"));
            return;
        }
        m_unikey = unikey;
        m_qrContent = QStringLiteral("https://music.163.com/login?codekey=") + unikey;
        emit qrContentChanged();
        m_timer->start();
        poll();
    });
}

void Session::cancel()
{
    ++m_generation;
    ++m_cookieGeneration;   // 关掉对话框时在途的 Cookie 校验也作废：回包不能再登录、报错或重开扫码
    m_polling = false;
    m_timer->stop();
    m_unikey.clear();
    setState(QStringLiteral("idle"));
}

void Session::refreshSession()
{
    if (m_refreshing)
        return;
    if (!m_client->hasMusicU()) {
        emit refreshFailed(QStringLiteral("请先登录"));
        return;
    }
    m_refreshing = true;
    const quint64 generation = ++m_refreshGeneration;
    // 只有扫码登录的 MUSIC_U 能这样续期：网页端复制的 MUSIC_U 在这里一律回 301（issue #3），
    // 那种会话过期了只能重新登录。新 cookie 经 Set-Cookie 下发，回调之前 NcmClient 已经存好。
    m_client->postWeapi(QStringLiteral("login/token/refresh"), {}, true,
                        [this, generation](const QJsonDocument &doc, bool ok) {
        if (generation != m_refreshGeneration)
            return;
        m_refreshing = false;
        const int code = ok ? doc.object().value(QStringLiteral("code")).toInt() : 0;
        if (!ok || code != 200 || !m_client->hasMusicU()) {
            emit refreshFailed(QStringLiteral("登录状态刷新失败，请重新登录"));
            return;
        }
        m_store->refresh();
        emit refreshed();
    });
}

void Session::loginWithCookie(const QString &raw)
{
    const QMap<QString, QString> credentials = credentialsFrom(raw);
    if (credentials.isEmpty()) {
        emit loginFailed(QStringLiteral("没有识别到 MUSIC_U：请粘贴整段 Cookie，或 MUSIC_U 的值"));
        return;
    }
    // 正在等扫码时用 Cookie 登录：那张二维码已经无关，停止轮询
    ++m_generation;
    m_polling = false;
    m_timer->stop();
    m_unikey.clear();
    if (!m_qrContent.isEmpty()) {
        m_qrContent.clear();
        emit qrContentChanged();
    }

    // 用账号接口校验：拿到 profile 才算这个 MUSIC_U 有效。不能用 login/token/refresh，
    // 浏览器里复制的有效 MUSIC_U 也会被它回 301（issue #3）。
    // 校验过了才写进 jar：失败、断网或中途关掉对话框时，原来的登录状态原样留着。
    const quint64 generation = ++m_cookieGeneration;
    m_client->getWithCredentials(QStringLiteral("api/nuser/account/get"), credentials,
                                 [this, generation, credentials](const QJsonDocument &doc, bool ok) {
        if (generation != m_cookieGeneration)
            return;
        const QJsonObject root = doc.object();
        const int code = root.value(QStringLiteral("code")).toInt(200);
        if (!ok || root.value(QStringLiteral("profile")).toObject()
                           .value(QStringLiteral("userId")).toDouble() <= 0) {
            if (!ok)
                emit loginFailed(QStringLiteral("连不上服务器，请稍后再试"));
            else if (code == 200 || code == 301)
                emit loginFailed(QStringLiteral("Cookie 无效或已过期，请从浏览器重新复制"));
            else   // 风控（250）、限流（-460）等：Cookie 未必有问题
                emit loginFailed(QStringLiteral("网易暂时拒绝了登录（%1），请稍后再试").arg(code));
            // 这张二维码的轮询已经停了：回到扫码流程，对话框不至于停在空二维码上
            start();
            return;
        }
        m_client->replaceAuthCookies(credentials);
        // 校验期间可能又开了一张码（关掉再打开对话框）：登录成了就不再轮询它
        ++m_generation;
        m_polling = false;
        m_timer->stop();
        m_unikey.clear();
        setState(QStringLiteral("done"));
        m_store->refresh();
    });
}

void Session::poll()
{
    if (m_polling)
        return;
    if (m_unikey.isEmpty()) {
        m_timer->stop();
        return;
    }
    const quint64 generation = m_generation;
    const QString unikey = m_unikey;
    m_polling = true;
    m_client->get("api/login/qrcode/client/login",
                  QUrlQuery{{QStringLiteral("key"), unikey}, {QStringLiteral("type"), QStringLiteral("3")}},
                  false, [this, generation, unikey](const QJsonDocument &doc, bool ok) {
        m_polling = false;
        // 取消/重开过，或回包属于上一张二维码：不能改状态，更不能在 cancel 后把用户登进去
        if (generation != m_generation || unikey != m_unikey)
            return;
        const int code = ok ? doc.object().value("code").toInt() : 0;
        switch (code) {
        case 800: // 过期，轮询停止，等用户点刷新
            if (m_state != QLatin1String("waiting") && m_state != QLatin1String("scanned"))
                break;
            m_timer->stop();
            setState(QStringLiteral("expired"));
            break;
        case 802:
            if (m_state == QLatin1String("waiting"))
                setState(QStringLiteral("scanned"));
            break;
        case 803: {
            // 只从 waiting/scanned 接受成功：迟到的旧 800/802 不能把 done 改回去
            if (m_state != QLatin1String("waiting") && m_state != QLatin1String("scanned"))
                break;
            m_timer->stop();
            // 803 的 cookie 在不同版本里可能是 cookieSet 数组，也可能是单个 cookie 字符串；
            // 两种都收，之后统一落进 jar 并重拉首屏。
            const QJsonObject root = doc.object();
            QJsonArray jar;
            for (const QJsonValue &c : root.value("cookieSet").toArray())
                appendCookies(jar, c.toString());
            const QJsonValue cookie = root.value("cookie");
            if (cookie.isString())
                appendCookies(jar, cookie.toString());
            else if (cookie.isArray()) {
                for (const QJsonValue &c : cookie.toArray())
                    appendCookies(jar, c.toString());
            }
            QStringList cookieNames;
            for (const QJsonValue &c : jar)
                cookieNames.append(c.toObject().keys().value(0));
            qInfo().noquote() << "login qr success cookie keys=" << cookieNames.join(u',')
                              << "response fields=" << root.keys().join(u',');
            m_client->adoptCookies(QJsonDocument(jar));
            if (!m_client->hasMusicU()) {
                qWarning() << "login succeeded without MUSIC_U; response fields=" << root.keys()
                           << "cookie keys=" << cookieNames;
                setState(QStringLiteral("expired"));
                break;
            }
            setState(QStringLiteral("done"));
            m_store->refresh();
            break;
        }
        default: // 801 等待扫码，继续轮询；其余错误码不中断（瞬时失败）
            break;
        }
    });
}

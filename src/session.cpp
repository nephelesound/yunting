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

void appendCookies(QJsonArray &jar, const QString &raw)
{
    // 803 的 cookie 可能是 "MUSIC_U=…; __csrf=…" 这一种字符串（新版接口），
    // 也可能是 Set-Cookie 风格的属性串；只取我们需要的三个键。
    for (const QString &part : raw.split(u';')) {
        const QString pair = part.section(u';', 0, 0).trimmed();
        if (pair.isEmpty())
            continue;
        const QString name = pair.section(u'=', 0, 0).trimmed();
        if (name == QLatin1String("MUSIC_U") || name == QLatin1String("__csrf")
            || name == QLatin1String("NMTID")) {
            jar.append(QJsonObject{{name, pair.mid(name.length() + 1)}});
        }
    }
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

    // type=3 是 PC 客户端会话（SPlayer 与我们的默认），type=1 是网页会话（YesPlayMusic）；
    // 缺了这个参数会回 HTTP 200 + code 400「参数错误」。
    m_client->get("api/login/qrcode/unikey", QUrlQuery{{QStringLiteral("type"), qrType()}}, false,
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
    m_polling = false;
    m_timer->stop();
    m_unikey.clear();
    setState(QStringLiteral("idle"));
}

void Session::setLoginMode(const QString &mode)
{
    if (mode != QLatin1String("client") && mode != QLatin1String("web"))
        return;
    if (mode == m_loginMode)
        return;
    m_loginMode = mode;
    emit loginModeChanged();
    // 正在等一张码：它的轮询带着取码时的 type，换模式就得换一张码，否则 803 给的会话类型对不上
    if (m_state == QLatin1String("waiting") || m_state == QLatin1String("scanned"))
        start();
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
    // The reference service uses this endpoint through its default transport; the
    // Web MUSIC_U context is accepted by /weapi. Cookies arrive as Set-Cookie and
    // are persisted by NcmClient before this callback runs.
    m_client->postWeapi(QStringLiteral("login/token/refresh"), {}, true,
                        [this, generation](const QJsonDocument &doc, bool ok) {
        if (generation != m_refreshGeneration)
            return;
        m_refreshing = false;
        const int code = ok ? doc.object().value(QStringLiteral("code")).toInt() : 0;
        if (!ok || code != 200 || !m_client->hasMusicU()) {
            emit refreshFailed(QStringLiteral("登录状态刷新失败，请重新扫码"));
            return;
        }
        m_store->refresh();
        emit refreshed();
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
                  QUrlQuery{{QStringLiteral("key"), unikey}, {QStringLiteral("type"), qrType()}},
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

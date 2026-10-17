#include "session.h"
#include "ncmclient.h"
#include "store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrlQuery>

namespace {
constexpr int PollMs = 2000;
} // namespace

Session::Session(NcmClient *client, Store *store, QObject *parent)
    : QObject(parent), m_client(client), m_store(store)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(PollMs);
    connect(m_timer, &QTimer::timeout, this, &Session::poll);
}

void Session::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
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

    m_client->get("api/login/qrcode/unikey", QUrlQuery(), false,
                  [this, generation](const QJsonDocument &doc, bool ok) {
        if (generation != m_generation)
            return;   // 期间 cancel() 或重新 start()：这份 unikey 已经过时
        const QString unikey = ok ? doc.object().value("unikey").toString() : QString();
        if (unikey.isEmpty()) {
            qWarning() << "cannot get login qrcode";
            setState(QStringLiteral("idle"));
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
                  QUrlQuery{{QStringLiteral("key"), unikey}, {QStringLiteral("type"), QStringLiteral("1")}},
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
            // 成功回包带 cookieSet 数组（"NAME=VALUE; Path=..."）：挑要紧的三个落盘，重拉首屏
            QJsonArray jar;
            for (const QJsonValue &c : doc.object().value("cookieSet").toArray()) {
                const QString pair = c.toString().section(u';', 0, 0);
                const QString name = pair.section(u'=', 0, 0).trimmed();
                if (name == "MUSIC_U" || name == "__csrf" || name == "NMTID")
                    jar.append(QJsonObject{{name, pair.mid(name.length() + 1)}});
            }
            m_client->adoptCookies(QJsonDocument(jar));
            if (!m_client->hasMusicU()) {
                qWarning() << "login succeeded without MUSIC_U";
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

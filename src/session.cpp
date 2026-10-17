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
    m_timer->stop();
    setState(QStringLiteral("waiting"));
    emit qrContentChanged(); // 取到 unikey 前先清空旧码
    m_qrContent.clear();
    m_unikey.clear();

    m_client->get("api/login/qrcode/unikey", QUrlQuery(), false,
                  [this](const QJsonDocument &doc, bool ok) {
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
    m_timer->stop();
    m_unikey.clear();
    setState(QStringLiteral("idle"));
}

void Session::poll()
{
    if (m_unikey.isEmpty()) {
        m_timer->stop();
        return;
    }
    m_client->get("api/login/qrcode/client/login",
                  QUrlQuery{{QStringLiteral("key"), m_unikey}, {QStringLiteral("type"), QStringLiteral("1")}},
                  false, [this](const QJsonDocument &doc, bool ok) {
        const int code = ok ? doc.object().value("code").toInt() : 0;
        switch (code) {
        case 800: // 过期，轮询停止，等用户点刷新
            m_timer->stop();
            setState(QStringLiteral("expired"));
            break;
        case 802:
            setState(QStringLiteral("scanned"));
            break;
        case 803: {
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
            setState(QStringLiteral("done"));
            m_store->refresh();
            break;
        }
        default: // 801 等待扫码，继续轮询；其余错误码不中断（瞬时失败）
            break;
        }
    });
}

#include "powerinhibitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QUuid>

Q_LOGGING_CATEGORY(lcPower, "yunting.power")

namespace {
constexpr auto kPortalService = "org.freedesktop.portal.Desktop";
constexpr auto kPortalPath = "/org/freedesktop/portal/desktop";
constexpr auto kInhibitInterface = "org.freedesktop.portal.Inhibit";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

// xdg-desktop-portal 的 Inhibit 标志
constexpr uint kInhibitSuspend = 4;
constexpr uint kInhibitIdle = 8;

// 门户把请求对象放在 /org/freedesktop/portal/desktop/request/<调用者唯一名>/<handle_token>。
// 先按这个可预测的路径连上 Response，再发 Inhibit，避免响应比方法回包先到而漏掉。
QString requestPathFor(const QString &token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(u':'))
        sender.remove(0, 1);
    sender.replace(u'.', u'_');
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}
} // namespace

PowerInhibitor::PowerInhibitor(QObject *parent, const QString &sleepReason, const QString &screenReason)
    : QObject(parent)
    , m_sleep{kInhibitSuspend, sleepReason}
    , m_screen{kInhibitIdle, screenReason}
{
}

PowerInhibitor::~PowerInhibitor()
{
    for (auto it = m_handles.cbegin(); it != m_handles.cend(); ++it) {
        QDBusMessage close = QDBusMessage::createMethodCall(QLatin1String(kPortalService), it.key(),
                                                            QLatin1String(kRequestInterface),
                                                            QStringLiteral("Close"));
        QDBusConnection::sessionBus().send(close);
    }
}

void PowerInhibitor::setPreventSleep(bool on)
{
    if (m_sleep.wanted == on)
        return;
    m_sleep.wanted = on;
    reconcile(m_sleep);
    emit preventSleepChanged();
}

void PowerInhibitor::setKeepScreenOn(bool on)
{
    if (m_screen.wanted == on)
        return;
    m_screen.wanted = on;
    reconcile(m_screen);
    emit keepScreenOnChanged();
}

// 让门户里的实际状态追上「想要」：要而未持有 → Inhibit；持有而不要 → Close。
// 在途时不动，回包后按最新的「想要」再对一次（例如暂停又继续）。
void PowerInhibitor::reconcile(Inhibition &inhibition)
{
    if (inhibition.wanted && !inhibition.active && !inhibition.pending) {
        request(inhibition);
    } else if (!inhibition.wanted && inhibition.active) {
        closeHandle(inhibition.handle);
    }
}

void PowerInhibitor::request(Inhibition &inhibition)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCInfo(lcPower) << "no session bus, not inhibiting" << inhibition.reason;
        return;
    }

    const QString token = QUuid::createUuid().toString(QUuid::Id128);
    const QString path = requestPathFor(token);
    if (path.isEmpty()) {
        qCInfo(lcPower) << "cannot determine portal request path";
        return;
    }
    if (!bus.connect(QLatin1String(kPortalService), path, QLatin1String(kRequestInterface),
                     QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap)))) {
        qCInfo(lcPower) << "cannot listen for portal response" << path;
        return;
    }
    m_handles.insert(path, &inhibition);

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), token);
    options.insert(QStringLiteral("reason"), inhibition.reason);
    QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(kPortalService), QLatin1String(kPortalPath),
                                                       QLatin1String(kInhibitInterface), QStringLiteral("Inhibit"));
    // parent_window 为空：没有可挂靠的 X11/Wayland 窗口，门户不需要它
    call << QString() << inhibition.flags << options;

    inhibition.pending = true;
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, &inhibition, path](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        inhibition.pending = false;
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            forgetHandle(path);
            qCInfo(lcPower) << "inhibit unavailable:" << reply.error().message();
            return;   // 门户不在：不重试，下次状态变化再试
        }
        const QString handle = reply.value().path();
        if (handle.isEmpty()) {
            forgetHandle(path);
            return;
        }
        if (handle != path) {
            // 门户应使用 handle_token 生成请求路径；万一后端给了别的句柄，把监听搬过去。
            forgetHandle(path);
            if (!QDBusConnection::sessionBus().connect(
                    QLatin1String(kPortalService), handle, QLatin1String(kRequestInterface),
                    QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap)))) {
                closeHandle(handle);
                return;
            }
            m_handles.insert(handle, &inhibition);
        }
        inhibition.handle = handle;
        qCDebug(lcPower) << "portal inhibit requested" << inhibition.reason << handle;
        if (!inhibition.wanted)
            closeHandle(handle);
    });
}

void PowerInhibitor::onResponse(uint response, const QVariantMap &results)
{
    const QString path = message().path();
    const auto it = m_handles.find(path);
    if (it == m_handles.cend())
        return;
    Inhibition *inhibition = it.value();
    if (response == 0) {
        inhibition->active = true;
        inhibition->pending = false;
        inhibition->handle = path;
        qCDebug(lcPower) << "inhibited" << inhibition->reason;
        if (!inhibition->wanted)
            closeHandle(path);
    } else {
        // 用户或门户拒绝：没有句柄可持有，等下次状态变化再试。
        inhibition->active = false;
        inhibition->pending = false;
        forgetHandle(path);
        qCInfo(lcPower) << "portal denied inhibition" << inhibition->reason << response
                        << results.value(QStringLiteral("reason"));
    }
}

void PowerInhibitor::closeHandle(QString path)
{
    if (path.isEmpty())
        return;
    const auto it = m_handles.find(path);
    if (it != m_handles.cend()) {
        Inhibition *inhibition = it.value();
        if (inhibition->handle == path)
            inhibition->handle.clear();
        inhibition->active = false;
        inhibition->pending = false;
        m_handles.erase(it);
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.disconnect(QLatin1String(kPortalService), path, QLatin1String(kRequestInterface),
                   QStringLiteral("Response"), this, SLOT(onResponse(uint,QVariantMap)));
    QDBusMessage close = QDBusMessage::createMethodCall(QLatin1String(kPortalService), path,
                                                        QLatin1String(kRequestInterface), QStringLiteral("Close"));
    bus.send(close);
    qCDebug(lcPower) << "released portal inhibition" << path;
}

// 只丢弃对请求句柄的跟踪，不发送 Close：用于 Inhibit 失败、或门户已经给出 Response 的情况。
void PowerInhibitor::forgetHandle(QString path)
{
    const auto it = m_handles.find(path);
    if (it == m_handles.cend())
        return;
    Inhibition *inhibition = it.value();
    if (inhibition->handle == path)
        inhibition->handle.clear();
    m_handles.erase(it);
    QDBusConnection::sessionBus().disconnect(QLatin1String(kPortalService), path,
                                             QLatin1String(kRequestInterface), QStringLiteral("Response"),
                                             this, SLOT(onResponse(uint,QVariantMap)));
}

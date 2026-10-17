#include "powerinhibitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPower, "yunting.power")

PowerInhibitor::PowerInhibitor(QObject *parent)
    : QObject(parent)
    , m_sleep{"org.freedesktop.PowerManagement", "/org/freedesktop/PowerManagement/Inhibit",
              "org.freedesktop.PowerManagement.Inhibit", "正在播放音乐"}
    , m_screen{"org.freedesktop.ScreenSaver", "/ScreenSaver", "org.freedesktop.ScreenSaver", "正在播放 MV"}
{
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

// 让实际持有的状态追上「想要」：要而未持有 → Inhibit；持有而不要 → UnInhibit。
// 在途时不动，回包里再调一次自己（这期间「想要」可能已经变了，例如暂停又继续）
void PowerInhibitor::reconcile(Inhibition &inhibition)
{
    if (inhibition.pending)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    const auto method = [&inhibition](const QString &name) {
        return QDBusMessage::createMethodCall(QLatin1String(inhibition.service), QLatin1String(inhibition.path),
                                              QLatin1String(inhibition.interface), name);
    };

    if (inhibition.wanted && inhibition.cookie == 0) {
        QDBusMessage call = method(QStringLiteral("Inhibit"));
        // 应用 id 用 .desktop 名：电池小部件据此显示应用名与图标
        call << QGuiApplication::desktopFileName() << QString::fromUtf8(inhibition.reason);
        inhibition.pending = true;
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, &inhibition](QDBusPendingCallWatcher *w) {
            w->deleteLater();
            inhibition.pending = false;
            const QDBusPendingReply<uint> reply = *w;
            if (reply.isError()) {
                qCInfo(lcPower) << "inhibit unavailable:" << inhibition.service << reply.error().message();
                return;   // 服务不在：不重试，下次状态变化再试
            }
            inhibition.cookie = reply.value();
            qCDebug(lcPower) << "inhibited" << inhibition.reason << "cookie" << inhibition.cookie;
            reconcile(inhibition);
        });
    } else if (!inhibition.wanted && inhibition.cookie != 0) {
        QDBusMessage call = method(QStringLiteral("UnInhibit"));
        call << inhibition.cookie;
        bus.send(call);
        qCDebug(lcPower) << "released" << inhibition.reason << "cookie" << inhibition.cookie;
        inhibition.cookie = 0;
    }
}

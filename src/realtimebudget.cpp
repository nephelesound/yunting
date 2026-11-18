#include "realtimebudget.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>

#include <optional>
#include <sys/resource.h>

namespace {

bool present(const QDBusConnection &bus, const QString &service)
{
    return bus.isConnected() && bus.interface()->isServiceRegistered(service);
}

std::optional<qint64> advertisedBudget(QDBusConnection bus, const QString &service, const QString &path,
                                       const QString &interface)
{
    QDBusMessage get = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << interface << QStringLiteral("RTTimeUSecMax");
    const QDBusMessage reply = bus.call(get, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage)
        return std::nullopt;   // 读不到时 module-rt 用自己的默认值，不会是 0
    return reply.arguments().value(0).value<QDBusVariant>().variant().toLongLong();
}

} // namespace

void RealtimeBudget::refuseZero()
{
    if (qEnvironmentVariableIsSet("DISABLE_RTKIT"))
        return;
    // 与 module-rt 同一个顺序：门户在就只认门户
    const QString portal = QStringLiteral("org.freedesktop.portal.Desktop");
    const QString rtkit = QStringLiteral("org.freedesktop.RealtimeKit1");
    std::optional<qint64> advertised;
    if (present(QDBusConnection::sessionBus(), portal))
        advertised = advertisedBudget(QDBusConnection::sessionBus(), portal,
                                      QStringLiteral("/org/freedesktop/portal/desktop"),
                                      QStringLiteral("org.freedesktop.portal.Realtime"));
    else if (present(QDBusConnection::systemBus(), rtkit))
        advertised = advertisedBudget(QDBusConnection::systemBus(), rtkit, QStringLiteral("/org/freedesktop/RealtimeKit1"),
                                      rtkit);
    // 继承来的硬限制已经是 0（启动它的进程踩过同一个坑）也一样：rtkit 照批，批了就被杀
    rlimit inherited{};
    const bool inheritedZero = getrlimit(RLIMIT_RTTIME, &inherited) == 0 && inherited.rlim_max == 0;
    if ((advertised && *advertised <= 0) || inheritedZero) {
        qWarning().nospace() << "realtime budget is zero (advertised=" << advertised.value_or(-1)
                             << ", inherited hard limit zero=" << inheritedZero
                             << "); audio plays without realtime scheduling";
        qputenv("DISABLE_RTKIT", "1");
    }
}

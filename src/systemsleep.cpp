#include "systemsleep.h"

#include <QDBusConnection>
#include <QTimer>

#include <ctime>

namespace {
// 睡眠之外两个时钟之差也会有毫秒级的漂移：超过这个值才算睡过
constexpr qint64 kSleptMs = 2000;
// 收不到 logind 时，恢复后最晚这么久发现
constexpr int kClockCheckMs = 5000;

qint64 clockMs(clockid_t clock)
{
    timespec ts{};
    clock_gettime(clock, &ts);
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

qint64 systemSuspendedMs()
{
    return clockMs(CLOCK_BOOTTIME) - clockMs(CLOCK_MONOTONIC);
}
} // namespace

SystemSleep::SystemSleep(QObject *parent, std::function<qint64()> suspendedMs)
    : QObject(parent),
      m_suspendedMs(suspendedMs ? std::move(suspendedMs) : systemSuspendedMs),
      m_seenSuspendedMs(m_suspendedMs()),
      m_clock(new QTimer(this))
{
    // 没有系统总线或 logind（容器、Flatpak 未授权）时 connect 失败，只剩下面的时钟
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this,
                                         SLOT(onPrepareForSleep(bool)));
    m_clock->setInterval(kClockCheckMs);
    connect(m_clock, &QTimer::timeout, this, &SystemSleep::checkClock);
    m_clock->start();
}

void SystemSleep::onPrepareForSleep(bool sleeping)
{
    if (sleeping) {
        emit aboutToSleep();
        return;
    }
    // 这次睡眠已经由 logind 报告：时钟不再为它报一次
    m_seenSuspendedMs = m_suspendedMs();
    emit resumed();
}

void SystemSleep::checkClock()
{
    const qint64 suspended = m_suspendedMs();
    if (suspended - m_seenSuspendedMs < kSleptMs)
        return;
    m_seenSuspendedMs = suspended;
    emit resumed();
}

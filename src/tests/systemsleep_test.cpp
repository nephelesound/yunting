// SystemSleep：每次睡眠只报一次醒来，不论是 logind 报的还是时钟发现的
#include "systemsleep.h"

#include <QSignalSpy>
#include <QTest>

class SystemSleepTest : public QObject {
    Q_OBJECT

private slots:
    void clockFindsEachSleepOnce()
    {
        qint64 suspended = 500;
        SystemSleep sleep(nullptr, [&suspended] { return suspended; });
        QSignalSpy resumed(&sleep, &SystemSleep::resumed);

        suspended += 300;   // 两个时钟的正常漂移，不是睡眠
        QMetaObject::invokeMethod(&sleep, "checkClock");
        QCOMPARE(resumed.count(), 0);

        suspended += 30 * 60 * 1000;   // 睡了半小时
        QMetaObject::invokeMethod(&sleep, "checkClock");
        QMetaObject::invokeMethod(&sleep, "checkClock");
        QCOMPARE(resumed.count(), 1);
    }

    void logindWakeIsNotReportedAgainByTheClock()
    {
        qint64 suspended = 0;
        SystemSleep sleep(nullptr, [&suspended] { return suspended; });
        QSignalSpy before(&sleep, &SystemSleep::aboutToSleep);
        QSignalSpy resumed(&sleep, &SystemSleep::resumed);

        QMetaObject::invokeMethod(&sleep, "onPrepareForSleep", Q_ARG(bool, true));
        QCOMPARE(before.count(), 1);
        suspended += 60 * 1000;
        QMetaObject::invokeMethod(&sleep, "onPrepareForSleep", Q_ARG(bool, false));
        QMetaObject::invokeMethod(&sleep, "checkClock");
        QCOMPARE(resumed.count(), 1);
    }

    // 反过来：时钟先发现、logind 的醒来信号后到
    void clockWakeIsNotReportedAgainByLogind()
    {
        qint64 suspended = 0;
        SystemSleep sleep(nullptr, [&suspended] { return suspended; });
        QSignalSpy resumed(&sleep, &SystemSleep::resumed);

        QMetaObject::invokeMethod(&sleep, "onPrepareForSleep", Q_ARG(bool, true));
        suspended += 60 * 1000;
        QMetaObject::invokeMethod(&sleep, "checkClock");
        QMetaObject::invokeMethod(&sleep, "onPrepareForSleep", Q_ARG(bool, false));
        QCOMPARE(resumed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(SystemSleepTest)
#include "systemsleep_test.moc"

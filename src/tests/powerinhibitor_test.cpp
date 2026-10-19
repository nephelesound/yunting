// PowerInhibitor 对着真的会话总线测：效果只能在 Plasma 的电源管理里看到（PolicyAgent 列出的抑制项）。
// 没有会话总线或不是 Plasma（CI、沙箱）时跳过。测试期间本机会短暂地被抑制睡眠，结束即撤销。
#include "../powerinhibitor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QGuiApplication>
#include <QTest>
#include <QUuid>

class PowerInhibitorTest : public QObject {
    Q_OBJECT

    static constexpr auto kApp = "io.github.nephelesound.Yunting.test";
    // PowerDevil 收到 freedesktop 的抑制后约 5 秒才生效（短暂的抑制不算），等足这么久
    static constexpr int kApplied = 10000;

    // PolicyAgent 的 RequestedInhibitions（a(ssssu)：类型、应用、原因、……）里我们这条原因出现几次。
    // 应用名由桌面门户按真实应用 id 填写，测试进程没有安装自己的 .desktop，所以只认原因。
    static int ours(const QString &reason)
    {
        QDBusMessage get = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
        get << QStringLiteral("org.kde.Solid.PowerManagement.PolicyAgent") << QStringLiteral("RequestedInhibitions");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(get);
        if (reply.type() != QDBusMessage::ReplyMessage)
            return -1;
        const QDBusArgument list = reply.arguments().value(0).value<QDBusVariant>().variant().value<QDBusArgument>();
        int n = 0;
        list.beginArray();
        while (!list.atEnd()) {
            QString type, app, why, mode;
            uint flags = 0;
            list.beginStructure();
            list >> type >> app >> why >> mode >> flags;
            list.endStructure();
            n += why == reason;
        }
        list.endArray();
        return n;
    }

private slots:
    void initTestCase()
    {
        QGuiApplication::setDesktopFileName(QLatin1String(kApp));
        const QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()->isServiceRegistered(QStringLiteral("org.kde.Solid.PowerManagement")))
            QSKIP("没有 Plasma 的电源管理（无会话总线或非 KDE）");
    }

    void sleepIsInhibitedOnlyWhilePlaying()
    {
        const QString reason = QStringLiteral("云听测试睡眠 %1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        PowerInhibitor power(nullptr, reason, reason);
        power.setPreventSleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 1, kApplied);
        power.setPreventSleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 0, kApplied);
    }

    // 暂停又继续发生在请求在途时：最后只持有一个抑制，不漏撤也不多加
    void quickToggleEndsWithExactlyOne()
    {
        const QString reason = QStringLiteral("云听测试连按 %1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        PowerInhibitor power(nullptr, reason, reason);
        power.setPreventSleep(true);
        power.setPreventSleep(false);
        power.setPreventSleep(true);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 1, kApplied);
        QTest::qWait(300);
        QCOMPARE(ours(reason), 1);
        power.setPreventSleep(false);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 0, kApplied);
    }

    void screenStaysOnForVideo()
    {
        const QString reason = QStringLiteral("云听测试 MV %1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        PowerInhibitor power(nullptr, reason, reason);
        power.setKeepScreenOn(true);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 1, kApplied);
        power.setKeepScreenOn(false);
        QTRY_COMPARE_WITH_TIMEOUT(ours(reason), 0, kApplied);
    }
};

QTEST_GUILESS_MAIN(PowerInhibitorTest)
#include "powerinhibitor_test.moc"

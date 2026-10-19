// 诊断信息：只留警告以上与本应用的 info，最多 200 条，报告里有版本与日志
#include "../diagnostics.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QTest>

Q_LOGGING_CATEGORY(testUi, "yunting.ui")
Q_LOGGING_CATEGORY(testOther, "qt.other")

class DiagnosticsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setApplicationName(QStringLiteral("yunting"));
        Diagnostics::install();
    }

    void keepsWarningsAndOwnInfoOnly()
    {
        qCWarning(testOther) << "decoder stalled";
        qCInfo(testUi) << "like queued";
        qCInfo(testOther) << "noise";
        qCDebug(testUi) << "debug noise";
        const QStringList log = Diagnostics::recent();
        QVERIFY(log.last().contains(QStringLiteral("yunting.ui: like queued")));
        QVERIFY(log.at(log.size() - 2).contains(QStringLiteral("warning qt.other: decoder stalled")));
        for (const QString &line : log)
            QVERIFY(!line.contains(QStringLiteral("noise")));
    }

    void keepsTheLastTwoHundred()
    {
        for (int i = 0; i < 250; ++i)
            Diagnostics::record(QtWarningMsg, "yunting.test", QStringLiteral("line %1").arg(i));
        const QStringList log = Diagnostics::recent();
        QCOMPARE(log.size(), 200);
        QVERIFY(log.last().endsWith(QStringLiteral("line 249")));
        QVERIFY(log.first().endsWith(QStringLiteral("line 50")));
    }

    void reportStartsWithTheVersion()
    {
        Diagnostics diagnostics;
        QVERIFY(diagnostics.report().startsWith(QStringLiteral("yunting ")));
        QVERIFY(diagnostics.report().contains(QStringLiteral("line 249")));
        QVERIFY(!diagnostics.report().contains(QStringLiteral("流量：")));
    }

    // 网络出口给的流量摘要放进报告：排查请求风暴时一眼能看到最近的请求量
    void reportIncludesTraffic()
    {
        Diagnostics diagnostics;
        diagnostics.traffic = [] { return QStringLiteral("最近一分钟 3 个请求"); };
        QVERIFY(diagnostics.report().contains(QStringLiteral("流量：最近一分钟 3 个请求")));
    }
};

QTEST_GUILESS_MAIN(DiagnosticsTest)
#include "diagnostics_test.moc"

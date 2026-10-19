// TrafficMeter：网络出口的流量计。时间由测试直接给，规则不依赖真实时钟。
#include "../traffic.h"

#include <QRegularExpression>
#include <QTest>

class TrafficTest : public QObject {
    Q_OBJECT

private slots:
    // 循环造成的重复读请求：1 秒内同一个请求第 4 次起拒绝，过了 1 秒又能发
    void repeatedReadIsRefusedWithinASecond()
    {
        TrafficMeter meter;
        for (int i = 0; i < TrafficMeter::RepeatLimit; ++i)
            QVERIFY(meter.admit("GET", "api/song/lyric", "id=1", 100 + i));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("refused GET api/song/lyric"));
        QVERIFY(!meter.admit("GET", "api/song/lyric", "id=1", 200));
        QVERIFY(!meter.admit("GET", "api/song/lyric", "id=1", 300));   // 同一种告警 10 秒内只打一次
        QVERIFY(meter.admit("GET", "api/song/lyric", "id=2", 300));     // 参数不同就是另一个请求
        QVERIFY(meter.admit("GET", "api/song/lyric", "id=1", 1200));    // 1 秒后不再算重复
        QVERIFY(meter.summary(1200).contains(QStringLiteral("拒绝 2 个重复请求")));
    }

    // 正常的轮询（一起听 1 秒一次）与失败重试不会被拦
    void pollingAndRetriesPass()
    {
        TrafficMeter meter;
        for (int second = 0; second < 30; ++second)
            QVERIFY(meter.admit("GET", "api/listen/together/sync/list/get", "roomId=r", second * 1000));
        for (const qint64 at : {0, 500, 1500, 3500})   // 首次 + 三次重试（NcmClient::RetryDelaysMs）
            QVERIFY(meter.admit("GET", "api/v6/playlist/detail", "id=7", 100000 + at));
    }

    // 写请求只计数，不拦：weapi 请求体每次不同，连点红心是用户操作
    void writesAreCountedNotRefused()
    {
        TrafficMeter meter;
        for (int i = 0; i < 10; ++i)
            QVERIFY(meter.admit("POST", "/weapi/song/like", {}, 50 + i));
        QVERIFY(meter.summary(100).contains(QStringLiteral("POST /weapi/song/like ×10")));
    }

    // 1 秒内请求太多：告警（带最常请求的路径），不拒绝
    void burstIsReported()
    {
        TrafficMeter meter;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("request burst: 41 requests within 1 s"));
        for (int i = 0; i <= TrafficMeter::BurstPerSecond; ++i)
            QVERIFY(meter.admit("GET", "api/song/detail", QStringLiteral("id=%1").arg(i), 10 + i));
        QVERIFY(meter.summary(100).contains(QStringLiteral("峰值 41 个/秒")));
    }

    // 摘要只有路径与次数：参数里可能有搜索词，不能进诊断信息；一分钟前的不算
    void summaryNamesPathsOnlyAndForgets()
    {
        TrafficMeter meter;
        meter.admit("GET", "api/search/get", "s=secret-words", 0);
        meter.admit("GET", "api/search/get", "s=other", 10);
        const QString now = meter.summary(20);
        QVERIFY(now.contains(QStringLiteral("最近一分钟 2 个请求")));
        QVERIFY(now.contains(QStringLiteral("GET api/search/get ×2")));
        QVERIFY(!now.contains(QStringLiteral("secret")));
        meter.admit("GET", "api/hot", {}, 70000);
        QVERIFY(meter.summary(70000).contains(QStringLiteral("最近一分钟 1 个请求")));
    }
};

QTEST_GUILESS_MAIN(TrafficTest)
#include "traffic_test.moc"

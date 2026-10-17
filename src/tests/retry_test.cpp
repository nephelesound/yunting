// NcmClient 重试策略的行为测试：seam 在 I/O 边界（本地 QTcpServer / 端口扮演 music.163.com），
// 不 mock NcmClient 内部。覆盖：5xx 重试后成功（回调一次、间隔在走）、连 5xx 重试耗尽（恰 3 次）、
// 4xx 不重试、连接被拒按「网络原因」重试并耗尽。
// 注意不模拟「已建连后断开」：QNetworkAccessManager 会静默把这类请求在新连接上重发一遍，
// 脚本消耗就不确定了；回完整 HTTP 响应与连接被拒都是确定性的。
#include "../ncmclient.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QQueue>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

class RetryTest : public QObject {
    Q_OBJECT

    // 扮演 API：每个连接按脚本回一个状态码；请求头可能分多段到达，攒到「\r\n\r\n」才回，
    // 每个连接只回一次。脚本耗尽后回 200。
    class FakeApi : public QTcpServer {
    public:
        explicit FakeApi(QObject *parent = nullptr) : QTcpServer(parent)
        {
            connect(this, &QTcpServer::newConnection, this, [this] {
                while (QTcpSocket *sock = nextPendingConnection()) {
                    connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                        QByteArray &buf = m_buf[sock];
                        if (buf.contains("\r\n\r\n"))
                            return;
                        buf += sock->readAll();
                        if (!buf.contains("\r\n\r\n"))
                            return;
                        buf.clear();   // 连接被复用时还要服务下一个请求
                        const int status = script.isEmpty() ? 200 : script.takeFirst();
                        const QByteArray body = "{\"code\":200}\n";
                        sock->write("HTTP/1.1 " + QByteArray::number(status)
                                    + " x\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n"
                                    + body);
                        sock->flush();
                    });
                    connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
                }
            });
        }
        QQueue<int> script;

    private:
        QHash<QTcpSocket *, QByteArray> m_buf;
    };

    // 等一个请求落地：带超时，不悬挂测试
    struct Outcome {
        bool ok = false;
        bool networkFailure = false;
        int calls = 0;
        void record(bool o, bool n) { ++calls; ok = o; networkFailure = n; }
    };

private slots:
    void serverErrorThenSuccessRetries()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.script = QQueue<int>({503, 200});
        NcmClient client;
        client.setApiBase(baseUrl(&api));
        Outcome out;
        QElapsedTimer clock;
        clock.start();
        client.get("api/x", {}, false, [&](const QJsonDocument &doc, bool ok) {
            out.record(ok, false);
            QCOMPARE(doc.object().value("code").toInt(), 200);
        });
        QVERIFY(wait(out));
        QCOMPARE(out.calls, 1);                // 回调只发一次
        QCOMPARE(out.ok, true);
        QCOMPARE(out.networkFailure, false);   // 5xx 是服务器说的，不算离线
        QVERIFY2(clock.elapsed() >= 450, "0.5s 重试间隔没走");
        QVERIFY2(clock.elapsed() < 3000, "重试太慢");
    }

    void serverErrorsExhaustRetries()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.script = QQueue<int>({503, 502, 504, 500});   // 首次 + 3 次重试全部 5xx
        NcmClient client;
        client.setApiBase(baseUrl(&api));
        Outcome out;
        QElapsedTimer clock;
        clock.start();
        client.get("api/x", {}, false, [&](const QJsonDocument &doc, bool ok) {
            out.record(ok, false);
            QVERIFY(doc.isNull());
        });
        QVERIFY(wait(out));
        QCOMPARE(out.calls, 1);
        QCOMPARE(out.ok, false);
        QCOMPARE(out.networkFailure, false);   // 5xx 耗尽是「服务器说不行」，不算离线
        QVERIFY(api.script.isEmpty());         // 恰好用完首次 + 3 次重试
        // 总间隔 0.5 + 1 + 2 = 3.5 秒
        QVERIFY2(clock.elapsed() >= 3400, "重试间隔合计不足 3.5s");
        QVERIFY2(clock.elapsed() < 6000, "重试太慢");
    }

    void clientErrorIsNotRetried()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.script = QQueue<int>({404});
        NcmClient client;
        client.setApiBase(baseUrl(&api));
        Outcome out;
        QElapsedTimer clock;
        clock.start();
        client.get("api/x", {}, false, [&](const QJsonDocument &doc, bool ok) {
            out.record(ok, false);
            QVERIFY(doc.isNull());
        });
        QVERIFY(wait(out));
        QCOMPARE(out.calls, 1);
        QCOMPARE(out.ok, false);
        QCOMPARE(out.networkFailure, false);
        QCOMPARE(api.script.size(), 0);        // 404 只消耗了脚本第一条，没有重试
        QVERIFY2(clock.elapsed() < 1000, "4xx 不该等重试间隔");
    }

    // 连接被拒（无 HTTP 状态）也按同一节奏重试，耗尽后按「网络原因」分类：Store 据此报离线
    void refusedConnectionExhaustsRetriesAsNetworkFailure()
    {
        const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(deadPort());
        NcmClient client;
        client.setApiBase(base);
        Outcome out;
        QElapsedTimer clock;
        clock.start();
        client.get("api/x", {}, false, [&](const QJsonDocument &doc, bool ok) {
            Q_UNUSED(doc);
            out.record(ok, out.networkFailure);
        });
        connect(&client, &NcmClient::requestFinished,
                [&](bool, bool net) { out.networkFailure = net; });
        QVERIFY(wait(out));
        QCOMPARE(out.calls, 1);
        QCOMPARE(out.ok, false);
        QCOMPARE(out.networkFailure, true);
        QVERIFY2(clock.elapsed() >= 3400, "重试间隔合计不足 3.5s");
        QVERIFY2(clock.elapsed() < 6000, "重试太慢");
    }

private:
    static QString baseUrl(const FakeApi *api)
    {
        return QStringLiteral("http://127.0.0.1:%1/").arg(api->serverPort());
    }
    // 已关闭的端口：connect 立刻 ECONNREFUSED，稳定复现「无 HTTP 状态」的网络错误
    static quint16 deadPort()
    {
        QTcpServer probe;
        const quint16 port = probe.listen(QHostAddress::LocalHost) ? probe.serverPort() : 1;
        probe.close();
        return port;
    }
    // 等到请求落地或超时；重试节奏最长 0.5+1+2=3.5 秒，6 秒足够
    static bool wait(const Outcome &out)
    {
        QDeadlineTimer deadline(6 * 1000);
        while (out.calls == 0 && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return out.calls == 1;
    }
};

QTEST_GUILESS_MAIN(RetryTest)
#include "retry_test.moc"

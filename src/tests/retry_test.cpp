// NcmClient 重试策略的行为测试：seam 在 I/O 边界（本地 QTcpServer / 端口扮演 music.163.com），
// 不 mock NcmClient 内部。覆盖：5xx 重试后成功（回调一次、间隔在走）、连 5xx 重试耗尽（恰 3 次）、
// 4xx 不重试、连接被拒按「网络原因」重试并耗尽。
// 注意不模拟「已建连后断开」：QNetworkAccessManager 会静默把这类请求在新连接上重发一遍，
// 脚本消耗就不确定了；回完整 HTTP 响应与连接被拒都是确定性的。
#include "../ncmclient.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QQueue>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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
                        if (hold) {
                            held = sock;
                            hold = false;
                            return;
                        }
                        const int status = script.isEmpty() ? 200 : script.takeFirst();
                        const QByteArray body = "{\"code\":200}\n";
                        sock->write("HTTP/1.1 " + QByteArray::number(status)
                                    + " x\r\nContent-Type: application/json\r\nContent-Length: "
                                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n"
                                    + (setCookie.isEmpty() ? QByteArray() : "Set-Cookie: " + setCookie + "\r\n")
                                    + (location.isEmpty() ? QByteArray() : "Location: " + location + "\r\n")
                                    + "\r\n" + body);
                        sock->flush();
                    });
                    connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
                }
            });
        }
        QQueue<int> script;
        QByteArray setCookie;   // 非空时每个响应都带这个 Set-Cookie
        QByteArray location;    // 非空时每个响应都带这个 Location（短链跳转）
        bool hold = false;      // 下一个请求挂起，由 release() 回包
        QPointer<QTcpSocket> held;

        bool release(const QByteArray &body = "{\"code\":200}",
                     const QByteArray &cookie = {})
        {
            if (!held)
                return false;
            QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                + QByteArray::number(body.size()) + "\r\nConnection: close\r\n"
                + (cookie.isEmpty() ? QByteArray() : "Set-Cookie: " + cookie + "\r\n")
                + "\r\n" + body;
            held->write(response);
            held->flush();
            held->disconnectFromHost();
            held.clear();
            return true;
        }

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

    QTemporaryDir m_home;

private slots:
    // 凭据只落在这次测试自己的目录里：断开会话总线（钥匙串不可用 → 后备文件），数据目录指到临时目录。
    // 即使被测代码出错，也碰不到真实的钥匙串与 ~/.local/share
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    }

    // 匿名调试（不读存储）时，响应里的 Set-Cookie 绝不能写回存储：那会用匿名 jar 盖掉登录凭据。
    // 对照：同样的响应，正常的客户端会写（证明这个测试能失败）
    void anonymousClientNeverWritesCredentials()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.setCookie = "__csrf=normal-test-value; Path=/";
        const auto requestOnce = [&](NcmClient &client) {
            client.setApiBase(baseUrl(&api));
            Outcome out;
            client.get("api/x", {}, false, [&](const QJsonDocument &, bool ok) { out.record(ok, false); });
            QVERIFY(wait(out));
        };

        // 写凭据是异步的（先试钥匙串，失败再落后备文件）：每次检查前等写完，否则迟到的写会混进下一步
        NcmClient anonymous(nullptr, false);
        const QString store = anonymous.fallbackPath();
        QFile::remove(store);
        requestOnce(anonymous);
        QVERIFY(anonymous.flushCredentials(3000));
        QVERIFY2(!QFile::exists(store), "匿名客户端把 cookie 写进了凭据存储");

        NcmClient normal;
        QTRY_VERIFY(normal.isReady());
        requestOnce(normal);
        QVERIFY(normal.flushCredentials(3000));
        QVERIFY2(QFile::exists(store), "对照失败：正常客户端没有写凭据，测试证明不了什么");
    }


    // 退出登录后，旧请求迟到的 Set-Cookie 不能把账号写回来
    void lateSetCookieAfterLogoutIsIgnored()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.hold = true;
        NcmClient client;
        QTRY_VERIFY(client.isReady());
        client.setApiBase(baseUrl(&api));
        client.adoptCookies(QJsonDocument(QJsonArray{QJsonObject{{"MUSIC_U", "old"}}}));
        QVERIFY(client.hasMusicU());

        int callbacks = 0;
        client.get("api/x", {}, true, [&](const QJsonDocument &, bool) { ++callbacks; });
        QTRY_VERIFY(api.held != nullptr);
        client.clearAuthCookies();
        QVERIFY(!client.hasMusicU());
        QVERIFY(api.release("{\"code\":200}", "MUSIC_U=late; Path=/"));
        QTRY_COMPARE(callbacks, 1);
        QVERIFY2(!client.hasMusicU(), "迟到的 Set-Cookie 在退出登录后复活了账号");
    }

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
            // HTTP 4xx/5xx 的 JSON 也要交出来，错误原因在 body 里
            QVERIFY(!doc.isNull());
            QCOMPARE(doc.object().value("code").toInt(), 200);
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
            // HTTP 4xx/5xx 的 JSON 也要交出来，错误原因在 body 里
            QVERIFY(!doc.isNull());
            QCOMPARE(doc.object().value("code").toInt(), 200);
        });
        QVERIFY(wait(out));
        QCOMPARE(out.calls, 1);
        QCOMPARE(out.ok, false);
        QCOMPARE(out.networkFailure, false);
        QCOMPARE(api.script.size(), 0);        // 404 只消耗了脚本第一条，没有重试
        QVERIFY2(clock.elapsed() < 1000, "4xx 不该等重试间隔");
    }

    // 短链：只问跳转、不跟过去，交出 Location；没有跳转就交空地址
    void shortLinkResolvesToItsTargetWithoutFollowing()
    {
        FakeApi api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        // 跳转指回这个假服务器自己：要是跟过去了，会多吃掉一条脚本
        api.script = QQueue<int>({302, 500, 200});
        api.location = "/song?id=186001";
        NcmClient client;
        QUrl target;
        bool done = false;
        client.resolveRedirect(QUrl(baseUrl(&api) + "xLb2c3"), [&](const QUrl &t) { target = t; done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(target, QUrl(baseUrl(&api) + "song?id=186001"));
        QCOMPARE(api.script.size(), 2);   // 只问了一次，没有跟着跳

        api.location.clear();
        done = false;
        client.resolveRedirect(QUrl(baseUrl(&api) + "gone"), [&](const QUrl &t) { target = t; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(target.isEmpty());
        QCOMPARE(api.script.size(), 1);   // 500 也只问一次，不重试
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

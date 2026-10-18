// Shared SDK transport seam: HTTP errors, business codes, network failures and
// the personal cookie flag. Uses a local TCP server, never the network.
#include "sdk/service.h"

#include "ncmclient.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace ncm::sdk;

class HttpStub : public QTcpServer {
public:
    int status = 200;
    QByteArray body = "{\"code\":200}";
    QByteArray headers;

    explicit HttpStub(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    int length = 0;
                    const QByteArray head = buffer->left(end);
                    for (const QByteArray &line : head.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toInt();
                    }
                    if (buffer->mid(end + 4).size() < length)
                        return;
                    headers = head;
                    buffer->clear();
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " x\r\n"
                                  "Content-Type: application/json\r\nContent-Length: "
                                  + QByteArray::number(body.size())
                                  + "\r\nConnection: close\r\n\r\n" + body);
                    socket->flush();
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
                connect(socket, &QTcpSocket::disconnected, this, [buffer] { delete buffer; });
            }
        });
    }
};

class SdkServiceTest : public QObject {
    Q_OBJECT

    static bool waitFor(const std::function<bool()> &done, int timeout = 3000)
    {
        QDeadlineTimer deadline(timeout);
        while (!done() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    }

    static void call(NcmClient &client, bool personal, std::function<void(Result<QJsonObject>)> done)
    {
        postFormJson(client, nullptr, QStringLiteral("api/test"), {}, std::move(done), personal);
    }

private slots:
    void httpErrorIsApiNotNetwork()
    {
        HttpStub stub;
        QVERIFY(stub.listen(QHostAddress::LocalHost));
        stub.status = 403;
        stub.body = "{\"code\":403,\"message\":\"denied\"}";
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(stub.serverPort()));

        std::optional<Result<QJsonObject>> result;
        call(client, false, [&](Result<QJsonObject> value) { result = std::move(value); });
        QVERIFY(waitFor([&] { return result.has_value(); }));
        QVERIFY(!result->ok());
        QCOMPARE(result->error.kind, ErrorKind::Api);
        QCOMPARE(result->error.httpStatus, 403);
        QCOMPARE(result->error.code, 403);
    }

    void businessCodeIsApi()
    {
        HttpStub stub;
        QVERIFY(stub.listen(QHostAddress::LocalHost));
        stub.body = "{\"code\":301,\"message\":\"expired\"}";
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(stub.serverPort()));

        std::optional<Result<QJsonObject>> result;
        call(client, false, [&](Result<QJsonObject> value) { result = std::move(value); });
        QVERIFY(waitFor([&] { return result.has_value(); }));
        QVERIFY(!result->ok());
        QCOMPARE(result->error.kind, ErrorKind::Api);
        QCOMPARE(result->error.code, 301);
    }

    void connectionFailureIsNetwork()
    {
        quint16 port = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost));
            port = probe.serverPort();
        }
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(port));

        std::optional<Result<QJsonObject>> result;
        call(client, false, [&](Result<QJsonObject> value) { result = std::move(value); });
        QVERIFY(waitFor([&] { return result.has_value(); }, 5000));
        QVERIFY(!result->ok());
        QCOMPARE(result->error.kind, ErrorKind::Network);
    }

    void personalFlagControlsCookie()
    {
        HttpStub stub;
        QVERIFY(stub.listen(QHostAddress::LocalHost));
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(stub.serverPort()));
        client.adoptCookies(QJsonDocument(QJsonArray{
            QJsonObject{{"MUSIC_U", "session"}, {"__csrf", "csrf"}}}));

        std::optional<Result<QJsonObject>> result;
        call(client, true, [&](Result<QJsonObject> value) { result = std::move(value); });
        QVERIFY(waitFor([&] { return result.has_value(); }));
        QVERIFY(result->ok());
        QVERIFY(stub.headers.contains("MUSIC_U=session"));
    }
};

QTEST_GUILESS_MAIN(SdkServiceTest)
#include "sdk_service_test.moc"

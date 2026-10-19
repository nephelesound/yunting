// One implementation of the song-detail fetch: batching, one callback, and the
// failure of any batch failing the whole result. Local TCP server only.
#include "sdk/track_service.h"

#include "ncmclient.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

using namespace ncm::sdk;

class FakeSongDetail : public QTcpServer {
public:
    QList<QJsonArray> batches; // ids in each request, in arrival order
    int failBatch = -1;        // request index that answers a business failure
    int requests = 0;

    explicit FakeSongDetail(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = buffer->left(end);
                    buffer->clear();
                    const QByteArray target =
                        head.left(head.indexOf('\n')).trimmed().split(' ').value(1);
                    const qsizetype question = target.indexOf('?');
                    const QUrlQuery query(QString::fromLatin1(
                        question < 0 ? QByteArray() : target.mid(question + 1)));
                    const QJsonArray ids = QJsonDocument::fromJson(
                        query.queryItemValue(QStringLiteral("c")).toUtf8()).array();
                    batches.append(ids);
                    ++requests;

                    QByteArray body;
                    if (requests - 1 == failBatch) {
                        body = "{\"code\":500,\"message\":\"boom\"}";
                    } else {
                        QJsonArray songs;
                        for (const QJsonValue &value : ids) {
                            const qint64 id =
                                static_cast<qint64>(value.toObject().value(QStringLiteral("id")).toDouble());
                            songs.append(QJsonObject{
                                {"id", static_cast<double>(id)},
                                {"name", QStringLiteral("song-%1").arg(id)},
                                {"dt", 1000.0},
                                {"ar", QJsonArray{}},
                                {"al", QJsonObject{}},
                            });
                        }
                        body = QJsonDocument(QJsonObject{{"code", 200}, {"songs", songs}})
                                   .toJson(QJsonDocument::Compact);
                    }
                    socket->write("HTTP/1.1 200 x\r\n"
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

class SdkTrackServiceTest : public QObject {
    Q_OBJECT

    static bool waitFor(const std::function<bool()> &done, int timeout = 3000)
    {
        QDeadlineTimer deadline(timeout);
        while (!done() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    }

    static QList<qint64> ids(int count)
    {
        QList<qint64> out;
        out.reserve(count);
        for (int i = 0; i < count; ++i)
            out.append(1000 + i);
        return out;
    }

private slots:
    void moreThan250IdsUseSeveralRequestsAndOneCallback()
    {
        FakeSongDetail api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));

        int callbacks = 0;
        std::optional<Result<QList<Track>>> result;
        fetchSongDetails(client, ids(300), false, nullptr,
                         [&](Result<QList<Track>> value) {
            ++callbacks;
            result = std::move(value);
        });
        QVERIFY(waitFor([&] { return result.has_value(); }));

        QCOMPARE(callbacks, 1);
        QCOMPARE(api.requests, 2);
        QList<int> sizes;
        for (const QJsonArray &batch : std::as_const(api.batches))
            sizes.append(static_cast<int>(batch.size()));
        std::sort(sizes.begin(), sizes.end());
        QCOMPARE(sizes, (QList<int>{50, 250}));
        QVERIFY(result->ok());
        QCOMPARE(result->value->size(), 300);
        QCOMPARE(result->value->first().id, qint64(1000));
        QCOMPARE(result->value->last().id, qint64(1299));
    }

    void oneFailingBatchFailsTheWholeResult()
    {
        FakeSongDetail api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.failBatch = 1;
        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));

        int callbacks = 0;
        std::optional<Result<QList<Track>>> result;
        fetchSongDetails(client, ids(300), false, nullptr,
                         [&](Result<QList<Track>> value) {
            ++callbacks;
            result = std::move(value);
        });
        QVERIFY(waitFor([&] { return result.has_value(); }));

        QCOMPARE(callbacks, 1);
        QCOMPARE(api.requests, 2);
        QVERIFY(!result->ok());
        QCOMPARE(result->error.kind, ErrorKind::Api);
        QCOMPARE(result->error.code, 500);
        QCOMPARE(result->error.endpoint, QStringLiteral("api/v3/song/detail"));
    }
};

QTEST_GUILESS_MAIN(SdkTrackServiceTest)
#include "sdk_track_service_test.moc"

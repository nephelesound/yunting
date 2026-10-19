// CloudUploader end-to-end against a local HTTP stub: check -> token -> LBS ->
// raw upload -> info -> publish. No real account or network.
#include "clouduploader.h"

#include "ncmclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>
#include <algorithm>

class CloudUploadTest : public QObject {
    Q_OBJECT

private slots:
    void uploadsThroughWebPipeline()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        QStringList paths;

        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    const QByteArray request = socket->readAll();
                    const QList<QByteArray> line = request.split('\n').value(0).split(' ');
                    const QString path = QString::fromLatin1(line.value(1));
                    paths.append(path);

                    QByteArray body;
                    if (path.startsWith(QStringLiteral("/api/cloud/upload/check"))) {
                        body = R"({"code":200,"needUpload":true,"songId":0})";
                    } else if (path.startsWith(QStringLiteral("/weapi/nos/token/alloc"))) {
                        body = R"({"code":200,"result":{"objectKey":"obj/1.mp3","token":"tok","resourceId":"res"}})";
                    } else if (path.startsWith(QStringLiteral("/lbs"))) {
                        const QString host = QStringLiteral("http://127.0.0.1:%1")
                                                 .arg(server.serverPort());
                        body = QStringLiteral(R"({"upload":["%1"]})").arg(host).toUtf8();
                    } else if (path.startsWith(QStringLiteral(
                                   "/jd-musicrep-privatecloud-audio-public/"))) {
                        body = QByteArray();
                    } else if (path.startsWith(QStringLiteral("/api/upload/cloud/info/v2"))) {
                        body = R"({"code":200,"songId":12345})";
                    } else if (path.startsWith(QStringLiteral("/api/cloud/pub/v2"))) {
                        body = R"({"code":200})";
                    } else {
                        body = R"({"code":404})";
                    }

                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
                    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                    response += "Connection: close\r\n\r\n";
                    response += body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });

        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(file.write(QByteArray(1024, 'a')) == 1024);
        QVERIFY(file.flush());

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort()));

        CloudUploader uploader(&client);
        uploader.setTestEndpoints(
            QUrl(QStringLiteral("http://127.0.0.1:%1/lbs").arg(server.serverPort())),
            QUrl());

        QSignalSpy finished(&uploader, &CloudUploader::finished);
        QSignalSpy failed(&uploader, &CloudUploader::failed);
        uploader.upload(file.fileName());

        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(finished.first().first().toDouble(), 12345.0);

        const QStringList expected{
            QStringLiteral("/api/cloud/upload/check"),
            QStringLiteral("/weapi/nos/token/alloc"),
            QStringLiteral("/lbs"),
            QStringLiteral("/jd-musicrep-privatecloud-audio-public/obj%2F1.mp3"),
            QStringLiteral("/api/upload/cloud/info/v2"),
            QStringLiteral("/api/cloud/pub/v2"),
        };
        for (const QString &path : expected) {
            const bool found = std::any_of(paths.cbegin(), paths.cend(),
                                           [&path](const QString &seen) {
                return seen.startsWith(path);
            });
            QVERIFY2(found, qPrintable(path + " missing from " + paths.join(", ")));
        }
    }

    void rejectsMissingFileWithoutRequest()
    {
        NcmClient client(nullptr, false);
        CloudUploader uploader(&client);
        QSignalSpy failed(&uploader, &CloudUploader::failed);
        uploader.upload(QStringLiteral("/definitely/not/a/file.mp3"));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    void helperNamesAreStable()
    {
        QCOMPARE(CloudUploader::fileSuffix(QStringLiteral("/tmp/a.FLAC")), QStringLiteral("flac"));
        QCOMPARE(CloudUploader::songNameFromPath(QStringLiteral("/tmp/a b.mp3")),
                 QStringLiteral("a b"));
        QCOMPARE(CloudUploader::objectNameFromPath(QStringLiteral("/tmp/a b.mp3")),
                 QStringLiteral("ab"));
        QCOMPARE(CloudUploader::contentTypeForPath(QStringLiteral("/tmp/a.flac")),
                 QStringLiteral("audio/flac"));
    }
};

QTEST_GUILESS_MAIN(CloudUploadTest)
#include "cloud_upload_test.moc"

// CloudUploader end-to-end against a local HTTP stub. No real account or network.
#include "clouduploader.h"

#include "ncmclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTest>
#include <QUrlQuery>

class CloudUploadTest : public QObject {
    Q_OBJECT

private slots:
    void uploadsOrImports_data()
    {
        QTest::addColumn<QJsonValue>("resourceId");
        QTest::addColumn<bool>("needUpload");
        QTest::addColumn<int>("publishCode");
        QTest::addColumn<int>("importStatus");
        QTest::addColumn<bool>("importConfirmed");

        QTest::newRow("numeric-registration-id")
            << QJsonValue(qint64(123456789012345678)) << true << 200 << 0 << false;
        QTest::newRow("string-registration-id")
            << QJsonValue(QStringLiteral("registration-id")) << true << 201 << 0 << false;
        QTest::newRow("publish-rejected")
            << QJsonValue(QStringLiteral("registration-id")) << true << 523 << 0 << false;
        QTest::newRow("instant-import") << QJsonValue() << false << 0 << 0 << true;
        QTest::newRow("already-in-cloud") << QJsonValue() << false << 0 << 1 << true;
        QTest::newRow("import-forbidden") << QJsonValue() << false << 0 << 2 << false;
        QTest::newRow("import-unconfirmed") << QJsonValue() << false << 0 << 0 << false;
    }

    void uploadsOrImports()
    {
        QFETCH(QJsonValue, resourceId);
        QFETCH(bool, needUpload);
        QFETCH(int, publishCode);
        QFETCH(int, importStatus);
        QFETCH(bool, importConfirmed);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QString host = QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
        const QByteArray audio(1024, 'a');
        const QString checkPath = QStringLiteral("/api/cloud/upload/check");
        const QString importCheckPath = checkPath + QStringLiteral("/v2");
        const QString importPath = QStringLiteral("/api/cloud/user/song/import");
        const QString tokenPath = QStringLiteral("/weapi/nos/token/alloc");
        const QString lbsPath = QStringLiteral("/lbs");
        const QString uploadPath = QStringLiteral("/jd-musicrep-privatecloud-audio-public/obj%2F1.mp3");
        const QString infoPath = QStringLiteral("/api/upload/cloud/info/v2");
        const QString publishPath = QStringLiteral("/weapi/cloud/pub/v2");
        const QString checkId = QStringLiteral("opaque-check-id");
        const QString importId = QStringLiteral("opaque-import-id");
        QStringList paths;
        QByteArray uploadedAudio;
        QByteArray uploadToken;
        QUrlQuery infoForm;
        QUrlQuery importCheckForm;
        QUrlQuery importForm;
        int tokenRequests = 0;

        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket,
                    [&, socket, request = QByteArray()]() mutable {
                        request += socket->readAll();
                        const qsizetype headerEnd = request.indexOf("\r\n\r\n");
                        if (headerEnd < 0)
                            return;
                        const QByteArray headers = request.left(headerEnd);
                        qsizetype contentLength = 0;
                        for (const auto &header : headers.split('\n')) {
                            if (header.toLower().startsWith("content-length:"))
                                contentLength = header.mid(15).trimmed().toLongLong();
                        }
                        if (request.size() < headerEnd + 4 + contentLength)
                            return;
                        const QByteArray payload = request.mid(headerEnd + 4, contentLength);
                        const QByteArray target = headers.split('\n').first().split(' ').value(1);
                        const QString path = QString::fromLatin1(target.split('?').first());
                        paths.append(path);

                        QJsonObject reply{{"code", 200}};
                        if (path == checkPath) {
                            reply.insert("needUpload", needUpload);
                            if (needUpload)
                                reply.insert("songId", checkId);
                        } else if (path == tokenPath) {
                            if (++tokenRequests == 1) {
                                // A numeric resource ID triggered "上传凭据不完整" in 0.6.38,
                                // even when the response also supplied an object key and token.
                                reply.insert("result", QJsonObject{{"resourceId", resourceId},
                                    {"objectKey", "unused-key"}, {"token", "unused-token"}});
                            } else {
                                QJsonObject transfer{{"objectKey", "obj/1.mp3"}, {"token", "audio-token"}};
                                if (resourceId.isString())
                                    transfer.insert("resourceId", "different-transfer-id");
                                reply.insert("result", transfer);
                            }
                        } else if (path == lbsPath) {
                            reply = QJsonObject{{"upload", QJsonArray{host}}};
                        } else if (path == uploadPath) {
                            uploadedAudio = payload;
                            for (const auto &header : headers.split('\n')) {
                                if (header.toLower().startsWith("x-nos-token:"))
                                    uploadToken = header.mid(12).trimmed();
                            }
                        } else if (path == infoPath) {
                            infoForm = QUrlQuery(QString::fromUtf8(payload));
                            reply.insert("songId", QStringLiteral("12345"));
                        } else if (path == publishPath) {
                            reply.insert("code", publishCode);
                        } else if (path == importCheckPath) {
                            importCheckForm = QUrlQuery(QString::fromUtf8(payload));
                            reply.insert("data", QJsonArray{QJsonObject{
                                {"upload", importStatus}, {"songId", importId}}});
                        } else if (path == importPath) {
                            importForm = QUrlQuery(QString::fromUtf8(payload));
                            QJsonArray successful;
                            if (importConfirmed)
                                successful.append(QJsonObject{{"songId", importId},
                                    {"song", QJsonObject{{"songId", "67890"}}}});
                            reply.insert("data", QJsonObject{{"successSongs", successful}});
                        } else {
                            reply.insert("code", 404);
                        }

                        const QByteArray body = path == uploadPath ? QByteArray()
                            : QJsonDocument(reply).toJson(QJsonDocument::Compact);
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                            "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                            "Connection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
            }
        });

        QTemporaryFile file;
        QVERIFY(file.open());
        QCOMPARE(file.write(audio), audio.size());
        QVERIFY(file.flush());

        NcmClient client(nullptr, false);
        client.setApiBase(host + QLatin1Char('/'));
        CloudUploader uploader(&client);
        uploader.setTestEndpoints(QUrl(host + lbsPath));

        QSignalSpy finished(&uploader, &CloudUploader::finished);
        QSignalSpy failed(&uploader, &CloudUploader::failed);
        uploader.upload(file.fileName());

        QTRY_COMPARE_WITH_TIMEOUT(finished.count() + failed.count(), 1, 5000);
        const bool success = needUpload ? publishCode == 200 || publishCode == 201 : importConfirmed;
        const QString error = failed.isEmpty() ? QString() : failed.first().first().toString();
        QVERIFY2(finished.count() == (success ? 1 : 0), qPrintable(error));
        QCOMPARE(failed.count(), success ? 0 : 1);
        if (success)
            QCOMPARE(finished.first().first().toDouble(), needUpload ? 12345.0 : 67890.0);
        QVERIFY(!uploader.busy());

        if (needUpload) {
            const QStringList expected{checkPath, tokenPath, tokenPath, lbsPath,
                                       uploadPath, infoPath, publishPath};
            QCOMPARE(paths, expected);
            QCOMPARE(uploadedAudio, audio);
            QCOMPARE(uploadToken, QByteArray("audio-token"));
            QCOMPARE(infoForm.queryItemValue(QStringLiteral("songid")), checkId);
            QCOMPARE(infoForm.queryItemValue(QStringLiteral("resourceId")),
                     resourceId.isString() ? resourceId.toString()
                                           : QString::number(resourceId.toInteger()));
            if (!success)
                QVERIFY(error.contains(QString::number(publishCode)));
            return;
        }

        QStringList expected{checkPath, importCheckPath};
        if (importStatus != 2)
            expected.append(importPath);
        QCOMPARE(paths, expected);
        const auto checked = songsInForm(importCheckForm);
        QCOMPARE(checked.size(), 1);
        QCOMPARE(checked.first().toObject().value("songId").toInt(), -2);
        QCOMPARE(checked.first().toObject().value("fileSize").toInteger(), audio.size());
        if (importStatus != 2) {
            const auto imported = songsInForm(importForm);
            QCOMPARE(imported.size(), 1);
            QCOMPARE(imported.first().toObject().value("songId").toString(), importId);
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

private:
    static QJsonArray songsInForm(const QUrlQuery &form)
    {
        return QJsonDocument::fromJson(
            form.queryItemValue(QStringLiteral("songs"), QUrl::FullyDecoded).toUtf8()).array();
    }
};

QTEST_GUILESS_MAIN(CloudUploadTest)
#include "cloud_upload_test.moc"

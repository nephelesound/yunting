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
#include <functional>

namespace {

const QString CheckPath = QStringLiteral("/api/cloud/upload/check");
const QString ImportCheckPath = QStringLiteral("/api/cloud/upload/check/v2");
const QString ImportPath = QStringLiteral("/api/cloud/user/song/import");
const QString TokenPath = QStringLiteral("/weapi/nos/token/alloc");
const QString LbsPath = QStringLiteral("/lbs");
const QString UploadPath = QStringLiteral("/jd-musicrep-privatecloud-audio-public/obj%2F1.mp3");
const QString InfoPath = QStringLiteral("/api/upload/cloud/info/v2");
const QString PublishPath = QStringLiteral("/weapi/cloud/pub/v2");
const QString CheckId = QStringLiteral("opaque-check-id");
const QString ImportId = QStringLiteral("opaque-import-id");

struct Request {
    QString path;
    QByteArray headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        const QByteArray prefix = name.toLower() + ':';
        for (const QByteArray &line : headers.split('\n')) {
            if (line.toLower().startsWith(prefix))
                return line.mid(prefix.size()).trimmed();
        }
        return {};
    }

    QUrlQuery form() const { return QUrlQuery(QString::fromUtf8(body)); }
};

// Answers every request with `route(request)` as a one-shot HTTP/1.1 response
// and records the paths in arrival order.
class StubServer : public QTcpServer {
public:
    std::function<QJsonObject(const Request &)> route;
    QStringList paths;

    StubServer()
    {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection())
                serve(socket);
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

private:
    void serve(QTcpSocket *socket)
    {
        connect(socket, &QTcpSocket::readyRead, socket,
                [this, socket, buffer = QByteArray()]() mutable {
            buffer += socket->readAll();
            const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            Request request;
            request.headers = buffer.left(headerEnd);
            const qsizetype length = request.header("content-length").toLongLong();
            if (buffer.size() < headerEnd + 4 + length)
                return;
            request.body = buffer.mid(headerEnd + 4, length);
            const QByteArray target = request.headers.split('\n').first().split(' ').value(1);
            request.path = QString::fromLatin1(target.split('?').first());
            paths.append(request.path);

            const QJsonObject reply = route(request);
            const QByteArray body = reply.isEmpty() ? QByteArray()
                                                    : QJsonDocument(reply).toJson(QJsonDocument::Compact);
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

QJsonArray songsInForm(const QUrlQuery &form)
{
    return QJsonDocument::fromJson(
        form.queryItemValue(QStringLiteral("songs"), QUrl::FullyDecoded).toUtf8()).array();
}

} // namespace

class CloudUploadTest : public QObject {
    Q_OBJECT

private slots:
    void uploads_data()
    {
        QTest::addColumn<QJsonValue>("resourceId");
        QTest::addColumn<int>("publishCode");

        QTest::newRow("numeric-resource-id") << QJsonValue(qint64(123456789012345678)) << 200;
        QTest::newRow("string-resource-id") << QJsonValue(QStringLiteral("resource-id")) << 201;
        QTest::newRow("publish-rejected") << QJsonValue(QStringLiteral("resource-id")) << 523;
    }

    void uploads()
    {
        QFETCH(QJsonValue, resourceId);
        QFETCH(int, publishCode);

        StubServer server;
        QVERIFY(server.isListening());
        QByteArray uploadedAudio;
        QByteArray uploadToken;
        QUrlQuery infoForm;
        int tokenRequests = 0;
        server.route = [&](const Request &request) {
            QJsonObject reply{{"code", 200}};
            if (request.path == CheckPath) {
                reply.insert("needUpload", true);
                reply.insert("songId", CheckId);
            } else if (request.path == TokenPath && ++tokenRequests == 1) {
                // A numeric resource ID triggered "上传凭据不完整" in 0.6.38, even
                // when the response also supplied an object key and token.
                reply.insert("result", QJsonObject{{"resourceId", resourceId},
                                                   {"objectKey", "unused-key"},
                                                   {"token", "unused-token"}});
            } else if (request.path == TokenPath) {
                reply.insert("result", QJsonObject{{"resourceId", "transfer-resource-id"},
                                                   {"objectKey", "obj/1.mp3"},
                                                   {"token", "audio-token"}});
            } else if (request.path == LbsPath) {
                reply = QJsonObject{{"upload", QJsonArray{server.url()}}};
            } else if (request.path == UploadPath) {
                uploadedAudio = request.body;
                uploadToken = request.header("x-nos-token");
                return QJsonObject();
            } else if (request.path == InfoPath) {
                infoForm = request.form();
                reply.insert("songId", "12345");
            } else if (request.path == PublishPath) {
                reply.insert("code", publishCode);
            } else {
                reply.insert("code", 404);
            }
            return reply;
        };

        const QByteArray audio(1024, 'a');
        QString error;
        double songId = 0;
        upload(server, audio, &error, &songId);
        if (QTest::currentTestFailed())
            return;

        const QStringList expected{CheckPath, TokenPath, TokenPath, LbsPath,
                                   UploadPath, InfoPath, PublishPath};
        QCOMPARE(server.paths, expected);
        QCOMPARE(uploadedAudio, audio);
        QCOMPARE(uploadToken, QByteArray("audio-token"));
        QCOMPARE(infoForm.queryItemValue(QStringLiteral("songid")), CheckId);
        QCOMPARE(infoForm.queryItemValue(QStringLiteral("resourceId")),
                 resourceId.isString() ? resourceId.toString()
                                       : QString::number(resourceId.toInteger()));
        if (publishCode == 523) {
            QVERIFY2(error.contains(QStringLiteral("523")), qPrintable(error));
        } else {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(songId, 12345.0);
        }
    }

    void imports_data()
    {
        QTest::addColumn<int>("importStatus");
        QTest::addColumn<bool>("importConfirmed");

        QTest::newRow("importable") << 0 << true;
        QTest::newRow("already-in-cloud") << 1 << true;
        QTest::newRow("import-refused") << 2 << false;
        QTest::newRow("import-unconfirmed") << 0 << false;
    }

    void imports()
    {
        QFETCH(int, importStatus);
        QFETCH(bool, importConfirmed);

        StubServer server;
        QVERIFY(server.isListening());
        QUrlQuery importCheckForm;
        QUrlQuery importForm;
        server.route = [&](const Request &request) {
            QJsonObject reply{{"code", 200}};
            if (request.path == CheckPath) {
                reply.insert("needUpload", false);
            } else if (request.path == ImportCheckPath) {
                importCheckForm = request.form();
                reply.insert("data", QJsonArray{QJsonObject{{"upload", importStatus},
                                                            {"songId", ImportId}}});
            } else if (request.path == ImportPath) {
                importForm = request.form();
                QJsonArray successful;
                if (importConfirmed)
                    successful.append(QJsonObject{{"songId", ImportId},
                                                  {"song", QJsonObject{{"songId", "67890"}}}});
                reply.insert("data", QJsonObject{{"successSongs", successful}});
            } else {
                reply.insert("code", 404);
            }
            return reply;
        };

        const QByteArray audio(1024, 'a');
        QString error;
        double songId = 0;
        upload(server, audio, &error, &songId);
        if (QTest::currentTestFailed())
            return;

        const bool refused = importStatus == 2;
        QStringList expected{CheckPath, ImportCheckPath};
        if (!refused)
            expected.append(ImportPath);
        QCOMPARE(server.paths, expected);
        const QJsonObject checked = songsInForm(importCheckForm).first().toObject();
        QCOMPARE(checked.value("songId").toInt(), -2);
        QCOMPARE(checked.value("fileSize").toInteger(), audio.size());
        if (!refused) {
            const QJsonArray imported = songsInForm(importForm);
            QCOMPARE(imported.size(), 1);
            QCOMPARE(imported.first().toObject().value("songId").toString(), ImportId);
        }
        if (importConfirmed) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            QCOMPARE(songId, 67890.0);
        } else {
            QVERIFY(!error.isEmpty());
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
    // Uploads `audio` through `server` and reports exactly one outcome: the
    // failure message, or the song ID on success.
    static void upload(StubServer &server, const QByteArray &audio, QString *error, double *songId)
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QCOMPARE(file.write(audio), audio.size());
        QVERIFY(file.flush());

        NcmClient client(nullptr, false);
        client.setApiBase(server.url() + QLatin1Char('/'));
        CloudUploader uploader(&client);
        uploader.setTestLbs(QUrl(server.url() + LbsPath));

        QSignalSpy finished(&uploader, &CloudUploader::finished);
        QSignalSpy failed(&uploader, &CloudUploader::failed);
        uploader.upload(file.fileName());

        QTRY_COMPARE_WITH_TIMEOUT(finished.count() + failed.count(), 1, 5000);
        QVERIFY(!uploader.busy());
        if (!failed.isEmpty())
            *error = failed.first().first().toString();
        else
            *songId = finished.first().first().toDouble();
    }
};

QTEST_GUILESS_MAIN(CloudUploadTest)
#include "cloud_upload_test.moc"

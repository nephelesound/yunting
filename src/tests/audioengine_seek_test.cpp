// AudioEngine 的跳转归属测试：切歌前留下的 seek 只能作用于那一首，不能落到下一首。
// 本地 QTcpServer 同时扮演取址接口与音频文件，不碰外网。
#include "../audioengine.h"
#include "../ncmclient.h"

#include <QDataStream>
#include <QDeadlineTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

namespace {
// 3 秒、8 kHz、单声道、8 位 PCM 的静音 WAV：足够长，能区分 1 秒与 0 秒的起点
QByteArray silentWav()
{
    constexpr int Rate = 8000;
    constexpr int Seconds = 3;
    const QByteArray pcm(Rate * Seconds, '\x80');
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(Rate)
           << quint32(Rate) << quint16(1) << quint16(8);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    return out;
}

class MediaServer : public QTcpServer {
public:
    // path → (Content-Type, body)；只服务 GET
    std::function<QPair<QByteArray, QByteArray>(const QByteArray &path)> handler;

    explicit MediaServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *sock = nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                    const QByteArray request = sock->readAll();
                    const QByteArray firstLine = request.left(request.indexOf('\n')).trimmed();
                    const QByteArray target = firstLine.split(' ').value(1);
                    const qsizetype queryAt = target.indexOf('?');
                    const QByteArray path = queryAt < 0 ? target : target.left(queryAt);
                    const auto response = handler ? handler(path)
                                                  : qMakePair(QByteArray("application/json"),
                                                              QByteArray("{\"code\":200}"));
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: " + response.first
                                + "\r\nContent-Length: " + QByteArray::number(response.second.size())
                                + "\r\nConnection: close\r\n\r\n" + response.second);
                    sock->flush();
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            }
        });
    }
};
} // namespace

class AudioEngineSeekTest : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("XDG_DATA_HOME", m_dir.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_dir.filePath("cache").toUtf8());
        qputenv("YUNTING_KEYCHAIN_SERVICE", "yunting-test");
    }

    void pendingSeekDoesNotLeakToNextTrack()
    {
        MediaServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());
        const QByteArray wav = silentWav();
        const QByteArray urlJson = QJsonDocument(QJsonObject{
            {"code", 200},
            {"data", QJsonArray{QJsonObject{{"url", base + QStringLiteral("audio.wav")}, {"expi", 1200}}}}})
            .toJson(QJsonDocument::Compact);

        server.handler = [&server, wav, urlJson](const QByteArray &path) {
            Q_UNUSED(server);
            if (path.startsWith("/api/song/enhance/player/url"))
                return qMakePair(QByteArray("application/json"), urlJson);
            return qMakePair(QByteArray("audio/wav"), wav);
        };

        NcmClient client(nullptr, false);
        client.setApiBase(base);
        AudioEngine audio(&client);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 1.0}, {"dur", 3.0}});
        audio.seek(1.0);   // 地址还没到：跳转挂起，只属于第 1 首
        audio.setTrack(QVariantMap{{"id", 2.0}, {"dur", 3.0}});
        audio.setPlaying(true);

        QDeadlineTimer deadline(8 * 1000);
        while (audio.position() <= 0.05 && failedSpy.isEmpty() && !deadline.hasExpired())
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        QVERIFY2(failedSpy.isEmpty(), "playback failed; cannot observe the pending seek");
        QVERIFY2(audio.position() > 0.05, "playback did not start");
        QVERIFY2(audio.position() < 0.6, "the previous track's seek leaked into the next track");
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(AudioEngineSeekTest)
#include "audioengine_seek_test.moc"

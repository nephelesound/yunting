// AudioEngine 的行为测试：公开属性（默认值、越界钳制、同值不重复 NOTIFY）与真实 libmpv 播放。
// 播放用测试自己生成的本地 WAV，ao=null，不碰声卡；取址接口是本机 QTcpServer，不碰网络。
#include "../audioengine.h"
#include "../ncmclient.h"

#include <QDataStream>
#include <QDeadlineTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <functional>

namespace {
// 静音 WAV：8 kHz 单声道 8 位，位置会推进但没有声音
bool writeSilentWav(const QString &path, int seconds, int rate = 8000)
{
    const QByteArray pcm(rate * seconds, '\x80');
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + pcm.size());
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(1) << quint32(rate)
           << quint32(rate) << quint16(1) << quint16(8);
    stream.writeRawData("data", 4);
    stream << quint32(pcm.size());
    stream.writeRawData(pcm.constData(), pcm.size());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(out) == out.size();
}

// 只扮演 song/enhance/player/url/v1：返回调用方给的音频地址
class ApiServer : public QTcpServer {
public:
    QString audioUrl;
    int requests = 0;

    explicit ApiServer(QObject *parent = nullptr) : QTcpServer(parent)
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *sock = nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                    sock->readAll();
                    ++requests;
                    const QByteArray json = QJsonDocument(QJsonObject{
                        {"code", 200},
                        {"data", QJsonArray{QJsonObject{{"url", audioUrl}, {"expi", 1200}}}}})
                        .toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(json.size())
                                + "\r\nConnection: close\r\n\r\n" + json);
                    sock->flush();
                    sock->disconnectFromHost();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            }
        });
    }
};

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 8000)
{
    QDeadlineTimer deadline(timeoutMs);
    while (!condition() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return condition();
}
} // namespace

class AudioEngineTest : public QObject {
    Q_OBJECT

    // NcmClient 在构造时读 XDG_DATA_HOME，先指到临时目录再建
    NcmClient *client(const QTemporaryDir &dir)
    {
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", dir.filePath("cache").toUtf8());
        return new NcmClient(this);
    }
    AudioEngine *engine(const QTemporaryDir &dir) { return new AudioEngine(client(dir), this); }

private slots:
    void init()
    {
        // 测试不碰真实声卡：AudioEngine 构造时读这个变量
        qputenv("YUNTING_TEST_AO_NULL", "1");
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
        qputenv("XDG_DATA_HOME", m_dir.filePath("data").toUtf8());
        qputenv("XDG_CACHE_HOME", m_dir.filePath("cache").toUtf8());
        qputenv("YUNTING_KEYCHAIN_SERVICE", "yunting-test");
    }

    void defaultsMatchDesign()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        QCOMPARE(audio->volume(), 0.7);
        QVERIFY(!audio->muted());
        QVERIFY(audio->eqAvailable());
    }

    void volumeIsClampedToUnitRange()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        audio->setVolume(1.5);
        QCOMPARE(audio->volume(), 1.0);
        audio->setVolume(-0.2);
        QCOMPARE(audio->volume(), 0.0);
        audio->setVolume(0.42);
        QCOMPARE(audio->volume(), 0.42);
    }

    void settersNotifyOnlyOnChange()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        QSignalSpy volumeSpy(audio, &AudioEngine::volumeChanged);
        QSignalSpy mutedSpy(audio, &AudioEngine::mutedChanged);

        audio->setVolume(0.9);
        QCOMPARE(volumeSpy.count(), 1);
        audio->setVolume(0.9);
        QCOMPARE(volumeSpy.count(), 1);   // 同值不发
        audio->setVolume(1.9);            // 钳到 1.0：值变了，要发
        QCOMPARE(volumeSpy.count(), 2);
        QCOMPARE(audio->volume(), 1.0);

        audio->setMuted(true);
        QCOMPARE(mutedSpy.count(), 1);
        QVERIFY(audio->muted());
        audio->setMuted(true);
        QCOMPARE(mutedSpy.count(), 1);    // 同值不发
        audio->setMuted(false);
        QCOMPARE(mutedSpy.count(), 2);
        QVERIFY(!audio->muted());
    }

    void equalizerGainsAreClampedAndReset()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        QCOMPARE(audio->eqGains().size(), 10);
        QSignalSpy gainsSpy(audio, &AudioEngine::eqGainsChanged);
        audio->setEqGain(0, 99);
        QCOMPARE(audio->eqGains().at(0).toDouble(), 12.0);
        QCOMPARE(gainsSpy.count(), 1);
        audio->setEqGain(0, 12.0);
        QCOMPARE(gainsSpy.count(), 1);   // 同值不发
        audio->setEqGain(10, -3);         // 越界 band 忽略
        QCOMPARE(gainsSpy.count(), 1);
        audio->resetEq();
        QCOMPARE(audio->eqGains().at(0).toDouble(), 0.0);
        QCOMPARE(gainsSpy.count(), 2);
    }

    void playingAdvancesPositionAndReportsDuration()
    {
        QVERIFY(writeSilentWav(m_dir.filePath("long.wav"), 3));
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("long.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 1.0}, {"dur", 3.0}});
        audio.setPlaying(true);
        QVERIFY2(waitFor([&] { return audio.position() > 0.1; }), "playback did not start");
        QVERIFY(failedSpy.isEmpty());
        QVERIFY(audio.duration() > 2.9);
        QVERIFY(audio.duration() < 3.1);
    }

    void seekLandsAndEmitsSeeked()
    {
        QVERIFY(writeSilentWav(m_dir.filePath("long.wav"), 3));
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("long.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy seekedSpy(&audio, &AudioEngine::seeked);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 2.0}, {"dur", 3.0}});
        audio.setPlaying(true);
        QVERIFY(waitFor([&] { return audio.position() > 0.1; }));
        audio.seek(1.5);
        QCOMPARE(seekedSpy.count(), 1);
        QVERIFY(waitFor([&] { return audio.position() > 1.4 && audio.position() < 2.5; }));
        QVERIFY(failedSpy.isEmpty());
    }

    void endOfFileEmitsFinished()
    {
        QVERIFY(writeSilentWav(m_dir.filePath("short.wav"), 1));
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("short.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy finishedSpy(&audio, &AudioEngine::finished);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 3.0}, {"dur", 1.0}});
        audio.setPlaying(true);
        QVERIFY2(waitFor([&] { return finishedSpy.count() > 0; }), "end of file did not emit finished");
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(!audio.playing());
        QVERIFY(failedSpy.isEmpty());
    }

    void qualityChangeReloadsCurrentTrack()
    {
        QVERIFY(writeSilentWav(m_dir.filePath("long.wav"), 3));
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("long.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 6.0}, {"dur", 3.0}});
        audio.setPlaying(true);
        QVERIFY(waitFor([&] { return audio.position() > 0.1; }));
        QVERIFY(waitFor([&] { return api.requests == 1; }));

        audio.setQuality(QStringLiteral("higher"));
        QCOMPARE(audio.quality(), QStringLiteral("higher"));
        QVERIFY2(waitFor([&] { return api.requests == 2; }), "quality change did not refetch the URL");
        QVERIFY2(waitFor([&] { return audio.position() > 0.1; }), "quality change did not resume playback");
        QVERIFY(audio.playing());
        QVERIFY(failedSpy.isEmpty());
    }

    void missingFileEmitsFailed()
    {
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("missing.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 4.0}, {"dur", 3.0}});
        audio.setPlaying(true);
        QVERIFY2(waitFor([&] { return failedSpy.count() > 0; }), "missing file did not fail");
        QCOMPARE(failedSpy.takeFirst().at(0).toString(), QStringLiteral("暂时无法播放这首歌"));
        QVERIFY(!audio.playing());   // 失败后界面不能停在「播放中」
    }

    void eqChangeDuringPlaybackKeepsPlaying()
    {
        QVERIFY(writeSilentWav(m_dir.filePath("long.wav"), 3));
        ApiServer api;
        QVERIFY(api.listen(QHostAddress::LocalHost));
        api.audioUrl = QUrl::fromLocalFile(m_dir.filePath("long.wav")).toString();

        NcmClient client(nullptr, false);
        client.setApiBase(QStringLiteral("http://127.0.0.1:%1/").arg(api.serverPort()));
        AudioEngine audio(&client);
        QSignalSpy failedSpy(&audio, &AudioEngine::failed);

        audio.setTrack(QVariantMap{{"id", 5.0}, {"dur", 3.0}});
        audio.setPlaying(true);
        QVERIFY(waitFor([&] { return audio.position() > 0.1; }));
        const double before = audio.position();

        audio.setEqGain(0, 6);
        audio.setBassBoost(3);
        audio.setStereoWidth(1.4);
        audio.applyEqPreset(QStringLiteral("rock"));

        QVERIFY(audio.playing());
        QVERIFY2(waitFor([&] { return audio.position() > before + 0.05; }), "playback stopped after EQ change");
        QVERIFY(failedSpy.isEmpty());
    }

    // 「跟随系统默认」只有一个入口：菜单里的「系统默认」（空 id）。mpv 自己的 auto 项不进列表，
    // 否则菜单里会有两个「默认」，选了 auto 时「系统默认」也不打勾
    void audioOutputsLeaveTheDefaultToTheMenu()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        for (const QVariant &value : audio->audioOutputs()) {
            const QVariantMap entry = value.toMap();
            QVERIFY(!entry.value("id").toString().isEmpty());
            QVERIFY(entry.value("id").toString() != QLatin1String("auto"));
        }
        audio->setOutputDevice(QStringLiteral("auto"));
        QCOMPARE(audio->outputDevice(), QString());
        audio->setOutputDevice(QString());
        QCOMPARE(audio->outputDevice(), QString());
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(AudioEngineTest)
#include "audioengine_test.moc"

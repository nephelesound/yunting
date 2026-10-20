// VoicePlayer 走真实 libmpv（ao=null）与本地 WAV：「在放哪一条」只在真正空闲时回到空，
// 换一条时旧那条的 end-file 不能把新的一条清掉
#include "voiceplayer.h"

#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {
// 8 kHz、单声道、8 位 PCM 的静音 WAV
QByteArray silentWav(double seconds)
{
    constexpr int Rate = 8000;
    const QByteArray pcm(int(Rate * seconds), '\x80');
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
} // namespace

class VoicePlayerTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;

    QString clip(const QString &name, double seconds)
    {
        QFile file(m_dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(silentWav(seconds)) < 0)
            return {};
        return QUrl::fromLocalFile(file.fileName()).toString();
    }

private slots:
    void initTestCase()
    {
        qputenv("YUNTING_TEST_AO_NULL", "1");
        QVERIFY(m_dir.isValid());
    }

    void playsToTheEndThenIdles()
    {
        VoicePlayer player;
        const QString url = clip("short.wav", 1.5);
        player.toggle(url);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0, 5000);   // 真的放起来了，没被旧的 idle 清掉
        QCOMPARE(player.source(), url);
        QTRY_COMPARE_WITH_TIMEOUT(player.source(), QString(), 5000);
        QCOMPARE(player.position(), 0.0);
    }

    void togglingThePlayingClipStops()
    {
        VoicePlayer player;
        const QString url = clip("long-a.wav", 3);
        player.toggle(url);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0, 5000);
        player.toggle(url);
        QCOMPARE(player.source(), QString());
    }

    void switchingClipsKeepsTheNewOne()
    {
        VoicePlayer player;
        const QString first = clip("long-b.wav", 3);
        const QString second = clip("long-c.wav", 3);
        player.toggle(first);
        QTRY_VERIFY_WITH_TIMEOUT(player.position() > 0, 5000);
        player.toggle(second);
        QTest::qWait(500);   // 旧那条的 end-file 在这期间到达
        QCOMPARE(player.source(), second);
    }

    void missingFileFallsBackToIdle()
    {
        VoicePlayer player;
        const QString url = QUrl::fromLocalFile(m_dir.filePath("missing.wav")).toString();
        player.toggle(url);
        QTRY_COMPARE_WITH_TIMEOUT(player.source(), QString(), 5000);
    }
};

QTEST_GUILESS_MAIN(VoicePlayerTest)
#include "voiceplayer_test.moc"

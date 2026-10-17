// AudioEngine 音量/静音的行为测试：只走公开属性（默认值、越界钳制、同值不重复 NOTIFY）。
// 不取播放地址，不碰网络；cookie jar 指到临时目录，绝不读写用户真实数据。
#include "../audioengine.h"
#include "../ncmclient.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    void defaultsMatchDesign()
    {
        QTemporaryDir dir;
        AudioEngine *audio = engine(dir);
        QCOMPARE(audio->volume(), 0.7);
        QVERIFY(!audio->muted());
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
};

QTEST_GUILESS_MAIN(AudioEngineTest)
#include "audioengine_test.moc"

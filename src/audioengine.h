#pragma once
// AudioEngine：声音接缝（prototype/Playback.qml 消费）。接口与 SimEngine.qml 逐字段一致：
// track（Playback 设置即开始取播放地址）· playing · position / duration（秒）。
// 队列与「下一首」不在这里：自然播完只发 finished()，切歌规则只有 Playback 那一份。
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>

class NcmClient;
class QAudioOutput;
class QMediaPlayer;

class AudioEngine : public QObject {
    Q_OBJECT
    // 当前曲目（QVariantMap，同 snapshot.py 的 track 形状）；null = 没有加载
    Q_PROPERTY(QVariantMap track READ track WRITE setTrack NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
public:
    explicit AudioEngine(NcmClient *client, QObject *parent = nullptr);

    QVariantMap track() const { return m_track; }
    bool playing() const { return m_playing; }
    double position() const;
    // 曲目未加载完时用 track.dur（SimEngine 同款回退）
    double duration() const;

    void setTrack(const QVariantMap &track);
    void setPlaying(bool playing);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);

    // --smoke-audio：无窗口地取地址并播放 seconds 秒，打印状态变化
    int smokeTest(double songId, int seconds);

signals:
    void trackChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    // 自然播完
    void finished();
    // 取不到播放地址等；reason 是给用户看的中文句子
    void failed(const QString &reason);

private:
    // fee=8 免费曲匿名也能拿 320k；data[0].url 为空即 failed
    void resolveUrl(double songId);

    NcmClient *m_client;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVariantMap m_track;
    bool m_playing = false;
    bool m_failed = false;
    double m_pendingSeek = -1;
};

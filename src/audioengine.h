#pragma once
// AudioEngine：声音接缝（prototype/Playback.qml 消费）。接口与 SimEngine.qml 逐字段一致：
// track（Playback 设置即开始取播放地址）· playing · position / duration（秒）· volume / muted。
// volume 用对数（滑条）刻度：0–1 感知线性，喂给 QAudioOutput 前转线性刻度，
// 否则滑条前半段几乎听不出变化。muted 与音量互相独立，不互相改写。
// 队列与「下一首」不在这里：自然播完只发 finished()，切歌规则只有 Playback 那一份。
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
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
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
public:
    explicit AudioEngine(NcmClient *client, QObject *parent = nullptr);

    QVariantMap track() const { return m_track; }
    bool playing() const { return m_playing; }
    double position() const;
    // 曲目未加载完时用 track.dur（SimEngine 同款回退）
    double duration() const;

    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }

    void setTrack(const QVariantMap &track);
    void setPlaying(bool playing);
    void setVolume(double volume);
    void setMuted(bool muted);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);

    // --smoke-audio：无窗口地取地址并播放 seconds 秒，打印状态变化
    int smokeTest(double songId, int seconds);

signals:
    void trackChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void seeked();   // 跳转生效（MPRIS 据此发 Seeked）
    // 自然播完
    void finished();
    // 取不到播放地址等；reason 是给用户看的中文句子
    void failed(const QString &reason);
    // 已向服务器上报 startplay；Store 据此把曲目插到「最近播放」首位
    void scrobbled(const QVariantMap &track);

private:
    // fee=8 免费曲匿名也能拿 320k；data[0].url 为空即 failed
    void resolveUrl(double songId);
    void startSource(const QString &url);
    // 听歌打卡：startplay 写「最近播放」，play 写「听歌排行」；每首歌各一次
    void scrobbleStart();
    void scrobbleFinish();
    void sendWeblog(const char *action, int seconds);

    struct CachedUrl {
        QString url;
        QDateTime expires;
    };
    QHash<double, CachedUrl> m_urls;   // 播放地址缓存，到 expi 前复用
    QSet<double> m_urlRequests;        // 在途的取址请求：同一首歌只发一次，避免重复 startSource

    NcmClient *m_client;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVariantMap m_track;
    bool m_playing = false;
    bool m_failed = false;
    // 滑条刻度（对数感知）；写入 QAudioOutput 前经 convertVolume 转线性
    double m_volume = 0.7;
    bool m_muted = false;
    double m_pendingSeek = -1;
    bool m_scrobbleStarted = false;
    bool m_scrobbleFinished = false;
};

#pragma once
// AudioEngine：声音接缝（prototype/Playback.qml 消费）。接口与 SimEngine.qml 逐字段一致：
// track（Playback 设置即开始取播放地址）· playing · position / duration（秒）· volume / muted。
// volume 用对数（滑条）刻度：0–1 感知线性，喂给 QAudioOutput 前转线性刻度，
// 否则滑条前半段几乎听不出变化。muted 与音量互相独立，不互相改写。
// 队列与「下一首」不在这里：自然播完只发 finished()，切歌规则只有 Playback 那一份。
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class NcmClient;
class QAudioOutput;
class QMediaPlayer;
#ifdef YUNTING_EXPERIMENTAL_EQ
class QAudioBufferOutput;
class EqAudioSink;
#endif

class AudioEngine : public QObject {
    Q_OBJECT
    // 当前曲目（QVariantMap，同 snapshot.py 的 track 形状）；null = 没有加载
    Q_PROPERTY(QVariantMap track READ track WRITE setTrack NOTIFY trackChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    // auto / standard / higher / exhigh / lossless / hires
    Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY qualityChanged)
    // DSP 可用（-DYUNTING_EXPERIMENTAL_EQ=OFF 时为 false，隐藏 EQ UI）；默认播放走系统
    // QAudioOutput，只有 EQ 非平直时才切到自建 QAudioSink，避免主线程忙时拖累普通播放
    Q_PROPERTY(bool eqAvailable READ eqAvailable CONSTANT)
    // 10 段增益（dB），band 0..9 = 31Hz..16kHz
    Q_PROPERTY(QVariantList eqGains READ eqGains NOTIFY eqGainsChanged)
    Q_PROPERTY(double bassBoost READ bassBoost WRITE setBassBoost NOTIFY bassBoostChanged)
    Q_PROPERTY(double stereoWidth READ stereoWidth WRITE setStereoWidth NOTIFY stereoWidthChanged)
    // 空串 = 系统默认输出
    Q_PROPERTY(QString outputDevice READ outputDevice NOTIFY outputDeviceChanged)
    // legacy = /api/feedback/weblog；ncbl = 官方 PC 客户端 PLV/PLD（默认 legacy）
    Q_PROPERTY(QString scrobbleMode READ scrobbleMode WRITE setScrobbleMode NOTIFY scrobbleModeChanged)
public:
    explicit AudioEngine(NcmClient *client, QObject *parent = nullptr);

    QVariantMap track() const { return m_track; }
    bool playing() const { return m_playing; }
    double position() const;
    // 曲目未加载完时用 track.dur（SimEngine 同款回退）
    double duration() const;

    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString quality() const { return m_quality; }
    bool eqAvailable() const
    {
#ifdef YUNTING_EXPERIMENTAL_EQ
        return true;
#else
        return false;
#endif
    }
    QVariantList eqGains() const { return m_eqGains; }
    double bassBoost() const { return m_bassBoost; }
    double stereoWidth() const { return m_stereoWidth; }
    void setBassBoost(double db);
    void setStereoWidth(double width);
    QString outputDevice() const { return m_outputDeviceId; }
    QString scrobbleMode() const { return m_scrobbleMode; }
    void setScrobbleMode(const QString &mode);

    void setTrack(const QVariantMap &track);
    void setPlaying(bool playing);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setQuality(const QString &quality);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);
    // DSP：未启用 YUNTING_EXPERIMENTAL_EQ 时 no-op
    Q_INVOKABLE void setEqGain(int band, double db);
    Q_INVOKABLE void resetEq();
    // flat / pop / rock / bass / vocal / live / spatial
    Q_INVOKABLE void applyEqPreset(const QString &preset);
    // 输出设备：返回 [{id, description}]；id 传回 setOutputDevice
    Q_INVOKABLE QVariantList audioOutputs() const;
    Q_INVOKABLE void setOutputDevice(const QString &id);

    // --smoke-audio：无窗口地取地址并播放 seconds 秒，打印状态变化
    int smokeTest(double songId, int seconds);

signals:
    void trackChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void qualityChanged();
    void eqGainsChanged();
    void bassBoostChanged();
    void stereoWidthChanged();
    void outputDeviceChanged();
    void scrobbleModeChanged();
    void seeked();   // 跳转生效（MPRIS 据此发 Seeked）
    // 自然播完
    void finished();
    // 取不到播放地址等；reason 是给用户看的中文句子
    void failed(const QString &reason);
    // 已向服务器上报 startplay；Store 据此把曲目插到「最近播放」首位
    void scrobbled(const QVariantMap &track);

private:
    // 地址取不到即 failed；quality=auto 时按曲目元数据选档
    void resolveUrl(double songId);
#ifdef YUNTING_EXPERIMENTAL_EQ
    // 平直曲线不需要 DSP：继续用系统 QAudioOutput；任一参数非平直才切到自建 sink。
    void updateEqActive();
    void enableEq();
    void disableEq();
    // 运行中切换输出后端：QMediaPlayer 不会在播放中重挂音频输出，重新打开同一 URL 并回到原位置
    void restartForOutput();
#endif
    void startSource(const QString &url);
    QString effectiveLevel() const;
    // 听歌打卡：startplay 写「最近播放」，play 写「听歌排行」；每首歌各一次
    void scrobbleStart();
    void scrobbleFinish();
    void scrobbleNcbl(int seconds);
    void sendWeblog(const char *action, int seconds);

    struct CachedUrl {
        QString url;
        QDateTime expires;
    };
    QHash<QString, CachedUrl> m_urls;   // songId:level → 地址，到 expi 前复用
    QSet<QString> m_urlRequests;        // 在途的取址（songId:level）：同一首同一音质只发一次，避免重复 startSource

    NcmClient *m_client;
    QMediaPlayer *m_player;
    QAudioOutput *m_audio = nullptr;
#ifdef YUNTING_EXPERIMENTAL_EQ
    QAudioBufferOutput *m_bufferOutput = nullptr;
    EqAudioSink *m_eqSink = nullptr;
    bool m_eqActive = false;
    bool m_eqBroken = false;   // sink 起不来：不再尝试切换，保留系统输出
#endif
    QVariantMap m_track;
    bool m_playing = false;
    bool m_failed = false;
    // 滑条刻度（对数感知）；写入 QAudioOutput 前经 convertVolume 转线性
    double m_volume = 0.7;
    bool m_muted = false;
    QString m_quality = QStringLiteral("auto");
    QString m_outputDeviceId;
    QString m_scrobbleMode = QStringLiteral("legacy");
    QString m_scrobbleRoundMode;   // 开始时锁定，避免中途切模式导致只发一半
    QString m_scrobbleAccountToken; // 开始时锁定账号，换号后旧轮次不再上报
    QVariantList m_eqGains = QVariantList(10, 0.0);
    double m_bassBoost = 0;
    double m_stereoWidth = 1;
    double m_pendingSeek = -1;
    qint64 m_resumePosition = -1;   // 切换输出后端后，等媒体重新加载完再跳回的位置
    bool m_scrobbleStarted = false;
    bool m_scrobbleFinished = false;
    QElapsedTimer m_trackClock;
    // 真实出声时间：暂停不计、seek 不增加；legacy 与 NCBL 都用它结算
    QElapsedTimer m_playClock;
    qint64 m_playedMs = 0;
    bool m_playClockRunning = false;
    bool m_firstAudioLogged = false;
};

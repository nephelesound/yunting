#pragma once
// AudioEngine：声音接缝（prototype/Playback.qml 消费）。接口与 SimEngine.qml 逐字段一致：
// track（Playback 设置即开始取播放地址）· playing · position / duration（秒）· volume / muted。
// volume 是滑条的 0–1，乘 100 即 mpv 的 volume：mpv 的音量本身是三次曲线（感知均匀），不再另做换算。
// muted 与音量互相独立，不互相改写。
// 播放走 libmpv：audio-device=auto 跟随 PipeWire/PulseAudio 的系统默认输出；EQ、低音与
// 空间感是 mpv 的 lavfi 滤镜。队列与「下一首」不在这里：自然播完只发 finished()，
// 切歌规则只有 Playback 那一份。
// 淡入淡出（与网易云相同）：暂停先淡出再停，继续先出声再淡入；正在出声时切歌，上一首淡出、
// 新的一首淡入（新地址在淡出期间照常去取）。自然播完接下一首不淡，免得削掉前奏。
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class MpvController;
class QVariantAnimation;
class NcmClient;
class QTimer;

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
    // 可选的音质档位（auto 在最前）：界面的音质菜单与命令行帮助都从这里取
    Q_PROPERTY(QStringList qualities READ qualities CONSTANT)
    // mpv 自带 lavfi 滤镜，恒可用
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
    ~AudioEngine() override;

    QVariantMap track() const { return m_track; }
    bool playing() const { return m_playing; }
    double position() const;
    // 曲目未加载完时用 track.dur（SimEngine 同款回退）
    double duration() const;

    double volume() const { return m_volume; }
    bool muted() const { return m_muted; }
    QString quality() const { return m_quality; }
    static QStringList qualityLevels();
    QStringList qualities() const { return qualityLevels(); }
    bool eqAvailable() const { return true; }
    QVariantList eqGains() const { return m_eqGains; }
    double bassBoost() const { return m_bassBoost; }
    double stereoWidth() const { return m_stereoWidth; }
    void setBassBoost(double db);
    void setStereoWidth(double width);
    QString outputDevice() const { return m_outputDeviceId; }
    QString scrobbleMode() const { return m_scrobbleMode; }
    void setScrobbleMode(const QString &mode);
    // 收听上报默认关闭；显式允许写入（--allow-write）或界面启动时打开
    void setScrobblingAllowed(bool allowed) { m_scrobblingAllowed = allowed; }

    void setTrack(const QVariantMap &track);
    void setPlaying(bool playing);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setQuality(const QString &quality);

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(double seconds);
    // 预取下一首的播放地址：只填 URL 缓存，不改变当前曲目/播放状态。
    // 智能续播补队后调用，减少切歌时的取址等待。
    Q_INVOKABLE void prefetch(double songId);
    Q_INVOKABLE void setEqGain(int band, double db);
    Q_INVOKABLE void resetEq();
    // flat / pop / rock / bass / vocal / live / spatial
    Q_INVOKABLE void applyEqPreset(const QString &preset);
    // 输出设备：返回 [{id, description}]；id 传回 setOutputDevice，空 id 是「跟随系统默认」
    Q_INVOKABLE QVariantList audioOutputs() const;
    // mpv 的 audio-device-list 换成菜单项：mpv 自己的 auto 就是空 id，不再列一次
    static QVariantList audioOutputsFrom(const QVariant &deviceList);
    Q_INVOKABLE void setOutputDevice(const QString &id);

    // 系统睡眠（SystemSleep）：睡前立即暂停，不淡出（logind 不等）；醒来后保持暂停，由用户继续
    void pauseForSleep();
    // 醒来后重建音频输出：睡前开着的流在 PipeWire/PulseAudio 里可能再也不出声（pipewire#2707），
    // 换歌也复用它，只有重开应用才好。音频服务醒来要一点时间：先等一会儿重建，再补一次（plezy 的做法）。
    // 停在一首歌中间的，下次按播放从原位置重新载入
    void recoverAfterSleep();

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
    // 设备列表变化后重新打开 mpv 的输出：仅靠 audio-device=auto 不会可靠地跟随热插拔
    void reloadAudioOutput();
    // 地址取不到即 failed；quality=auto 时按曲目元数据选档
    void resolveUrl(double songId);
    void startSource(const QString &url, const QString &level, int bitrate);
    QString effectiveLevel() const;
    // 切歌时丢掉上一首挂起的事件（STOP 之外，可能是错误），不让它落到新曲目上
    void discardEvents();
    void handleProperty(const QString &name, const QVariant &value);
    void onFileLoaded();
    void onEndFile(const QString &reason);
    void seekNow(double target);   // 文件已载入：发跳转，落地前进度报目标
    bool seekInFlight() const;
    void setPaused(bool paused);
    // mpv 的音量 = 用户音量 × m_fade。mpv 的音量刻度本身按听感分布，匀速改它听起来就是匀速的淡入淡出。
    // 新的一次淡变接替正在进行的那次，被接替的那次不再执行它的 done
    void fadeTo(double target, int ms, std::function<void()> done = {});
    void applyVolume();
    // 上一首淡出完毕：停掉它，播淡出期间取到的新地址
    void finishSwitch();
    // 滤镜链始终挂在一个带标签的 lavfi 上，运行中改参数只发 af-command
    void applyFilters();
    void updateFilter(const char *target, const char *command, double value);
    void notePlaybackStarted();
    // 听歌打卡：startplay 写「最近播放」，play 写「听歌排行」；每首歌各一次
    void scrobbleStart();
    void scrobbleFinish();
    void scrobbleNcbl(int seconds);
    void sendWeblog(const char *action, int seconds);

    struct CachedUrl {
        QString url;
        QString level;   // 播放地址响应里服务器实际给的档位
        int bitrate = 320000;
        QDateTime expires;
    };
    QHash<QString, CachedUrl> m_urls;   // songId:level → 地址，到 expi 前复用
    QSet<QString> m_urlRequests;        // 在途的取址（songId:level）：同一首同一音质只发一次，避免重复 startSource

    NcmClient *m_client;
    MpvController *m_controller = nullptr;
    QVariantMap m_track;
    bool m_playing = false;
    bool m_failed = false;
    bool m_reloadOnPlay = false;   // 停在歌中间睡醒了：下次按播放重新载入（睡前的网络流已断）
    bool m_urlReady = false;
    bool m_fileLoaded = false;
    bool m_atEof = false;
    bool m_finishedEmitted = false;
    QString m_sourceUrl;
    // 滑条刻度 0–1；写入 mpv 时直接乘 100
    double m_volume = 0.7;
    bool m_muted = false;
    QString m_quality = QStringLiteral("auto");
    QString m_outputDeviceId;
    QTimer *m_audioDeviceDebounce = nullptr;
    QVariant m_deviceList;   // 观察到的 audio-device-list（测试与打包构建里不观察，保持空）
    // 这次播放实际拿到的档位与码率；NCBL 上报用，不是曲目的最高可用档位
    QString m_playedLevel;
    int m_playedBitrate = 320000;
    bool m_scrobblingAllowed = false;
    QString m_scrobbleMode = QStringLiteral("legacy");
    QString m_scrobbleRoundMode;   // 开始时锁定，避免中途切模式导致只发一半
    QString m_scrobbleAccountToken; // 开始时锁定账号，换号后旧轮次不再上报
    QVariantList m_eqGains = QVariantList(10, 0.0);
    double m_bassBoost = 0;
    double m_stereoWidth = 1;
    double m_pendingSeek = -1;      // 秒；等文件加载完再应用
    double m_seekTarget = -1;       // 秒；跳转还没落地时进度先报这里，最多等 kSeekLandMs
    QElapsedTimer m_seekClock;
    // time-pos / duration 在停止或结束后会不可用，缓存最后一个值给 QML
    double m_position = 0;
    double m_duration = 0;
    bool m_scrobbleStarted = false;
    bool m_scrobbleFinished = false;
    QElapsedTimer m_trackClock;
    // 真实出声时间：暂停不计、seek 不增加；legacy 与 NCBL 都用它结算
    QElapsedTimer m_playClock;
    qint64 m_playedMs = 0;
    bool m_playClockRunning = false;
    bool m_firstAudioLogged = false;

    QVariantAnimation *m_fadeAnimation = nullptr;
    double m_fade = 1.0;
    std::function<void()> m_afterFade;
    // 上一首正在淡出：它的事件不再落到新曲目上，新曲目的地址先存在 m_pendingSource
    bool m_switching = false;
    struct PendingSource {
        QString url;
        QString level;
        int bitrate = 320000;
    };
    std::optional<PendingSource> m_pendingSource;
};

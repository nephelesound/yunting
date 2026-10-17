#pragma once
// Mpris：媒体键 / 锁屏 / Plasma 媒体控件。在会话总线注册 org.mpris.MediaPlayer2.<应用名>。
// 状态读自 AudioEngine；Play/Pause/PlayPause/Seek/SetPosition 直接作用于它，
// Next/Previous 只发信号：切歌规则只有 Playback 那一份，QML 接 nextRequested/previousRequested。
// 歌词放进元数据的 xesam:asText（LRC）：面板歌词组件（如 Lyrica 的 Plasma 小部件）直接用我们已取到的歌词，
// 不必再各自去网易取一遍。换歌时向 Store 要这首的歌词（歌词永久缓存，每首最多取一次）。
#include <QVariantMap>
#include <QObject>

class AudioEngine;
class Store;
class MprisBaseAdaptor;
class MprisPlayerAdaptor;
class QDBusInterface;

class Mpris : public QObject {
    Q_OBJECT
public:
    explicit Mpris(AudioEngine *audio, Store *store, QObject *parent = nullptr);
    ~Mpris() override;

    QString lyricsText() const { return m_lyricsText; }

signals:
    void nextRequested();
    void previousRequested();
    void raiseRequested();   // Plasma 媒体弹窗里点了曲目：把主窗口拿到前面

private:
    void emitChanged(const QStringList &properties);
    void refreshLyrics();   // Store 里是当前这首的歌词就写进元数据

    AudioEngine *m_audio;
    Store *m_store;
    MprisBaseAdaptor *m_base;
    MprisPlayerAdaptor *m_player;
    QString m_lyricsText;
    bool m_registered = false;
};

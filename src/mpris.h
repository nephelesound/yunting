#pragma once
// Mpris：媒体键 / 锁屏 / Plasma 媒体控件。在会话总线注册 org.mpris.MediaPlayer2.ncmplayer。
// 状态读自 AudioEngine；Play/Pause/PlayPause/Seek/SetPosition 直接作用于它，
// Next/Previous 只发信号——切歌规则只有 Playback 那一份，QML 接 nextRequested/previousRequested。
#include <QVariantMap>
#include <QObject>

class AudioEngine;
class MprisBaseAdaptor;
class MprisPlayerAdaptor;
class QDBusInterface;

class Mpris : public QObject {
    Q_OBJECT
public:
    explicit Mpris(AudioEngine *audio, QObject *parent = nullptr);
    ~Mpris() override;

signals:
    void nextRequested();
    void previousRequested();
    void raiseRequested();   // Plasma 媒体弹窗里点了曲目：把主窗口拿到前面

private:
    MprisBaseAdaptor *m_base;
    MprisPlayerAdaptor *m_player;
    bool m_registered = false;
};

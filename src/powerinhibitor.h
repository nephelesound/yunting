#pragma once
// 电源与屏幕（与 KDE 的集成）：
//   preventSleep：放歌时系统不自动睡眠；屏幕照常变暗、熄灭、锁屏，锁屏后继续放（锁屏上有 MPRIS 控件）。
//   keepScreenOn：看 MV 时屏幕也不熄、不锁。
// 走 freedesktop 的 PowerManagement.Inhibit 与 ScreenSaver.Inhibit（Plasma 的 PowerDevil 实现，Elisa 同做法），
// 电池小部件里会列出「云听：正在播放音乐」。服务不在（非 KDE 桌面、没有会话总线）时什么也不做。
// 进程退出时 PowerDevil 按总线连接自动撤销，不需要在析构里同步等待。
#include <QObject>

class PowerInhibitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool preventSleep READ preventSleep WRITE setPreventSleep NOTIFY preventSleepChanged)
    Q_PROPERTY(bool keepScreenOn READ keepScreenOn WRITE setKeepScreenOn NOTIFY keepScreenOnChanged)
public:
    explicit PowerInhibitor(QObject *parent = nullptr);

    bool preventSleep() const { return m_sleep.wanted; }
    void setPreventSleep(bool on);
    bool keepScreenOn() const { return m_screen.wanted; }
    void setKeepScreenOn(bool on);

signals:
    void preventSleepChanged();
    void keepScreenOnChanged();

private:
    // 一种抑制：想要的状态与实际持有的 cookie 分开记；请求在途时不重复发，回包后按最新的「想要」再对一次
    struct Inhibition {
        const char *service;
        const char *path;
        const char *interface;
        const char *reason;
        bool wanted = false;
        bool pending = false;
        uint cookie = 0;   // 0 = 没持有
    };
    void reconcile(Inhibition &inhibition);

    Inhibition m_sleep;
    Inhibition m_screen;
};

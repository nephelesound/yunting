#pragma once
// 电源与屏幕（与桌面的集成）：
//   preventSleep：放歌时系统不自动睡眠；屏幕照常变暗、熄灭、锁屏，锁屏后继续放（锁屏上有 MPRIS 控件）。
//   keepScreenOn：看 MV 时屏幕也不熄、不锁。
// 走 org.freedesktop.portal.Inhibit：Flatpak 里不需要直连 PowerManagement/ScreenSaver 的 D-Bus 权限，
// 桌面门户把抑制转给 PowerDevil 等后端，电池小部件里会列出「云听：正在播放音乐」。
// 门户不在（没有会话总线、非桌面会话）时什么也不做。请求句柄在析构或取消时用 Request.Close 撤销。
#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <utility>

class PowerInhibitor : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(bool preventSleep READ preventSleep WRITE setPreventSleep NOTIFY preventSleepChanged)
    Q_PROPERTY(bool keepScreenOn READ keepScreenOn WRITE setKeepScreenOn NOTIFY keepScreenOnChanged)
public:
    explicit PowerInhibitor(QObject *parent = nullptr,
                            const QString &sleepReason = QStringLiteral("正在播放音乐"),
                            const QString &screenReason = QStringLiteral("正在播放 MV"));
    ~PowerInhibitor() override;

    bool preventSleep() const { return m_sleep.wanted; }
    void setPreventSleep(bool on);
    bool keepScreenOn() const { return m_screen.wanted; }
    void setKeepScreenOn(bool on);

signals:
    void preventSleepChanged();
    void keepScreenOnChanged();

private slots:
    // 门户 Request 对象的 Response(u, a{sv})；门的路径从当前 D-Bus 消息里取。
    void onResponse(uint response, const QVariantMap &results);

private:
    // 一种抑制：想要的状态、在途请求与已生效的请求分开记。门户的 Inhibit 先返回请求句柄，
    // 生效与否由随后 Request.Response 的 0/非 0 决定；撤销是关闭请求句柄。
    struct Inhibition {
        Inhibition(uint flags, QString reason)
            : flags(flags)
            , reason(std::move(reason))
        {
        }

        uint flags;
        QString reason;
        bool wanted = false;
        bool pending = false;
        bool active = false;
        QString handle;
    };
    void reconcile(Inhibition &inhibition);
    void request(Inhibition &inhibition);
    // path 按值传：调用方常传 inhibition.handle，而函数会把它清空
    void closeHandle(QString path);
    void forgetHandle(QString path);

    Inhibition m_sleep;
    Inhibition m_screen;
    QHash<QString, Inhibition *> m_handles;
};

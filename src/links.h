#pragma once
// GUI 的链接入口：命令行、第二次启动的 D-Bus 激活都先到这里，解析后只给 QML 发一个信号。
// 窗口还没创建时收到的链接先挂起，等 QML 加载完由 start() 发出。
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QWindow;

class Links : public QObject {
    Q_OBJECT
public:
    explicit Links(QObject *parent = nullptr);

    // 主窗口创建后调用；第二次启动就是靠它抬起已有窗口。
    void setWindow(QWindow *window);
    // 冷启动：QML 已加载，处理命令行里的链接（以及窗口就绪前收到的激活链接）。
    void start(const QStringList &arguments);
    // 第二次启动：激活已运行实例并打开它带来的链接。
    void activate(const QStringList &arguments);

signals:
    // QML 只消费这一个形状；id 用 double 是 QML 的数字类型。
    void opened(const QString &kind, double id);

private:
    static QString firstLink(const QStringList &arguments);
    void dispatch(const QString &text);
    void raiseWindow();

    QPointer<QWindow> m_window;
    QString m_pending;
    bool m_ready = false;
};

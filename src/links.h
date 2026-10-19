#pragma once
// GUI 的链接入口：命令行、第二次启动的 D-Bus 激活、剪贴板都先到这里，解析后只给 QML 发信号。
// 窗口还没创建时收到的链接先挂起，等 QML 加载完由 start() 发出。
// 163cn.tv 短链经 NcmClient 问一次跳转再打开；只有用户要打开时才发这个请求。
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class NcmClient;
class QWindow;
struct FoundLink;

class Links : public QObject {
    Q_OBJECT
public:
    explicit Links(NcmClient *client, QObject *parent = nullptr);

    // 主窗口创建后调用：第二次启动靠它抬起已有窗口；窗口每次被激活时看一眼剪贴板。
    void setWindow(QWindow *window);
    // 冷启动：QML 已加载，处理命令行里的链接（以及窗口就绪前收到的激活链接）。
    void start(const QStringList &arguments);
    // 第二次启动：激活已运行实例并打开它带来的链接。
    void activate(const QStringList &arguments);

    // 打开一段文字里的网易云链接（剪贴板提示里点「打开」）。
    Q_INVOKABLE void open(const QString &text);

signals:
    // QML 消费的形状：kind ∈ song/playlist/album/artist/user/mv；id 用 double 是 QML 的数字类型。
    void opened(const QString &kind, double id);
    // 打不开：给用户看的一句话。
    void failed(const QString &message);
    // 窗口被激活时剪贴板里有网易云链接（别的程序复制的；同一段文字每次运行只提示一次）。
    void clipboardOffered(const QString &text);

private:
    static QString firstLink(const QStringList &arguments);
    void dispatch(const FoundLink &found);
    void offerClipboard();
    void raiseWindow();

    NcmClient *m_client;
    QPointer<QWindow> m_window;
    QString m_pending;
    QSet<QString> m_offered;
    bool m_ready = false;
};

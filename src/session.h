#pragma once
// Session：扫码登录（prototype/LoginDialog.qml 消费）。状态机与 SimSession.qml 同接口：
// idle → waiting →（scanned）→ done；取码失败 → error；等待超时 → expired；cancel() 从任何状态回 idle。
#include <QMap>
#include <QObject>
#include <QString>

class NcmClient;
class QTimer;
class Store;

class Session : public QObject {
    Q_OBJECT
    // idle | waiting | scanned | expired | error | done
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // 二维码内容 https://music.163.com/login?codekey=<unikey>；没有时为空串
    Q_PROPERTY(QString qrContent READ qrContent NOTIFY qrContentChanged)
    // 扫码的两套会话：client = PC 客户端（type=3，SPlayer 与默认），web = 网页（type=1，gtk 客户端）。
    // 两种 MUSIC_U 接口都认；切换后重新取码，二维码与轮询的 type 必须一致。
    Q_PROPERTY(QString loginMode READ loginMode WRITE setLoginMode NOTIFY loginModeChanged)
public:
    explicit Session(NcmClient *client, Store *store, QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString qrContent() const { return m_qrContent; }
    QString loginMode() const { return m_loginMode; }
    void setLoginMode(const QString &mode);

    // 取 unikey 进入 waiting，并开始每 2 秒轮询
    Q_INVOKABLE void start();
    // 停止轮询，回到 idle
    Q_INVOKABLE void cancel();
    // 用现有 MUSIC_U 刷新登录 cookie；成功后重拉首屏。会话过期时自动尝试一次。
    Q_INVOKABLE void refreshSession();
    // 网页端 Cookie 登录：粘贴整段 Cookie（MUSIC_U=…; __csrf=…）或只粘贴 MUSIC_U 的值。
    // 经账号接口校验，通过了才写进 jar：成功发 done 并重拉首屏，失败发 loginFailed。
    Q_INVOKABLE void loginWithCookie(const QString &raw);

signals:
    void stateChanged();
    void qrContentChanged();
    void loginModeChanged();
    void refreshed();
    void refreshFailed(const QString &message);
    void loginFailed(const QString &message);

private:
    void poll();
    void setState(const QString &state);
    // Cookie 登录的校验：api/nuser/account/get，拿到 profile 才写进 jar
    void checkCookie(const QMap<QString, QString> &credentials, quint64 generation);

    // 取码与轮询共用的 type：client=3（PC 客户端），web=1（网页）
    QString qrType() const
    {
        return m_loginMode == QLatin1String("web") ? QStringLiteral("1") : QStringLiteral("3");
    }

    NcmClient *m_client;
    Store *m_store;
    QTimer *m_timer;
    QString m_state = QStringLiteral("idle");
    QString m_qrContent;
    QString m_unikey;
    QString m_unikeyType;   // 取这张码时用的 type；轮询必须与它一致
    QString m_loginMode = QStringLiteral("client");   // client | web
    quint64 m_generation = 0;   // start()/cancel() 递增；旧二维码的回包一律丢弃
    bool m_polling = false;     // 同一时间只允许一个轮询请求在途
    bool m_refreshing = false;
    quint64 m_refreshGeneration = 0;
    quint64 m_cookieGeneration = 0;   // loginWithCookie 递增；旧校验的回包不能改状态
};

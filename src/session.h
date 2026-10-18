#pragma once
// Session：扫码登录（prototype/LoginDialog.qml 消费）。状态机与 SimSession.qml 同接口：
// idle → waiting →（scanned）→ done；取码失败 → error；等待超时 → expired；cancel() 从任何状态回 idle。
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
public:
    explicit Session(NcmClient *client, Store *store, QObject *parent = nullptr);

    QString state() const { return m_state; }
    QString qrContent() const { return m_qrContent; }

    // 取 unikey 进入 waiting，并开始每 2 秒轮询
    Q_INVOKABLE void start();
    // 停止轮询，回到 idle
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void qrContentChanged();

private:
    void poll();
    void setState(const QString &state);

    NcmClient *m_client;
    Store *m_store;
    QTimer *m_timer;
    QString m_state = QStringLiteral("idle");
    QString m_qrContent;
    QString m_unikey;
    quint64 m_generation = 0;   // start()/cancel() 递增；旧二维码的回包一律丢弃
    bool m_polling = false;     // 同一时间只允许一个轮询请求在途
};

#pragma once
// NcmClient 是唯一的网络边界：所有对 music.163.com 的 /api/ 明文请求都从这里发。
// 端点返回 JSON；归一化（snapshot.py 的形状）不在这里做，Store 负责。
#include <QJsonDocument>
#include <functional>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

class NcmClient : public QObject {
    Q_OBJECT
public:
    using GetCallback = std::function<void(const QJsonDocument &doc, bool ok)>;

    explicit NcmClient(QObject *parent = nullptr);

    // 一次性的 cookie 导入：读取 netease-cloud-music-gtk4 的 cookies.json，
    // 保留 MUSIC_U / __csrf / NMTID 写到自己的 jar。返回导入的 cookie 个数。
    static int importGtk4Cookies(const QString &targetPath);

    // jar 里有没有 MUSIC_U（决定 Store 是否拉个人数据）
    bool hasMusicU() const;

    // cookie jar 的落盘路径（--import-gtk4-cookies 写这里）
    QString jarPath() const { return m_jarPath; }

    // 登录成功（803）等场景：把 {name: value} 数组合并进 jar 并落盘
    void adoptCookies(const QJsonDocument &jarDoc);

    // GET https://music.163.com/<path>?<query>，完成后回调一次；失败时 doc 为空、ok=false。
    // 网络错误、超时与 HTTP 5xx 自动重试，最多 3 次，间隔 0.5 / 1 / 2 秒；4xx 与业务码不重试。
    // 语义层的在途去重在 Store.request（key 在它那里才知道）；这里每个请求都发。
    void get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback);

    // 测试接缝：把 API 指到本地 QTcpServer。默认 music.163.com。
    void setApiBase(const QString &base);

signals:
    // 每个请求（含重试）最终落地一次。networkFailure = 网络原因（无连接 / 超时 / DNS），
    // 区别于服务器明确回了非 200 或业务码；Store 靠它区分离线与「服务器说不行」。
    void requestFinished(bool ok, bool networkFailure);

private:
    // 发起一次尝试；retrying 表示这是重试（成功/终局只发一次回调与信号）
    void startGet(const QString &path, const QUrlQuery &query, bool personal,
                  int attempt, bool retrying, GetCallback callback);
    static bool isNetworkError(const QNetworkReply &reply);
    void loadJar();
    void saveJar() const;
    void keepSetCookies(class QNetworkReply *reply);
    // 同 snapshot.py 的 cookie_header()：只带这三个；空 value 跳过
    QString cookieHeader(bool personal) const;

    QNetworkAccessManager *m_nam;
    QString m_jarPath;
    QString m_apiBase = QStringLiteral("https://music.163.com/");
    QMap<QString, QString> m_jar;
};

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
    // 语义层的在途去重在 Store.request（key 在它那里才知道）；这里每个请求都发。
    void get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback);

private:
    void loadJar();
    void saveJar() const;
    void keepSetCookies(class QNetworkReply *reply);
    // 同 snapshot.py 的 cookie_header()：只带这三个；空 value 跳过
    QString cookieHeader(bool personal) const;

    QNetworkAccessManager *m_nam;
    QString m_jarPath;
    QMap<QString, QString> m_jar;
};

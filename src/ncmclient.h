#pragma once
// NcmClient 是唯一的网络边界：所有对 music.163.com 的 /api/ 明文请求都从这里发。
// 端点返回 JSON；归一化（snapshot.py 的形状）不在这里做，Store 负责。
// cookie jar 的存取走 CookieVault（钥匙串，后备 600 文件）：构造时先迁移 legacy
// 文件再读存储，读到 jar 后发 ready()——之前不得发个人请求。
#include <QJsonDocument>
#include <functional>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrlQuery>

class CookieVault;
class QNetworkAccessManager;

class NcmClient : public QObject {
    Q_OBJECT
public:
    using GetCallback = std::function<void(const QJsonDocument &doc, bool ok)>;

    explicit NcmClient(QObject *parent = nullptr);

    // 从 netease-cloud-music-gtk4 的 cookies.json 挑出 MUSIC_U / __csrf / NMTID。
    // 空表 = 读不了或格式不对。
    static QMap<QString, QString> importGtk4Cookies();

    // 一次性导入：把 gtk4 的 cookie 整包替换进 jar 并存入（钥匙串或后备文件），
    // 内部等存储写完才返回；返回导入的 cookie 个数，失败为 -1。
    int importGtk4();

    // jar 里有没有 MUSIC_U（决定 Store 是否拉个人数据）
    bool hasMusicU() const;

    // cookie 的后备文件路径（钥匙串不可用时落在这里；测试用）
    QString fallbackPath() const;

    // 登录成功（803）等场景：把 {name: value} 数组合并进 jar 并存储
    void adoptCookies(const QJsonDocument &jarDoc);

    // GET https://music.163.com/<path>?<query>，完成后回调一次；失败时 doc 为空、ok=false。
    // 语义层的在途去重在 Store.request（key 在它那里才知道）；这里每个请求都发。
    // 依赖 jar 的个人请求必须在 ready() 之后。
    void get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback);

signals:
    // jar 已从存储读出，可以开始（个人）请求了
    void ready();

private:
    void persistJar();
    void keepSetCookies(class QNetworkReply *reply);
    // 同 snapshot.py 的 cookie_header()：只带这三个；空 value 跳过
    QString cookieHeader(bool personal) const;

    CookieVault *m_vault;
    QNetworkAccessManager *m_nam;
    QMap<QString, QString> m_jar;
};

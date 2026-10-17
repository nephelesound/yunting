#pragma once
// NcmClient 是唯一的网络边界：所有对 music.163.com 的 /api/ 明文请求都从这里发。
// 端点返回 JSON；归一化（snapshot.py 的形状）不在这里做，Store 负责。
// cookie jar 的存取走 CookieVault（钥匙串，后备 600 文件）：构造时先迁移 legacy
// 文件再读存储，读到 jar 后发 ready()，在那之前不得发个人请求。
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

class CookieVault;
class QNetworkAccessManager;
class QNetworkReply;

class NcmClient : public QObject {
    Q_OBJECT
public:
    using GetCallback = std::function<void(const QJsonDocument &doc, bool ok)>;

    // 一次请求的落地结果。ok = 传输成功且 JSON 可解析；networkFailure 只在连接层失败
    // （没有 HTTP 状态码）时为 true —— 服务器回的 4xx/5xx 说明网络是通的。
    struct Response {
        QJsonDocument doc;
        bool ok = false;
        bool networkFailure = false;
    };
    using ResponseCallback = std::function<void(const Response &response)>;

    // 请求失败的唯一分类：只由传输结果与网易云业务码判定，绝不看 message 文本。
    // 301 是会话过期，-460 与 405 是风控/限流，其余都是服务器拒绝了这次操作。
    enum class Failure { None, Offline, SessionExpired, RateLimited, Rejected };
    static Failure failureOf(const Response &response);

    // loadCookies=false 用于 --anonymous / 只读调试：不碰钥匙串，立即 ready
explicit NcmClient(QObject *parent = nullptr, bool loadCookies = true);

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

    // 退出登录：移除 MUSIC_U / __csrf / NMTID 并落盘；匿名设备 cookie 不保留（我们只用这三个）
    void clearAuthCookies();

    // GET https://music.163.com/<path>?<query>，完成后回调一次；失败时 doc 为空、ok=false。
    // 网络错误、超时与 HTTP 5xx 自动重试，最多 3 次，间隔 0.5 / 1 / 2 秒；4xx 与业务码不重试。
    // 语义层的在途去重在 Store.request（key 在它那里才知道）；这里每个请求都发。
    // 依赖 jar 的个人请求必须在 ready() 之后。
    void get(const QString &path, const QUrlQuery &query, bool personal, GetCallback callback);

    // POST https://music.163.com/weapi/<path>，payload 经 weapi 加密（AES-CBC 两次 + RSA）。
    // 写操作不重试：重复提交红心/建歌单并不幂等。失败时 doc 为空、ok=false。
    void postWeapi(const QString &path, const QJsonObject &payload, bool personal, GetCallback callback,
                   bool keepCookies = true);
    // 同上，但把传输结果一起交出来：写操作要区分「连不上」与「服务器回了话但拒绝」，
    // 不能把 HTTP 错误当成断网。
    void postWeapi(const QString &path, const QJsonObject &payload, bool personal,
                   ResponseCallback callback, bool keepCookies = true);
    // 明文 /api/ 表单 POST（听歌打卡 /api/feedback/weblog 用），同样不重试。
    void postForm(const QString &path, const QUrlQuery &form, bool personal, GetCallback callback);

    // 客户端日志（NCBL）上传：multipart 已由 Ncbl::multipart 组好，cookie 是完整设备上下文。
    // 不参与 Store 的 online 计数；失败只回调一次，不重试。
    void postClientLog(const QByteArray &multipart, const QString &boundary,
                       const QString &cookie, GetCallback callback);
    void postClientLogAttempt(const QByteArray &multipart, const QString &boundary,
                              const QString &cookie, GetCallback callback, int attempt);
    // NCBL 需要的 cookie jar（只读快照；不要在调用方打印）
    QMap<QString, QString> cookieJar() const { return m_jar; }

    // Store 的回调生命周期：Store 先于本对象销毁时，晚到的回包不再回调。
    // 只应由 Store 设置；不设置时（CLI / 测试）所有回调照常。
    void setCallbackContext(QObject *context);
    bool callbackContextAlive() const;

    // 测试接缝：把 API 指到本地 QTcpServer。默认 music.163.com。
    void setApiBase(const QString &base);

    // 退出前把排队中的凭据写盘做完（钥匙串是异步的）；最多等 timeoutMs 毫秒
    bool flushCredentials(int timeoutMs = 2000);

    // jar 是否已从存储读出（ready() 发过）
    bool isReady() const { return m_ready; }

signals:
    // jar 已从存储读出，可以开始（个人）请求了
    void ready();

    // 每个请求（含重试）最终落地一次。networkFailure = 网络原因（无连接 / 超时 / DNS），
    // 区别于服务器明确回了非 200 或业务码；Store 靠它区分离线与「服务器说不行」。
    void requestFinished(bool ok, bool networkFailure);

private:
    // 发起一次尝试；retrying 表示这是重试（成功/终局只发一次回调与信号）
    void startGet(const QString &path, const QUrlQuery &query, bool personal,
                  int attempt, bool retrying, GetCallback callback);
    static bool isNetworkError(const QNetworkReply &reply);
    void postRaw(const QUrl &url, const QByteArray &body, const QString &cookie, ResponseCallback callback,
                 bool keepCookies = true);
    void persistJar();
    void keepSetCookies(class QNetworkReply *reply);
    // 同 snapshot.py 的 cookie_header()：只带这三个；空 value 跳过
    QString cookieHeader(bool personal) const;

    CookieVault *m_vault;
    QNetworkAccessManager *m_nam;
    QString m_apiBase = QStringLiteral("https://music.163.com/");
    QMap<QString, QString> m_jar;
    bool m_ready = false;
    bool m_contextSet = false;
    QPointer<QObject> m_callbackContext;     // 存储已读出：可以发请求
    bool m_persist = true;    // jar 变了写回存储；匿名调试（不读存储）时绝不写，否则匿名 jar 会盖掉登录凭据
};

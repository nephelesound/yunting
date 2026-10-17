#pragma once
// CookieVault：cookie jar 的存储层（DESIGN.md「凭据存储」）。
// 主路是系统钥匙串（QtKeychain → Secret Service/KWallet）：服务名 YUNTING_KEYCHAIN_SERVICE
// 或应用名，键 "cookies"，值是紧凑 JSON。钥匙串不可用（无会话总线、后端出错、
// 用户拒绝）时退回 600 权限的 cookies.json，每次运行至多提示一次；该文件也是钥匙串
// 引入前的 legacy 落点，migrate() 把它导入钥匙串、写成功后才删除。
// 操作串行执行（排队、逐个完成），每个操作的结果经对应信号恰好发出一次。
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class CookieVault : public QObject {
    Q_OBJECT
public:
    explicit CookieVault(QObject *parent = nullptr);

    // 后备文件路径（= legacy 文件路径；迁移成功后被删除）
    QString fallbackPath() const { return m_fallbackPath; }

    // 异步读；结果经 loaded() 发出。无条目、或走后备且无文件时 data 为空。
    void load();
    // 异步写；结果经 saved() 发出。钥匙串写失败时同一份数据自动落后备文件。
    void save(const QByteArray &json);
    // 后备/legacy 文件 → 钥匙串（文件存在就是最新的，覆盖条目），写成功才删文件；
    // 完成后（做没做事都一样）发 migrated()
    // 恰好一次，之后的 load 读到的就是最终存储。必须是本对象的第一个操作
    // （NcmClient 构造时先 migrate 再 load），其探测读兼作钥匙串可用性判定。
    void migrate();

    // 退出前等排队中的操作（尤其是保存）落地；返回是否在时限内清空。文件模式同步完成
    bool waitForPending(int timeoutMs);

signals:
    void loaded(const QByteArray &data);
    // 读失败（不是 EntryNotFound）：存储里的内容未知，调用方不能把它当成空 jar
    void loadFailed();
    void saved(bool ok);
    void migrated();

private:
    enum class Mode { Undecided, Keychain, File };
    struct Op {
        bool write = false;
        bool migration = false;   // 迁移写：成功要删 legacy 文件，失败不切后备模式
        QByteArray json;
    };
    void enqueue(const Op &op);
    void pump();
    void startProbe();
    void runSave(const QByteArray &json);
    void readKeychain();
    void writeMigrated(const QByteArray &json);
    void writeFile(const QByteArray &json) const;
    QByteArray loadFile() const;
    void logFallback(const char *why);

    QString m_fallbackPath;
    QString m_service;
    QString m_key;
    Mode m_mode = Mode::Undecided;
    bool m_busy = false;             // 一个异步操作在途，完成后 pump() 取下一个
    bool m_probeInFlight = false;
    bool m_fallbackLogged = false;
    QList<Op> m_pending;
};

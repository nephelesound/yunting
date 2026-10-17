#include "cookievault.h"

#include <qt6keychain/keychain.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <QLoggingCategory>
#include <QStandardPaths>
#include <utility>

Q_LOGGING_CATEGORY(lcVault, "yunting.vault", QtWarningMsg)

namespace {
// 开发与测试用 YUNTING_KEYCHAIN_SERVICE 换一个服务名，不碰日常使用的存储。
// 换名要同时换掉后备文件：否则迁移会读走并删除日常的那个文件
QString serviceOverride()
{
    return qEnvironmentVariable("YUNTING_KEYCHAIN_SERVICE");
}
} // namespace

CookieVault::CookieVault(QObject *parent)
    // 后备文件（也是钥匙串引入前的落点）在应用数据目录里，遵循 XDG_DATA_HOME
    : QObject(parent),
      m_fallbackPath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + (serviceOverride().isEmpty() ? QStringLiteral("/cookies.json")
                                                    : "/cookies-" + serviceOverride() + ".json")),
      m_service(serviceOverride().isEmpty() ? QCoreApplication::applicationName() : serviceOverride()),
      m_key(QStringLiteral("cookies"))
{
}

void CookieVault::enqueue(const Op &op)
{
    m_pending.append(op);
    pump();
}

void CookieVault::pump()
{
    if (m_busy)
        return;

    if (m_mode == Mode::Undecided) {
        if (m_probeInFlight)
            return;
        // 首个操作兼作钥匙串可用性探测（读一次）；等 finished 里按结论重放
        m_probeInFlight = true;
        auto *probe = new QKeychain::ReadPasswordJob(m_service, this);
        probe->setKey(m_key);
        connect(probe, &QKeychain::ReadPasswordJob::finished, this, [this, probe] {
            m_probeInFlight = false;
            // EntryNotFound 是好答案：钥匙串在、还没有条目。其余错误（无会话总线、
            // 后端出错、用户拒绝）都算钥匙串不可用
            const bool keychainOk =
                probe->error() == QKeychain::NoError || probe->error() == QKeychain::EntryNotFound;
            m_mode = keychainOk ? Mode::Keychain : Mode::File;
            if (!keychainOk)
                logFallback(probe->errorString().toUtf8().constData());
            probe->deleteLater();
            m_busy = false;
            pump();
        });
        probe->start();
        m_busy = true;
        return;
    }

    // File 模式的操作是同步的：一次清空队列（否则后续操作没人再 pump，ready 永远不来）
    while (!m_pending.isEmpty() && m_mode == Mode::File) {
        const Op op = m_pending.takeFirst();
        if (op.write) {
            writeFile(op.json);
            emit saved(true);
        } else {
            emit loaded(loadFile());
        }
        if (op.migration)
            emit migrated();
    }
    if (m_pending.isEmpty())
        return;
    // Keychain 模式：取一个，等它异步完成后再 pump 下一个
    const Op op = m_pending.takeFirst();
    if (op.write) {
        // 文件存在时它总是最新的一份：只有钥匙串不可用（或钥匙串之前的旧版）才会写它，
        // 所以迁移直接覆盖钥匙串条目
        if (op.migration)
            writeMigrated(op.json);
        else
            runSave(op.json);
    } else {
        readKeychain();
    }
}

void CookieVault::load()
{
    enqueue({false, false, {}});
}

void CookieVault::save(const QByteArray &json)
{
    enqueue({true, false, json});
}

void CookieVault::migrate()
{
    QFile legacy(m_fallbackPath);
    if (!legacy.open(QIODevice::ReadOnly)) {
        emit migrated();
        return;
    }
    const QByteArray json = legacy.readAll();
    legacy.close();
    enqueue({true, true, json});
}

void CookieVault::runSave(const QByteArray &json)
{
    auto *job = new QKeychain::WritePasswordJob(m_service, this);
    job->setKey(m_key);
    job->setBinaryData(json);
    connect(job, &QKeychain::WritePasswordJob::finished, this, [this, json](QKeychain::Job *done) {
        const bool ok = done->error() == QKeychain::NoError;
        if (!ok) {
            // 钥匙串写失败（用户拒绝、后端消失）：同一份数据退回后备文件，之后一直走文件
            logFallback(done->errorString().toUtf8().constData());
            m_mode = Mode::File;
            writeFile(json);
        }
        done->deleteLater();
        m_busy = false;
        pump();
        emit saved(ok);
    });
    job->start();
    m_busy = true;
}

void CookieVault::readKeychain()
{
    auto *job = new QKeychain::ReadPasswordJob(m_service, this);
    job->setKey(m_key);
    connect(job, &QKeychain::ReadPasswordJob::finished, this, [this, job](QKeychain::Job *done) {
        // 读失败不能当「无条目」：空 jar 会让随后的匿名 NMTID 落盘，覆盖真凭据。
        // EntryNotFound 才是权威的「还没有条目」。
        const bool readError = done->error() != QKeychain::NoError
            && done->error() != QKeychain::EntryNotFound;
        if (readError)
            qCWarning(lcVault) << "keychain read failed:" << done->errorString();
        const QByteArray data = done->error() == QKeychain::NoError ? job->binaryData() : QByteArray();
        done->deleteLater();
        m_busy = false;
        pump();
        if (readError)
            emit loadFailed();
        else
            emit loaded(data);
    });
    job->start();
    m_busy = true;
}

bool CookieVault::waitForPending(int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    while ((m_busy || !m_pending.isEmpty()) && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return !m_busy && m_pending.isEmpty();
}

void CookieVault::writeMigrated(const QByteArray &json)
{
    auto *job = new QKeychain::WritePasswordJob(m_service, this);
    job->setKey(m_key);
    job->setBinaryData(json);
    connect(job, &QKeychain::WritePasswordJob::finished, this, [this, json](QKeychain::Job *done) {
        if (done->error() == QKeychain::NoError) {
            QFile::remove(m_fallbackPath);
            // 不用 qCInfo：本机日志配置会吞掉 info；qCDebug 在 --verbose 下可见（同 ncm.net）
            qCDebug(lcVault) << "cookie migration: legacy cookies.json imported into keychain";
        } else {
            // 迁移写失败不切后备模式（本次启动已按 load 结果工作）；文件留着，下次启动再迁
            qCWarning(lcVault) << "cookie migration write failed:" << done->errorString();
        }
        done->deleteLater();
        m_busy = false;
        pump();
        emit migrated();
    });
    job->start();
    m_busy = true;
}

void CookieVault::writeFile(const QByteArray &json) const
{
    const QString dirPath = QFileInfo(m_fallbackPath).path();
    QDir().mkpath(dirPath);
    QFile::setPermissions(dirPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
#ifdef Q_OS_UNIX
    // 用 open(0600) 创建，而不是先 open 再 chmod：中间不存在一个其他用户可读的窗口。
    const QByteArray path = QFile::encodeName(m_fallbackPath);
    const int fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd >= 0) {
        QFile f;
        if (f.open(fd, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
            if (f.write(json) == json.size())
                return;
            qCWarning(lcVault) << "cannot write cookie file:" << m_fallbackPath << f.errorString();
            return;
        }
        ::close(fd);
    }
#endif
    QFile f(m_fallbackPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // 登录凭据：只给本人读写
        f.write(json);
    } else {
        qCWarning(lcVault) << "cannot write cookie file:" << m_fallbackPath << f.errorString();
    }
}

QByteArray CookieVault::loadFile() const
{
    QFile f(m_fallbackPath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

void CookieVault::logFallback(const char *why)
{
    if (m_fallbackLogged)
        return;
    m_fallbackLogged = true;
    qCWarning(lcVault) << "keychain unavailable, using cookie file" << m_fallbackPath
                       << "(" << why << ")";
}

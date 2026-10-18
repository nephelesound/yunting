// CookieVault 的后备文件路径测试：把会话总线指到不存在的地址，钥匙串必然不可用，
// 验证它退回 600 权限的 cookies.json 且数据原样可读。钥匙串主路需要守护进程与
// 可能的用户确认，不进单测（由 --dump-store 的手工验证覆盖）。
#include "../cookievault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class CookieVaultTest : public QObject {
    Q_OBJECT

private slots:
    // 在任何 DBus 连接建立之前把会话总线指到不存在的地址：钥匙串必然不可用，
    //逼它走文件后备。钥匙串主路要守护进程与可能的用户确认，不进单测
    //（由 --dump-store 的手工验证覆盖）。
    void initTestCase()
    {
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent");
    }

    // 换服务名（开发、测试）时后备文件也换掉：迁移不能读走、删掉日常使用的那个文件
    void serviceOverrideLeavesTheDailyFileAlone()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        const QString daily = dir.filePath(QCoreApplication::applicationName() + "/cookies.json");
        QVERIFY(QDir().mkpath(QFileInfo(daily).path()));
        QFile f(daily);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"MUSIC_U":"daily"})");
        f.close();

        qputenv("YUNTING_KEYCHAIN_SERVICE", "yunting-test");
        CookieVault vault;
        qunsetenv("YUNTING_KEYCHAIN_SERVICE");
        QVERIFY(vault.fallbackPath() != daily);
        vault.migrate();
        QTest::qWait(200);   // 迁移经钥匙串任务异步结束（总线不可用，很快失败）
        QVERIFY2(QFile::exists(daily), "换了服务名的迁移删掉了日常的凭据文件");
    }

    void fallbackFileRoundTrip()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        const QByteArray jar = R"({"MUSIC_U":"x","NMTID":"y"})";

        CookieVault vault;
        QCOMPARE(vault.fallbackPath(), dir.filePath(QCoreApplication::applicationName() + "/cookies.json"));
        QEventLoop loop;
        connect(&vault, &CookieVault::saved, &loop, [&loop](bool ok) {
            QVERIFY(ok);
            loop.quit();
        });
        vault.save(jar);
        loop.exec();

        QFile f(vault.fallbackPath());
        QVERIFY(f.exists());
        QCOMPARE(f.permissions() & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup
                                    | QFileDevice::WriteGroup | QFileDevice::ReadOther | QFileDevice::WriteOther),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);   // 600：只有本人读写
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), jar);

        CookieVault reopened;
        QEventLoop readLoop;
        QByteArray got;
        connect(&reopened, &CookieVault::loaded, &readLoop, [&](const QByteArray &data) {
            got = data;
            readLoop.quit();
        });
        reopened.load();
        readLoop.exec();
        QCOMPARE(got, jar);
    }

    // 后备文件存在但读不了：必须报 loadFailed，不能当成「没有凭据」让调用方覆盖它。
    void unreadableFallbackFileFailsLoudly()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        CookieVault vault;
        QVERIFY(QDir().mkpath(QFileInfo(vault.fallbackPath()).path()));
        QFile f(vault.fallbackPath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::Permissions()));
        QEventLoop loop;
        int loadedCount = 0;
        int failedCount = 0;
        connect(&vault, &CookieVault::loaded, &loop, [&] { ++loadedCount; loop.quit(); });
        connect(&vault, &CookieVault::loadFailed, &loop, [&] { ++failedCount; loop.quit(); });
        vault.load();
        loop.exec();
        QCOMPARE(failedCount, 1);
        QCOMPARE(loadedCount, 0);
    }

    // 后备文件写不进去时 saved 必须是 false，不能假装凭据已保存。
    void failedFallbackWriteIsReported()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        CookieVault vault;
        QVERIFY(QDir().mkpath(vault.fallbackPath()));   // 目录占住文件路径
        QEventLoop loop;
        int savedOk = -1;
        connect(&vault, &CookieVault::saved, &loop, [&](bool ok) { savedOk = ok ? 1 : 0; loop.quit(); });
        vault.save("{}");
        loop.exec();
        QCOMPARE(savedOk, 0);
    }

    void migrateWithoutFileEmitsOnce()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());

        CookieVault vault;
        QEventLoop loop;
        int count = 0;
        connect(&vault, &CookieVault::migrated, &loop, [&] {
            ++count;
            loop.quit();
        });
        vault.migrate();
        // 无文件时 migrated() 同步发出（exec 之前），异步路径（要探测）才需要 exec
        if (count == 0)
            loop.exec();
        QCOMPARE(count, 1);
        QVERIFY(!QFile::exists(dir.filePath(QCoreApplication::applicationName() + "/cookies.json")));
    }
};

QTEST_GUILESS_MAIN(CookieVaultTest)
#include "cookievault_test.moc"

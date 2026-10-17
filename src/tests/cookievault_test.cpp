// CookieVault 的后备文件路径测试：把会话总线指到不存在的地址，钥匙串必然不可用，
// 验证它退回 600 权限的 cookies.json 且数据原样可读。钥匙串主路需要守护进程与
// 可能的用户确认，不进单测（由 --dump-store 的手工验证覆盖）。
#include "../cookievault.h"

#include <QFile>
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

    void fallbackFileRoundTrip()
    {
        QTemporaryDir dir;
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        const QByteArray jar = R"({"MUSIC_U":"x","NMTID":"y"})";

        CookieVault vault;
        QCOMPARE(vault.fallbackPath(), dir.filePath("ncm-player/cookies.json"));
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
        QVERIFY(!QFile::exists(dir.filePath("ncm-player/cookies.json")));
    }
};

QTEST_GUILESS_MAIN(CookieVaultTest)
#include "cookievault_test.moc"

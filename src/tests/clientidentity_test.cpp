// 客户端身份是评论写入风控的前置条件：必须带完整四段官方版本，不能退回截断的 3.1.17。
#include "../clientidentity.h"

#include <QTest>

class ClientIdentityTest : public QObject {
    Q_OBJECT
private slots:
    void versionIsComplete()
    {
        const QString version = ncm::client::linuxDesktop().appver;
        QCOMPARE(version.split(u'.').size(), 4);
        QVERIFY(!version.isEmpty());
    }

    void cookieContextCarriesTheWholeIdentity()
    {
        const QString context = ncm::client::cookieContext();
        QVERIFY(context.contains(QStringLiteral("os=linux")));
        QVERIFY(context.contains(QStringLiteral("appver=1.2.1.0428")));
        QVERIFY(context.contains(QStringLiteral("osver=Deepin 20.9")));
        QVERIFY(context.contains(QStringLiteral("channel=netease")));
    }
};

QTEST_GUILESS_MAIN(ClientIdentityTest)
#include "clientidentity_test.moc"

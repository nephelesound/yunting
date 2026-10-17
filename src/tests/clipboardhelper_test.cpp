// 剪贴板必须真正落到 QClipboard，而不是依赖无窗口 TextEdit 的 copy()。用 offscreen 平台测试。
#include "../clipboardhelper.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

class ClipboardHelperTest : public QObject {
    Q_OBJECT
private slots:
    void copiesTextToTheSystemClipboard()
    {
        ClipboardHelper helper;
        QVERIFY(helper.copy(QStringLiteral("云听 clip test")));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("云听 clip test"));
    }

    void refusesEmptyText()
    {
        ClipboardHelper helper;
        QVERIFY(!helper.copy(QString()));
    }

    void copiesAnImageFileToTheSystemClipboard()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("card.png"));
        QImage image(4, 3, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        ClipboardHelper helper;
        QVERIFY(helper.copyImage(path));
        QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(4, 3));
        QVERIFY(!helper.copyImage(dir.filePath(QStringLiteral("missing.png"))));
    }
};

QTEST_MAIN(ClipboardHelperTest)
#include "clipboardhelper_test.moc"

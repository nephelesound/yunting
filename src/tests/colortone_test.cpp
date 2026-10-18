// 色调只给中性的方案：Breeze 的浅灰与深灰算中性，有色调的背景（Material You 那种）不算；混合按比例走向强调色
#include "../colortone.h"

#include <QTest>

class ColorToneTest : public QObject {
    Q_OBJECT
private slots:
    void breezeBackgroundsAreNeutral()
    {
        QVERIFY(ColorTone::isNeutral(QColor(239, 240, 241)));   // Breeze Light 窗口
        QVERIFY(ColorTone::isNeutral(QColor(32, 35, 38)));      // Breeze Dark 窗口
    }

    void tintedBackgroundsAreLeftAlone()
    {
        QVERIFY(!ColorTone::isNeutral(QColor(232, 238, 248)));  // 偏蓝的浅色背景
        QVERIFY(!ColorTone::isNeutral(QColor(248, 232, 236)));  // 偏粉的浅色背景
    }

    void tintMovesTowardTheAccent()
    {
        const QColor base(200, 200, 200);
        const QColor accent(0, 100, 250);
        QCOMPARE(ColorTone::tint(base, accent, 0), base);
        QCOMPARE(ColorTone::tint(base, accent, 1), accent);
        const QColor half = ColorTone::tint(base, accent, 0.5);
        QCOMPARE(half.red(), 100);
        QCOMPARE(half.green(), 150);
        QCOMPARE(half.blue(), 225);
    }
};

QTEST_GUILESS_MAIN(ColorToneTest)
#include "colortone_test.moc"

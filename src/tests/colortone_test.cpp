// 色调只给中性的方案：Breeze 的浅灰与深灰算中性，有色调的背景（Material You 那种）不算
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
};

QTEST_GUILESS_MAIN(ColorToneTest)
#include "colortone_test.moc"

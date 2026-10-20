// Material You 配色：用官方 Material Color Utilities 的 C++ 实现生成 Tonal Spot 方案，
// 再按 kde-material-you-colors 的映射写成 KDE 配色方案。期望值由 Python materialyoucolor
// 的 2021 规范对同一颗种子算出，两边逐角色一致。
#include "../materialyou.h"

#include <KConfig>
#include <KConfigGroup>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>

namespace {
// 方案文件里的 r,g,b。不用 readEntry 的 QColor 重载：它要 KConfigGui 在加载时注册的转换，
// Arch 打包（--as-needed）链出来的测试程序里没有它，读出来全是黑
QColor colorAt(const KConfigGroup &group, const char *key)
{
    const QStringList rgb = group.readEntry(key, QString()).split(u',');
    return rgb.size() == 3 ? QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()) : QColor();
}

// WCAG 对比度
double luminance(const QColor &c)
{
    const auto channel = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
} // namespace

class MaterialYouTest : public QObject {
    Q_OBJECT
private slots:
    void schemeMatchesMaterial3Baseline()
    {
        const MaterialYou::Scheme light = MaterialYou::schemeFor(QColor("#3daee9"), false);
        QCOMPARE(light.primary.name(), QStringLiteral("#226487"));
        QCOMPARE(light.onPrimary.name(), QStringLiteral("#ffffff"));
        QCOMPARE(light.primaryContainer.name(), QStringLiteral("#c7e7ff"));
        QCOMPARE(light.onPrimaryContainer.name(), QStringLiteral("#004c6c"));
        QCOMPARE(light.surface.name(), QStringLiteral("#f6fafe"));
        QCOMPARE(light.surfaceDim.name(), QStringLiteral("#d7dadf"));
        QCOMPARE(light.surfaceBright.name(), QStringLiteral("#f6fafe"));
        QCOMPARE(light.surfaceContainerLowest.name(), QStringLiteral("#ffffff"));
        QCOMPARE(light.surfaceContainerLow.name(), QStringLiteral("#f1f4f9"));
        QCOMPARE(light.surfaceContainer.name(), QStringLiteral("#ebeef3"));
        QCOMPARE(light.surfaceContainerHigh.name(), QStringLiteral("#e5e8ed"));
        QCOMPARE(light.surfaceContainerHighest.name(), QStringLiteral("#dfe3e7"));
        QCOMPARE(light.onSurface.name(), QStringLiteral("#181c20"));
        QCOMPARE(light.onSurfaceVariant.name(), QStringLiteral("#41484d"));
        QCOMPARE(light.outline.name(), QStringLiteral("#71787e"));
        QCOMPARE(light.outlineVariant.name(), QStringLiteral("#c1c7ce"));
        QCOMPARE(light.secondary.name(), QStringLiteral("#4f616e"));
        QCOMPARE(light.secondaryContainer.name(), QStringLiteral("#d2e5f5"));
        QCOMPARE(light.tertiary.name(), QStringLiteral("#62597c"));

        const MaterialYou::Scheme dark = MaterialYou::schemeFor(QColor("#3daee9"), true);
        QCOMPARE(dark.primary.name(), QStringLiteral("#92cef6"));
        QCOMPARE(dark.onPrimary.name(), QStringLiteral("#00344c"));
        QCOMPARE(dark.primaryContainer.name(), QStringLiteral("#004c6c"));
        QCOMPARE(dark.onPrimaryContainer.name(), QStringLiteral("#c7e7ff"));
        QCOMPARE(dark.surface.name(), QStringLiteral("#101417"));
        QCOMPARE(dark.surfaceDim.name(), QStringLiteral("#101417"));
        QCOMPARE(dark.surfaceBright.name(), QStringLiteral("#353a3d"));
        QCOMPARE(dark.surfaceContainerLowest.name(), QStringLiteral("#0a0f12"));
        QCOMPARE(dark.surfaceContainerLow.name(), QStringLiteral("#181c20"));
        QCOMPARE(dark.surfaceContainer.name(), QStringLiteral("#1c2024"));
        QCOMPARE(dark.surfaceContainerHigh.name(), QStringLiteral("#262a2e"));
        QCOMPARE(dark.surfaceContainerHighest.name(), QStringLiteral("#313539"));
        QCOMPARE(dark.onSurface.name(), QStringLiteral("#dfe3e7"));
        QCOMPARE(dark.onSurfaceVariant.name(), QStringLiteral("#c1c7ce"));
        QCOMPARE(dark.outline.name(), QStringLiteral("#8b9198"));
        QCOMPARE(dark.outlineVariant.name(), QStringLiteral("#41484d"));
        QCOMPARE(dark.secondary.name(), QStringLiteral("#b6c9d8"));
        QCOMPARE(dark.secondaryContainer.name(), QStringLiteral("#374955"));
        QCOMPARE(dark.tertiary.name(), QStringLiteral("#ccc0e9"));
    }

    void kdeSchemeMapsMaterialRoles()
    {
        const MaterialYou::Scheme light = MaterialYou::schemeFor(QColor("#3daee9"), false);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("light.colors"));
        QCOMPARE(MaterialYou::writeKdeScheme(light, false, path), path);

        KConfig config(path, KConfig::SimpleConfig);
        const auto at = [&config](const char *group, const char *key) {
            return colorAt(config.group(QString::fromLatin1(group)), key).name();
        };
        QCOMPARE(at("Colors:View", "BackgroundNormal"), light.surfaceBright.name());
        QCOMPARE(at("Colors:Window", "BackgroundNormal"), light.surfaceContainer.name());
        QCOMPARE(at("Colors:Button", "BackgroundNormal"), light.surfaceContainerHigh.name());
        QCOMPARE(at("Colors:Header", "BackgroundNormal"), light.surfaceContainer.name());
        QCOMPARE(at("Colors:Selection", "BackgroundNormal"), light.primary.name());
        QCOMPARE(at("Colors:Selection", "ForegroundNormal"), light.onPrimary.name());
        QCOMPARE(at("Colors:View", "ForegroundNormal"), light.onSurface.name());

        const MaterialYou::Scheme dark = MaterialYou::schemeFor(QColor("#3daee9"), true);
        const QString darkPath = directory.filePath(QStringLiteral("dark.colors"));
        QCOMPARE(MaterialYou::writeKdeScheme(dark, true, darkPath), darkPath);
        KConfig darkConfig(darkPath, KConfig::SimpleConfig);
        QCOMPARE(colorAt(darkConfig.group(QStringLiteral("Colors:View")), "BackgroundNormal").name(),
                 dark.surfaceDim.name());
        QCOMPARE(colorAt(darkConfig.group(QStringLiteral("Colors:Window")), "BackgroundNormal").name(),
                 dark.surfaceContainer.name());
    }

    void writtenSchemesAreReadable_data()
    {
        QTest::addColumn<QColor>("seed");
        QTest::addColumn<bool>("dark");
        const QList<QPair<const char *, QColor>> seeds{{"breeze-blue", QColor("#3daee9")},
                                                        {"red", QColor("#e93d58")},
                                                        {"yellow", QColor("#f6c013")},
                                                        {"green", QColor("#27ae60")},
                                                        {"grey", QColor("#7f8c8d")}};
        for (const auto &[name, seed] : seeds) {
            QTest::addRow("%s-light", name) << seed << false;
            QTest::addRow("%s-dark", name) << seed << true;
        }
    }

    // 写出来的方案：每一组的正文、次要文字、错误色、链接色在自己的底上都过 4.5:1；
    // 深色方案的窗口是深的、浅色的是浅的
    void writtenSchemesAreReadable()
    {
        QFETCH(QColor, seed);
        QFETCH(bool, dark);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("scheme.colors"));
        MaterialYou::writeKdeScheme(MaterialYou::schemeFor(seed, dark), dark, path);
        KConfig config(path, KConfig::SimpleConfig);
        for (const char *name : {"Colors:View", "Colors:Window", "Colors:Header", "Colors:Header][Inactive",
                                 "Colors:Button", "Colors:Tooltip", "Colors:Selection", "Colors:Complementary"}) {
            const KConfigGroup group = config.group(QString::fromLatin1(name));
            QVERIFY2(group.exists(), name);
            const QColor background = colorAt(group, "BackgroundNormal");
            QVERIFY2(background.isValid(), name);
            for (const char *key : {"ForegroundNormal", "ForegroundInactive", "ForegroundNegative", "ForegroundLink"}) {
                const double ratio = contrast(colorAt(group, key), background);
                QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 %2 %3:1").arg(QLatin1String(name), QLatin1String(key)).arg(ratio)));
            }
        }
        const double window = colorAt(config.group(QStringLiteral("Colors:Window")), "BackgroundNormal").lightnessF();
        QVERIFY(dark ? window < 0.2 : window > 0.85);
    }

    // 状态色往种子色调和，但仍是原来那一类：错误还是红的，成功还是绿的
    void statusColorsKeepTheirMeaning()
    {
        for (const bool dark : {false, true}) {
            const MaterialYou::Scheme scheme = MaterialYou::schemeFor(QColor("#3daee9"), dark);
            const int negative = scheme.negative.hslHue();
            const int positive = scheme.positive.hslHue();
            QVERIFY2(negative < 30 || negative > 330, qPrintable(QString::number(negative)));
            QVERIFY2(positive > 80 && positive < 170, qPrintable(QString::number(positive)));
        }
    }

    void recognizesMaterialYouSchemeNames()
    {
        QVERIFY(MaterialYou::schemeNameIsMaterialYou(QStringLiteral("MaterialYouLight")));
        QVERIFY(MaterialYou::schemeNameIsMaterialYou(QStringLiteral("MaterialYouDark2")));
        QVERIFY(MaterialYou::schemeNameIsMaterialYou(QStringLiteral("Material You Light")));
        QVERIFY(!MaterialYou::schemeNameIsMaterialYou(QStringLiteral("BreezeLight")));
        QVERIFY(!MaterialYou::schemeNameIsMaterialYou(QString()));
    }
};

QTEST_GUILESS_MAIN(MaterialYouTest)
#include "materialyou_test.moc"

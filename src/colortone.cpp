#include "colortone.h"

#include <KColorScheme>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

// 方案文件里每个颜色集的组名，与 KColorScheme::ColorSet 同序
const char *const setGroups[] = {"Colors:View", "Colors:Window", "Colors:Button", "Colors:Selection",
                                 "Colors:Tooltip", "Colors:Complementary", "Colors:Header"};

// 各颜色集的背景向强调色混合多少：窗口与页头最明显，列表与按钮淡一些；选中与反色区不动。
// 深色方案的背景本身暗，同样的比例看不出来，多一些
qreal amountFor(KColorScheme::ColorSet set, bool dark)
{
    switch (set) {
    case KColorScheme::Window: return dark ? 0.09 : 0.07;
    case KColorScheme::Header: return dark ? 0.10 : 0.08;
    case KColorScheme::Button: return dark ? 0.08 : 0.06;
    case KColorScheme::View: return dark ? 0.06 : 0.04;
    case KColorScheme::Tooltip: return dark ? 0.08 : 0.06;
    default: return 0;
    }
}

// 一个颜色集的全部颜色写进方案文件（系统里多半没有显式写出来，读 KColorScheme 算出的实际值）
void writeSet(KConfig &out, KColorScheme::ColorSet set, const QColor &accent, bool dark)
{
    const KColorScheme scheme(QPalette::Active, set);
    const qreal amount = amountFor(set, dark);
    KConfigGroup group(&out, QString::fromLatin1(setGroups[set]));
    group.writeEntry("BackgroundNormal", ColorTone::tint(scheme.background().color(), accent, amount));
    group.writeEntry("BackgroundAlternate",
                     ColorTone::tint(scheme.background(KColorScheme::AlternateBackground).color(), accent, amount));
    const std::pair<const char *, KColorScheme::ForegroundRole> foregrounds[] = {
        {"ForegroundNormal", KColorScheme::NormalText},     {"ForegroundInactive", KColorScheme::InactiveText},
        {"ForegroundActive", KColorScheme::ActiveText},     {"ForegroundLink", KColorScheme::LinkText},
        {"ForegroundVisited", KColorScheme::VisitedText},   {"ForegroundNegative", KColorScheme::NegativeText},
        {"ForegroundNeutral", KColorScheme::NeutralText},   {"ForegroundPositive", KColorScheme::PositiveText},
    };
    for (const auto &[key, role] : foregrounds)
        group.writeEntry(key, scheme.foreground(role).color());
    group.writeEntry("DecorationFocus", scheme.decoration(KColorScheme::FocusColor).color());
    group.writeEntry("DecorationHover", scheme.decoration(KColorScheme::HoverColor).color());
}

} // namespace

namespace ColorTone {

bool isNeutral(const QColor &background)
{
    return background.hslSaturationF() < 0.15;
}

QColor tint(const QColor &base, const QColor &accent, qreal amount)
{
    const auto mix = [amount](qreal a, qreal b) { return a + (b - a) * amount; };
    return QColor::fromRgbF(mix(base.redF(), accent.redF()), mix(base.greenF(), accent.greenF()),
                            mix(base.blueF(), accent.blueF()), base.alphaF());
}

QString apply()
{
    if (qEnvironmentVariable("YUNTING_TONE") == QLatin1String("0"))
        return {};
    const QColor window = KColorScheme(QPalette::Active, KColorScheme::Window).background().color();
    if (!isNeutral(window))
        return {};
    const QColor accent = KColorScheme(QPalette::Active, KColorScheme::Selection).background().color();
    const bool dark = window.lightnessF() < 0.5;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/tone.colors");
    QFile::remove(path);
    {
        KConfig out(path, KConfig::SimpleConfig);
        for (int set = 0; set < KColorScheme::NColorSets; ++set)
            writeSet(out, KColorScheme::ColorSet(set), accent, dark);
        // 停用 / 失焦时的颜色效果、对比度、旧式标题栏颜色沿用系统的
        const KSharedConfigPtr system = KSharedConfig::openConfig();
        for (const char *name : {"ColorEffects:Disabled", "ColorEffects:Inactive", "KDE", "WM"}) {
            KConfigGroup source(system, QString::fromLatin1(name));
            if (source.exists()) {
                KConfigGroup target(&out, QString::fromLatin1(name));
                source.copyTo(&target);
            }
        }
        KConfigGroup general(&out, QStringLiteral("General"));
        general.writeEntry("Name", QStringLiteral("云听"));
        out.sync();
    }
    qApp->setProperty("KDE_COLOR_SCHEME_PATH", path);
    qApp->setPalette(KColorScheme::createApplicationPalette(KSharedConfig::openConfig(path)));
    return path;
}

} // namespace ColorTone

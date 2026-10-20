#include "materialyou.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QFile>

#include "cpp/blend/blend.h"
#include "cpp/cam/hct.h"
#include "cpp/dynamiccolor/material_dynamic_colors.h"
#include "cpp/dynamiccolor/dynamic_scheme.h"
#include "cpp/palettes/tones.h"
#include "cpp/scheme/scheme_tonal_spot.h"

namespace {

namespace mcu = material_color_utilities;

QColor colorOf(mcu::DynamicColor role, const mcu::DynamicScheme &scheme)
{
    return QColor::fromRgba(role.GetArgb(scheme)).toRgb();
}

void writeSet(KConfig &out, const char *group, const QColor &background, const QColor &alternate,
              const QColor &foreground, const QColor &inactive, const QColor &active, const QColor &link,
              const QColor &visited, const QColor &negative, const QColor &neutral, const QColor &positive,
              const QColor &focus, const QColor &hover)
{
    KConfigGroup set(&out, QString::fromLatin1(group));
    set.writeEntry("BackgroundNormal", background);
    set.writeEntry("BackgroundAlternate", alternate);
    set.writeEntry("ForegroundNormal", foreground);
    set.writeEntry("ForegroundInactive", inactive);
    set.writeEntry("ForegroundActive", active);
    set.writeEntry("ForegroundLink", link);
    set.writeEntry("ForegroundVisited", visited);
    set.writeEntry("ForegroundNegative", negative);
    set.writeEntry("ForegroundNeutral", neutral);
    set.writeEntry("ForegroundPositive", positive);
    set.writeEntry("DecorationFocus", focus);
    set.writeEntry("DecorationHover", hover);
}

} // namespace

namespace MaterialYou {

Scheme schemeFor(const QColor &seed, bool dark)
{
    mcu::SchemeTonalSpot scheme(mcu::Hct(seed.rgba()), dark, 0.0);

    Scheme out;
    out.primary = colorOf(mcu::MaterialDynamicColors::Primary(), scheme);
    out.onPrimary = colorOf(mcu::MaterialDynamicColors::OnPrimary(), scheme);
    out.primaryContainer = colorOf(mcu::MaterialDynamicColors::PrimaryContainer(), scheme);
    out.onPrimaryContainer = colorOf(mcu::MaterialDynamicColors::OnPrimaryContainer(), scheme);
    out.secondary = colorOf(mcu::MaterialDynamicColors::Secondary(), scheme);
    out.onSecondary = colorOf(mcu::MaterialDynamicColors::OnSecondary(), scheme);
    out.secondaryContainer = colorOf(mcu::MaterialDynamicColors::SecondaryContainer(), scheme);
    out.onSecondaryContainer = colorOf(mcu::MaterialDynamicColors::OnSecondaryContainer(), scheme);
    out.secondaryFixed = colorOf(mcu::MaterialDynamicColors::SecondaryFixed(), scheme);
    out.tertiary = colorOf(mcu::MaterialDynamicColors::Tertiary(), scheme);
    out.surface = colorOf(mcu::MaterialDynamicColors::Surface(), scheme);
    out.surfaceDim = colorOf(mcu::MaterialDynamicColors::SurfaceDim(), scheme);
    out.surfaceBright = colorOf(mcu::MaterialDynamicColors::SurfaceBright(), scheme);
    out.surfaceContainerLowest = colorOf(mcu::MaterialDynamicColors::SurfaceContainerLowest(), scheme);
    out.surfaceContainerLow = colorOf(mcu::MaterialDynamicColors::SurfaceContainerLow(), scheme);
    out.surfaceContainer = colorOf(mcu::MaterialDynamicColors::SurfaceContainer(), scheme);
    out.surfaceContainerHigh = colorOf(mcu::MaterialDynamicColors::SurfaceContainerHigh(), scheme);
    out.surfaceContainerHighest = colorOf(mcu::MaterialDynamicColors::SurfaceContainerHighest(), scheme);
    out.surfaceVariant = colorOf(mcu::MaterialDynamicColors::SurfaceVariant(), scheme);
    out.onSurface = colorOf(mcu::MaterialDynamicColors::OnSurface(), scheme);
    out.onSurfaceVariant = colorOf(mcu::MaterialDynamicColors::OnSurfaceVariant(), scheme);
    out.outline = colorOf(mcu::MaterialDynamicColors::Outline(), scheme);
    out.outlineVariant = colorOf(mcu::MaterialDynamicColors::OutlineVariant(), scheme);
    out.inverseSurface = colorOf(mcu::MaterialDynamicColors::InverseSurface(), scheme);
    out.inversePrimary = colorOf(mcu::MaterialDynamicColors::InversePrimary(), scheme);

    // Breeze 的语义色按种子调和，再当作 error 调色板取 color / container 两个档位。
    // 与 kde-material-you-colors 的 static_color 同一做法：链接、错误、警告、成功、已访问
    // 跟着配色走，而不是固定用 Breeze 的蓝/红/橙/绿/紫。
    const auto extra = [&scheme, seed](const QColor &base, QColor &color, QColor &container) {
        scheme.error_palette = mcu::TonalPalette(mcu::BlendHarmonize(base.rgba(), seed.rgba()));
        color = colorOf(mcu::MaterialDynamicColors::Error(), scheme);
        container = colorOf(mcu::MaterialDynamicColors::ErrorContainer(), scheme);
    };
    extra(QColor(0x29, 0x80, 0xb9), out.link, out.linkContainer);
    extra(QColor(0xda, 0x44, 0x53), out.negative, out.negativeContainer);
    extra(QColor(0xf6, 0x74, 0x00), out.neutral, out.neutralContainer);
    extra(QColor(0x27, 0xae, 0x60), out.positive, out.positiveContainer);
    extra(QColor(0x9b, 0x59, 0xb6), out.visited, out.visitedContainer);
    return out;
}

QString writeKdeScheme(const Scheme &c, bool dark, const QString &path)
{
    QFile::remove(path);
    KConfig out(path, KConfig::SimpleConfig);

    // 底色与 kde-material-you-colors 的映射一致：View 是整窗的底色（浅色 surfaceBright、深色 surfaceDim），
    // Window/Header 取 surfaceContainer，Button 取 surfaceContainerHigh。
    // 文字比它高一档：正文 onSurface、次要文字 onSurfaceVariant。它用的 onSurfaceVariant / outline
    // 在 surfaceContainer 上只有 3:1 上下，读不清（materialyou_test 守着 4.5:1）
    writeSet(out, "Colors:View", dark ? c.surfaceDim : c.surfaceBright, c.surfaceContainer, c.onSurface,
             c.onSurfaceVariant, c.inverseSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary,
             dark ? c.inversePrimary : c.secondaryFixed);
    writeSet(out, "Colors:Window", c.surfaceContainer, c.surfaceVariant, c.onSurface, c.onSurfaceVariant, c.link,
             c.link, c.visited, c.negative, c.neutral, c.positive, c.primary, c.primary);
    writeSet(out, "Colors:Button", c.surfaceContainerHigh, c.surfaceVariant, c.onSurface, c.onSurfaceVariant,
             c.onSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary, c.primary);
    writeSet(out, "Colors:Tooltip", c.surfaceContainer, c.surfaceVariant, c.onSurface, c.onSurfaceVariant,
             c.onSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary, c.primary);
    writeSet(out, "Colors:Complementary", c.surfaceContainer, c.surface, c.onSurface, c.onSurfaceVariant,
             c.inverseSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary, c.primary);
    writeSet(out, "Colors:Header", c.surfaceContainer, c.surfaceContainer, c.onSurface, c.onSurfaceVariant,
             c.inverseSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary, c.primary);
    writeSet(out, "Colors:Header][Inactive", c.surfaceContainer, c.surfaceContainer, c.onSurface,
             c.onSurfaceVariant, c.inverseSurface, c.link, c.visited, c.negative, c.neutral, c.positive, c.primary,
             c.primary);
    writeSet(out, "Colors:Selection", c.primary, c.primary, c.onPrimary, c.onPrimary, c.onPrimary, c.linkContainer,
             c.visitedContainer, c.negativeContainer, c.neutralContainer, c.positiveContainer, c.primary,
             c.secondary);

    KConfigGroup disabled(&out, QStringLiteral("ColorEffects:Disabled"));
    disabled.writeEntry("Color", c.surfaceContainer);
    disabled.writeEntry("ColorAmount", 0.5);
    disabled.writeEntry("ColorEffect", 3);
    disabled.writeEntry("ContrastAmount", 0);
    disabled.writeEntry("ContrastEffect", 0);
    disabled.writeEntry("IntensityAmount", 0);
    disabled.writeEntry("IntensityEffect", 0);

    KConfigGroup inactive(&out, QStringLiteral("ColorEffects:Inactive"));
    inactive.writeEntry("ChangeSelectionColor", true);
    inactive.writeEntry("Color", c.surfaceContainerLowest);
    inactive.writeEntry("ColorAmount", 0.025);
    inactive.writeEntry("ColorEffect", 0);
    inactive.writeEntry("ContrastAmount", 0.1);
    inactive.writeEntry("ContrastEffect", 0);
    inactive.writeEntry("Enable", dark);
    inactive.writeEntry("IntensityAmount", 0);
    inactive.writeEntry("IntensityEffect", 0);

    KConfigGroup general(&out, QStringLiteral("General"));
    general.writeEntry("ColorScheme", dark ? QStringLiteral("YuntingMaterialYouDark")
                                           : QStringLiteral("YuntingMaterialYouLight"));
    general.writeEntry("Name", dark ? QStringLiteral("云听 Material You 深色")
                                    : QStringLiteral("云听 Material You 浅色"));
    general.writeEntry("shadeSortColumn", true);

    KConfigGroup kde(&out, QStringLiteral("KDE"));
    kde.writeEntry("contrast", 4);
    kde.writeEntry("frameContrast", 0.2);

    KConfigGroup wm(&out, QStringLiteral("WM"));
    wm.writeEntry("activeBackground", c.surfaceContainerHighest);
    wm.writeEntry("activeBlend", dark ? QColor(252, 252, 252) : QColor(227, 229, 231));
    wm.writeEntry("activeForeground", c.onSurface);
    wm.writeEntry("inactiveBackground", c.secondaryContainer);
    wm.writeEntry("inactiveBlend", dark ? QColor(161, 169, 177) : QColor(239, 240, 241));
    wm.writeEntry("inactiveForeground", dark ? c.onSecondaryContainer : c.onSurfaceVariant);

    out.sync();
    return path;
}

bool schemeNameIsMaterialYou(const QString &name)
{
    return name.contains(QLatin1String("MaterialYou"), Qt::CaseInsensitive)
        || name.contains(QLatin1String("Material You"), Qt::CaseInsensitive);
}

bool activeSchemeIsMaterialYou()
{
    const KSharedConfigPtr config = KSharedConfig::openConfig();
    config->reparseConfiguration();
    return schemeNameIsMaterialYou(
        config->group(QStringLiteral("General")).readEntry(QStringLiteral("ColorScheme")));
}

} // namespace MaterialYou

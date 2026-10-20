#pragma once
// 不用 kde-material-you-colors 时的配色：用系统强调色作种子，按 Material 3 的 Tonal Spot 方案算出整套颜色
//（Google material-color-utilities 的官方 C++ 实现，见 third_party/material-color-utilities/）。
// 它的方案正在生效时由它接管，这里只负责识别，不覆盖它的方案。
#include <QColor>
#include <QString>

namespace MaterialYou {

// KDE 配色方案用到的角色，以及 Breeze 语义色（链接、错误、警告、成功、已访问）的调和版本
struct Scheme {
    QColor primary;
    QColor onPrimary;
    QColor primaryContainer;
    QColor onPrimaryContainer;
    QColor secondary;
    QColor onSecondary;
    QColor secondaryContainer;
    QColor onSecondaryContainer;
    QColor secondaryFixed;
    QColor tertiary;
    QColor surface;
    QColor surfaceDim;
    QColor surfaceBright;
    QColor surfaceContainerLowest;
    QColor surfaceContainerLow;
    QColor surfaceContainer;
    QColor surfaceContainerHigh;
    QColor surfaceContainerHighest;
    QColor surfaceVariant;
    QColor onSurface;
    QColor onSurfaceVariant;
    QColor outline;
    QColor outlineVariant;
    QColor inverseSurface;
    QColor inversePrimary;
    QColor link;
    QColor linkContainer;
    QColor negative;
    QColor negativeContainer;
    QColor neutral;
    QColor neutralContainer;
    QColor positive;
    QColor positiveContainer;
    QColor visited;
    QColor visitedContainer;
};

// 由强调色生成浅色或深色方案（Material 3 Tonal Spot，对比度 0）
Scheme schemeFor(const QColor &seed, bool dark);

// 写一份 KDE 配色方案文件（.colors）；返回写入的路径
QString writeKdeScheme(const Scheme &scheme, bool dark, const QString &path);

// 配色方案名是否由 kde-material-you-colors 生成（MaterialYouLight/Dark、Material You Light/Dark 等）
bool schemeNameIsMaterialYou(const QString &name);


// 当前生效的 KDE 配色方案是否就是 kde-material-you-colors 的方案
bool activeSchemeIsMaterialYou();

} // namespace MaterialYou

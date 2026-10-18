#pragma once
// 中性配色下的色调：系统配色方案的背景几乎是灰的时候（Breeze 默认），按用户自己的强调色给背景染上一点色调
//（Material You 那种），作为本应用的配色方案生效。用的是 KDE 应用内配色的标准机制：写一份配色方案文件，
// 设 KDE_COLOR_SCHEME_PATH 与应用调色板，原生控件、菜单、对话框、标题栏一起变；深色、浅色都按用户的方案来。
// 系统方案本来就有色调（例如 Material You 配色）时什么也不做。YUNTING_TONE=0 关掉。
#include <QColor>
#include <QString>

namespace ColorTone {

// 创建任何窗口之前、QApplication 之后调用。返回生效的方案文件路径；没有改动时返回空
QString apply();

// 背景是否算中性（饱和度很低）：只有中性的方案才染色
bool isNeutral(const QColor &background);
// base 向 accent 混合 amount（0–1）
QColor tint(const QColor &base, const QColor &accent, qreal amount);

} // namespace ColorTone

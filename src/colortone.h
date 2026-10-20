#pragma once
// 应用配色：kde-material-you-colors 的方案在用就让它接管（系统方案原样生效）；否则用系统强调色
// 生成一套 Material 3 配色，作为本应用的配色方案。用的是 KDE 应用内配色的标准机制：写一份
// 配色方案文件，设 KDE_COLOR_SCHEME_PATH 与应用调色板，原生控件、菜单、对话框一起变。
// 系统方案本来就有色调（自定义方案、Material You 方案）时什么也不做。YUNTING_TONE=0 关掉。
#include <QColor>
#include <QString>

namespace ColorTone {

// 创建任何窗口之前、QApplication 之后调用。返回生效的方案文件路径；没有改动时返回空
QString apply();

// 背景是否算中性（饱和度很低）：只有中性的方案才生成 Material You 配色
bool isNeutral(const QColor &background);

} // namespace ColorTone

#pragma once
// 中日文用同一款字，并与系统一致。界面字体跟随系统，但系统字体（HarmonyOS Sans、Noto Sans）多半只有
// 拉丁字母，汉字、假名、全角标点靠 fontconfig 按语言回退，而回退按文字分开选：汉字按中文落到一款，
// 假名按日文落到另一款（常见的是 Noto Sans CJK JP），同一行「歌手ヨルシカ的新专辑」就成了两种字。
// 这里给应用字体排上第二个 family——一款同时有汉字与假名的中文字体，所有中日文都落在它上面：
//   原生运行：fontconfig 为界面语言排的第一款（用户在 ~/.config/fontconfig 里的选择也算在内）；
//   Flatpak 里：沙箱看得到宿主的字体文件、看不到用户的 fontconfig 规则，fontconfig 会退到沙箱默认的
//   文泉驿。这时先找系统字体的同族中文款（HarmonyOS Sans → HarmonyOS Sans SC，Noto Sans → Noto Sans CJK SC）。
// 不指定任何具体字体，也不需要额外的沙箱权限。

namespace UiFont {

// QApplication 之后、创建窗口之前调用；系统字体变了（平台主题改应用字体）会再排一次
void unifyCjk();

} // namespace UiFont

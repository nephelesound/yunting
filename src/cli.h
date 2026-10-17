#pragma once
// 命令行与界面启动：main() 设好应用身份后把控制权交到这里。
// CLI 子命令与无窗口调试模式在事件循环里处理 SIGTERM/SIGINT，走 aboutToQuit 正常退出。

class QApplication;

namespace Cli {
int run(QApplication &app);
}

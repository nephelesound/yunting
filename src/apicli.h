#pragma once
// 原生 API 调试 CLI：只读 GET + 端点清单。写操作仍走 --like / --playlist-selftest 等专用入口。
#include <QStringList>

class NcmClient;

// command = "list" 或 "get"；get 的 args 第一项是 path，其余是 key=value。
int runApiCommand(NcmClient &client, const QString &command, const QStringList &args, bool personal);

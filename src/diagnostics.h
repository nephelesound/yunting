#pragma once
// 诊断信息：最近的警告与错误日志留在内存里（不落盘），「关于」页一键复制，用户反馈问题时贴上。
// 接在 Qt 的日志处理链上：每条照常交给之前的处理器（journal / 终端），另外记一份。
// 只记警告以上与本应用的 info；日志本身不写 cookie 与查询参数（各处打日志时的约定），这里不再过滤。
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class Diagnostics : public QObject {
    Q_OBJECT
public:
    // 报告里的「流量」一段由网络出口给（NcmClient::trafficSummary）；不设就没有这一段
    std::function<QString()> traffic;

    // 进程里只装一次；之后创建的 Diagnostics 都读同一份记录
    static void install();
    // 测试用：直接记一条（与日志处理器记的格式相同）
    static void record(QtMsgType type, const char *category, const QString &message);

    using QObject::QObject;

    // 版本、Qt、平台与最近的日志，一段纯文本
    Q_INVOKABLE QString report() const;
    static QStringList recent();
};

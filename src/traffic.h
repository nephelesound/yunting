#pragma once
// 网络出口的流量计：NcmClient 发出每个请求前过一遍 admit()。
// 读请求（GET）按「路径 + 参数」计：1 秒内同一个请求第 4 次起拒绝。正常的轮询（一起听 1 秒一次、
// 扫码 2 秒一次）与失败重试都远低于此；超过它只可能是逻辑错误（循环、几处互相覆盖），
// 拒绝并告警，免得风暴打到网易、拖垮界面。写请求（POST）只计数：weapi 的请求体每次都不同，
// 连点红心是用户自己的操作。日志与摘要只写路径，不写参数（可能含搜索词）。
#include <QHash>
#include <QString>

#include <deque>

class TrafficMeter {
public:
    // now 是单调毫秒（测试里直接给）；返回 false = 不要发这个请求
    bool admit(const QString &method, const QString &path, const QString &query, qint64 now);
    // 最近一分钟的总数、峰值每秒、拒绝次数与最常请求的路径（全都只算这一分钟）：放进「复制诊断信息」
    QString summary(qint64 now) const;

    static constexpr int RepeatLimit = 3;         // 1 秒内同一个读请求最多发 3 次
    static constexpr int BurstPerSecond = 40;     // 1 秒内超过这么多请求就告警（不拒绝）

private:
    struct Sent {
        qint64 at;
        QString what;   // "GET api/song/lyric"
    };
    void forget(qint64 now);
    bool warnOnce(const QString &key, qint64 now);

    std::deque<Sent> m_lastMinute;
    std::deque<qint64> m_refusedAt;                  // 最近一分钟被拦下的时刻
    QHash<QString, std::deque<qint64>> m_sameRead;   // 路径 + 参数 → 最近 1 秒的发出时间
    QHash<QString, qint64> m_lastWarned;             // 每种告警 10 秒内只打一次，免得日志本身成为风暴（过期即删）
};

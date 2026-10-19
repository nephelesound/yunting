#include "traffic.h"

#include <QLoggingCategory>
#include <QStringList>

#include <algorithm>
#include <vector>

Q_LOGGING_CATEGORY(lcTraffic, "yunting.traffic", QtWarningMsg)

namespace {
constexpr qint64 Second = 1000;
constexpr qint64 Minute = 60 * Second;
constexpr qint64 WarnEvery = 10 * Second;
} // namespace

bool TrafficMeter::admit(const QString &method, const QString &path, const QString &query, qint64 now)
{
    forget(now);
    const QString what = method + u' ' + path;

    if (method == QLatin1String("GET")) {
        const QString key = path + u'?' + query;
        std::deque<qint64> &times = m_sameRead[key];
        while (!times.empty() && now - times.front() >= Second)
            times.pop_front();
        if (int(times.size()) >= RepeatLimit) {
            m_refusedAt.push_back(now);
            if (warnOnce(QStringLiteral("repeat ") + key, now))
                qCWarning(lcTraffic).noquote()
                    << "refused" << what << "- the same request" << RepeatLimit + 1
                    << "times within 1 s is a client bug (a loop or views overwriting each other)";
            return false;
        }
        times.push_back(now);
    }

    m_lastMinute.push_back({now, what});
    const auto inLastSecond = std::count_if(m_lastMinute.cbegin(), m_lastMinute.cend(),
                                            [now](const Sent &s) { return now - s.at < Second; });
    if (inLastSecond > BurstPerSecond && warnOnce(QStringLiteral("burst"), now))
        qCWarning(lcTraffic).noquote() << "request burst:" << inLastSecond << "requests within 1 s;"
                                       << summary(now).section(u'\n', 1);
    return true;
}

QString TrafficMeter::summary(qint64 now) const
{
    // 都只算最近一分钟：峰值是这一分钟里任意 1 秒窗口的最多请求数
    QHash<QString, int> counts;
    std::vector<qint64> times;
    for (const Sent &s : m_lastMinute) {
        if (now - s.at < Minute) {
            ++counts[s.what];
            times.push_back(s.at);
        }
    }
    const int total = int(times.size());
    int peak = 0;
    for (size_t first = 0, last = 0; last < times.size(); ++last) {
        while (times[last] - times[first] >= Second)
            ++first;
        peak = std::max(peak, int(last - first + 1));
    }
    const auto refused = std::count_if(m_refusedAt.cbegin(), m_refusedAt.cend(),
                                       [now](qint64 at) { return now - at < Minute; });
    QList<QPair<int, QString>> top;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        top.append({it.value(), it.key()});
    std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList busiest;
    for (qsizetype i = 0; i < std::min<qsizetype>(5, top.size()); ++i)
        busiest << QStringLiteral("%1 ×%2").arg(top.at(i).second).arg(top.at(i).first);
    return QStringLiteral("最近一分钟 %1 个请求，峰值 %2 个/秒，拒绝 %3 个重复请求\n%4")
        .arg(total).arg(peak).arg(refused)
        .arg(busiest.isEmpty() ? QStringLiteral("（没有请求）") : busiest.join(QStringLiteral("，")));
}

void TrafficMeter::forget(qint64 now)
{
    while (!m_lastMinute.empty() && now - m_lastMinute.front().at >= Minute)
        m_lastMinute.pop_front();
    while (!m_refusedAt.empty() && now - m_refusedAt.front() >= Minute)
        m_refusedAt.pop_front();
    for (auto it = m_lastWarned.begin(); it != m_lastWarned.end();)
        it = now - it.value() >= WarnEvery ? m_lastWarned.erase(it) : std::next(it);
    for (auto it = m_sameRead.begin(); it != m_sameRead.end();) {
        std::deque<qint64> &times = it.value();
        while (!times.empty() && now - times.front() >= Second)
            times.pop_front();
        it = times.empty() ? m_sameRead.erase(it) : std::next(it);
    }
}

bool TrafficMeter::warnOnce(const QString &key, qint64 now)
{
    const auto last = m_lastWarned.constFind(key);
    if (last != m_lastWarned.constEnd() && now - last.value() < WarnEvery)
        return false;
    m_lastWarned.insert(key, now);
    return true;
}

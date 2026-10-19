#include "traffic.h"

#include <QLoggingCategory>
#include <QStringList>

#include <algorithm>

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
            ++m_refused;
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
    m_peakPerSecond = std::max(m_peakPerSecond, int(inLastSecond));
    if (inLastSecond > BurstPerSecond && warnOnce(QStringLiteral("burst"), now))
        qCWarning(lcTraffic).noquote() << "request burst:" << inLastSecond << "requests within 1 s;"
                                       << summary(now).section(u'\n', 1);
    return true;
}

QString TrafficMeter::summary(qint64 now) const
{
    QHash<QString, int> counts;
    int total = 0;
    for (const Sent &s : m_lastMinute) {
        if (now - s.at < Minute) {
            ++counts[s.what];
            ++total;
        }
    }
    QList<QPair<int, QString>> top;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        top.append({it.value(), it.key()});
    std::sort(top.begin(), top.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList busiest;
    for (qsizetype i = 0; i < std::min<qsizetype>(5, top.size()); ++i)
        busiest << QStringLiteral("%1 ×%2").arg(top.at(i).second).arg(top.at(i).first);
    return QStringLiteral("最近一分钟 %1 个请求，峰值 %2 个/秒，拒绝 %3 个重复请求\n%4")
        .arg(total).arg(m_peakPerSecond).arg(m_refused)
        .arg(busiest.isEmpty() ? QStringLiteral("（没有请求）") : busiest.join(QStringLiteral("，")));
}

void TrafficMeter::forget(qint64 now)
{
    while (!m_lastMinute.empty() && now - m_lastMinute.front().at >= Minute)
        m_lastMinute.pop_front();
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

#pragma once
// Store 两个实现文件共用的 JSON / URL 小工具。只做访问缩写，不带策略。
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>

namespace ncm {

inline QJsonObject body(const QJsonDocument &doc) { return doc.object(); }

inline QJsonArray arr(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toArray();
}

// 这个 Qt 6.12 的 QJsonArray 没有 mid/sliced；逐个元素拷出前 n 个（QJsonValue 隐式共享，便宜）。
// n<=0 给空表，n 超长给全表。
inline QJsonArray take(const QJsonArray &a, qsizetype n)
{
    QJsonArray out;
    for (qsizetype i = 0; i < n && i < a.size(); ++i)
        out.append(a.at(i));
    return out;
}

// JSON 数字全走 double（QJson 默认），曲目 / 歌单 id 用起来一样
inline QString num(double v) { return QString::number(v, 'g', 17); }

// application/x-www-form-urlencoded：QUrlQuery 的默认 toString 不会编码 '+'，
// 而表单里的 '+' 会被服务端解成空格，所以每个 key/value 单独百分号编码。
inline QByteArray formEncoded(const QUrlQuery &form)
{
    QStringList parts;
    const auto items = form.queryItems(QUrl::FullyDecoded);
    for (const auto &item : items) {
        parts.append(QString::fromLatin1(QUrl::toPercentEncoding(item.first)) + u'='
                     + QString::fromLatin1(QUrl::toPercentEncoding(item.second)));
    }
    return parts.join(u'&').toLatin1();
}

// GET 的 query：QUrlQuery 不编码 '+'，服务端按表单规则会把它解成空格（搜索 "C++" 就错了），
// 所以 key/value 先各自百分号编码再交给 QUrlQuery——它不会再把 '%' 编成 %25。
inline QUrlQuery query(std::initializer_list<std::pair<const char *, QString>> pairs)
{
    QUrlQuery q;
    for (const auto &p : pairs) {
        q.addQueryItem(QString::fromLatin1(QUrl::toPercentEncoding(QLatin1String(p.first))),
                       QString::fromLatin1(QUrl::toPercentEncoding(p.second)));
    }
    return q;
}
} // namespace ncm

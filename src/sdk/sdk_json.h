#pragma once
// Qt JSON 之上的小型 typed reader。SDK 的所有字段读取都经过这里：
// - 缺失的字段返回给定的默认值；
// - 类型不符、必需字段缺失或自定义校验失败时，记录第一个错误与 JSON path；
// - 不做 QVariantMap 转换，调用方拿到的是明确的 QString / qint64 / QList 等类型。
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace ncm::sdk {

// 从 [{"name": "..."}] 数组取出非空名称；SDK 的曲目/节目共用的一个实现。
QStringList objectNameList(const QJsonArray &objects);

class JsonReader {
public:
    explicit JsonReader(const QJsonObject &object, QString path = {});

    bool has(const char *key) const;
    QString string(const char *key, QString fallback = {});
    double number(const char *key, double fallback = 0.0);
    qint64 integer(const char *key, qint64 fallback = 0);
    bool boolean(const char *key, bool fallback = false);
    QJsonArray array(const char *key);
    QJsonObject object(const char *key);
    QStringList stringList(const char *key);

    bool ok() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    QString path() const { return m_path; }
    // 解析器自己的业务校验：例如 id<=0 时调用 fail("songId must be positive")。
    void fail(QString message);

private:
    QJsonObject m_object;
    QString m_path;
    QString m_error;
    QString childPath(const char *key) const;
    void typeError(const char *key, const char *expected);
};

} // namespace ncm::sdk

#include "sdk/sdk_json.h"

namespace ncm::sdk {

QStringList objectNameList(const QJsonArray &objects)
{
    QStringList names;
    names.reserve(objects.size());
    for (const QJsonValue &value : objects) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            names.append(name);
    }
    return names;
}

JsonReader::JsonReader(const QJsonObject &object, QString path)
    : m_object(object), m_path(std::move(path))
{
}

QString JsonReader::childPath(const char *key) const
{
    return m_path.isEmpty() ? QString::fromLatin1(key)
                            : m_path + QLatin1Char('.') + QString::fromLatin1(key);
}

void JsonReader::fail(QString message)
{
    if (m_error.isEmpty())
        m_error = std::move(message);
}

void JsonReader::typeError(const char *key, const char *expected)
{
    fail(childPath(key) + QStringLiteral(": expected ") + QLatin1String(expected));
}

bool JsonReader::has(const char *key) const
{
    return m_object.contains(QLatin1String(key));
}

QString JsonReader::string(const char *key, QString fallback)
{
    const QJsonValue value = m_object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (!value.isString()) {
        typeError(key, "string");
        return fallback;
    }
    return value.toString();
}

double JsonReader::number(const char *key, double fallback)
{
    const QJsonValue value = m_object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (!value.isDouble()) {
        typeError(key, "number");
        return fallback;
    }
    return value.toDouble();
}

qint64 JsonReader::integer(const char *key, qint64 fallback)
{
    const double value = number(key, static_cast<double>(fallback));
    return static_cast<qint64>(value);
}

bool JsonReader::boolean(const char *key, bool fallback)
{
    const QJsonValue value = m_object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return fallback;
    if (!value.isBool()) {
        typeError(key, "boolean");
        return fallback;
    }
    return value.toBool();
}

QJsonArray JsonReader::array(const char *key)
{
    const QJsonValue value = m_object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isArray()) {
        typeError(key, "array");
        return {};
    }
    return value.toArray();
}

QJsonObject JsonReader::object(const char *key)
{
    const QJsonValue value = m_object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull())
        return {};
    if (!value.isObject()) {
        typeError(key, "object");
        return {};
    }
    return value.toObject();
}

QStringList JsonReader::stringList(const char *key)
{
    const QJsonArray values = array(key);
    QStringList out;
    out.reserve(values.size());
    for (qsizetype i = 0; i < values.size(); ++i) {
        const QJsonValue value = values.at(i);
        if (!value.isString()) {
            fail(childPath(key) + QStringLiteral("[%1]: expected string").arg(i));
            return out;
        }
        out.append(value.toString());
    }
    return out;
}

} // namespace ncm::sdk

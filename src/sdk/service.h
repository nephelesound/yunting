#pragma once
// Shared SDK transport seam: one place owns HTTP result classification, JSON
// envelope checks, and QObject callback lifetime. Endpoint modules provide only
// their form/parse function.
#include "sdk/sdk_result.h"
#include "sdk/sdk_json.h"

#include <QJsonObject>
#include <QObject>
#include <QUrlQuery>
#include <functional>

class NcmClient;

namespace ncm::sdk {

Error endpointError(ErrorKind kind, QString endpoint, QString message,
                    QString path = {}, int code = 0, int httpStatus = 0);

// Shared parse-error construction for the SDK's typed modules.
template <typename T>
Result<T> parseFailure(QString endpoint, const JsonReader &reader, QString path)
{
    return Result<T>::failure(endpointError(ErrorKind::Parse, std::move(endpoint),
                                            reader.error(), std::move(path)));
}

template <typename T>
Result<T> missingFailure(QString endpoint, QString path, QString what = QStringLiteral("array"))
{
    return Result<T>::failure(endpointError(
        ErrorKind::Parse, std::move(endpoint),
        path + QStringLiteral(": missing ") + what, std::move(path)));
}

template <typename T>
Result<T> itemTypeFailure(QString endpoint, QString path)
{
    return Result<T>::failure(endpointError(
        ErrorKind::Parse, std::move(endpoint),
        path + QStringLiteral(": expected object"), std::move(path)));
}

using JsonObjectCallback = std::function<void(Result<QJsonObject>)>;

// POST plaintext /api form. Calls back exactly once on the Qt thread; a callback
// whose context object is gone is dropped. Non-200 business codes are Api errors.
void postFormJson(NcmClient &client, QObject *context, QString endpoint,
                  QUrlQuery form, JsonObjectCallback callback, bool personal = false,
                  QString cookieContext = {});

// Same lifecycle/error policy for weapi POST. `personal` selects whether the
// stored MUSIC_U cookie is attached.
void postWeapiJson(NcmClient &client, QObject *context, QString endpoint,
                   QJsonObject payload, bool personal, JsonObjectCallback callback);

} // namespace ncm::sdk

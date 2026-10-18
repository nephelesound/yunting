#pragma once
// Shared SDK transport seam: one place owns HTTP result classification, JSON
// envelope checks, and QObject callback lifetime. Endpoint modules provide only
// their form/parse function.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QObject>
#include <QUrlQuery>
#include <functional>

class NcmClient;

namespace ncm::sdk {

Error endpointError(ErrorKind kind, QString endpoint, QString message,
                    QString path = {}, int code = 0, int httpStatus = 0);

using JsonObjectCallback = std::function<void(Result<QJsonObject>)>;

// POST plaintext /api form. Calls back exactly once on the Qt thread; a callback
// whose context object is gone is dropped. Non-200 business codes are Api errors.
void postFormJson(NcmClient &client, QObject *context, QString endpoint,
                  QUrlQuery form, JsonObjectCallback callback, bool personal = false);

// Same lifecycle/error policy for weapi POST. `personal` selects whether the
// stored MUSIC_U cookie is attached.
void postWeapiJson(NcmClient &client, QObject *context, QString endpoint,
                   QJsonObject payload, bool personal, JsonObjectCallback callback);

} // namespace ncm::sdk

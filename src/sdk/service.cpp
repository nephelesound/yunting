#include "sdk/service.h"

#include "ncmclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <utility>

namespace ncm::sdk {

Error endpointError(ErrorKind kind, QString endpoint, QString message,
                    QString path, int code)
{
    Error error;
    error.kind = kind;
    error.endpoint = std::move(endpoint);
    error.path = std::move(path);
    error.code = code;
    error.message = std::move(message);
    return error;
}

Error parseError(QString endpoint, QString message, QString path)
{
    return endpointError(ErrorKind::Parse, std::move(endpoint), std::move(message),
                         std::move(path));
}

namespace {

QString responseMessage(const QJsonObject &root)
{
    return root.value(QStringLiteral("message")).toString(
        root.value(QStringLiteral("msg")).toString());
}

int responseCode(const QJsonObject &root)
{
    return root.value(QStringLiteral("code")).isDouble()
        ? root.value(QStringLiteral("code")).toInt() : 0;
}

Result<QJsonObject> settled(const QJsonObject &root, const QString &endpoint)
{
    const int code = responseCode(root);
    if (code != 200)
        return Result<QJsonObject>::failure(endpointError(
            ErrorKind::Api, endpoint, responseMessage(root), {}, code));
    return Result<QJsonObject>::success(root);
}

} // namespace

void postFormJson(NcmClient &client, QObject *context, QString endpoint,
                  QUrlQuery form, JsonObjectCallback callback, bool personal)
{
    if (!callback)
        return;
    QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    client.postForm(endpoint, form, personal,
                    [guard, hasContext, endpoint, callback = std::move(callback)]
                    (const QJsonDocument &doc, bool ok) {
        if (hasContext && guard.isNull())
            return;
        if (!ok) {
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Network, endpoint, QStringLiteral("request failed"))));
            return;
        }
        callback(settled(doc.object(), endpoint));
    });
}

void postWeapiJson(NcmClient &client, QObject *context, QString endpoint,
                   QJsonObject payload, bool personal, JsonObjectCallback callback)
{
    if (!callback)
        return;
    QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    client.postWeapi(endpoint, payload, personal,
                     [guard, hasContext, endpoint, callback = std::move(callback)]
                     (const NcmClient::Response &response) {
        if (hasContext && guard.isNull())
            return;
        if (!response.ok || response.networkFailure) {
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Network, endpoint,
                response.networkFailure ? QStringLiteral("request failed")
                                        : responseMessage(response.doc.object()))));
            return;
        }
        callback(settled(response.doc.object(), endpoint));
    });
}

} // namespace ncm::sdk

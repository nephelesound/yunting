#include "sdk/service.h"

#include "ncmclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <utility>

namespace ncm::sdk {

Error endpointError(ErrorKind kind, QString endpoint, QString message,
                    QString path, int code, int httpStatus)
{
    Error error;
    error.kind = kind;
    error.endpoint = std::move(endpoint);
    error.path = std::move(path);
    error.code = code;
    error.httpStatus = httpStatus;
    error.message = std::move(message);
    return error;
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
                  QUrlQuery form, JsonObjectCallback callback, bool personal,
                  QString cookieContext)
{
    if (!callback)
        return;
    QPointer<QObject> guard(context);
    const bool hasContext = context != nullptr;
    auto onResponse = [guard, hasContext, endpoint, callback = std::move(callback)]
                    (const NcmClient::Response &response) {
        if (hasContext && guard.isNull())
            return;
        const QJsonObject root = response.doc.object();
        if (response.networkFailure) {
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Network, endpoint, QStringLiteral("request failed"))));
            return;
        }
        if (!response.ok) {
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Api, endpoint, responseMessage(root), {},
                responseCode(root), response.httpStatus)));
            return;
        }
        callback(settled(root, endpoint));
    };
    if (cookieContext.isEmpty())
        client.postForm(endpoint, form, personal, std::move(onResponse));
    else
        client.postFormWithContext(endpoint, form, personal, cookieContext,
                                   std::move(onResponse));
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
        if (response.networkFailure) {
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Network, endpoint, QStringLiteral("request failed"))));
            return;
        }
        if (!response.ok) {
            const QJsonObject root = response.doc.object();
            callback(Result<QJsonObject>::failure(endpointError(
                ErrorKind::Api, endpoint, responseMessage(root), {},
                responseCode(root), response.httpStatus)));
            return;
        }
        callback(settled(response.doc.object(), endpoint));
    });
}

} // namespace ncm::sdk

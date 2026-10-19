#include "sdk/pendant.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonObject>
#include <utility>

namespace ncm::sdk {
namespace {
constexpr auto PendantPath = "api/pendant/user/get";
}

Result<AvatarPendant> parseAvatarPendant(const QJsonObject &root)
{
    const QString rootPath = QStringLiteral("avatarPendant");
    JsonReader rootReader(root, rootPath);
    const QJsonObject data = rootReader.object("data");
    if (!rootReader.ok())
        return parseFailure<AvatarPendant>(QString::fromLatin1(PendantPath), rootReader,
                                           rootPath + QStringLiteral(".data"));

    JsonReader dataReader(data, rootPath + QStringLiteral(".data"));
    const QJsonValue pendantValue = data.value(QStringLiteral("pendantData"));
    if (!dataReader.ok())
        return parseFailure<AvatarPendant>(QString::fromLatin1(PendantPath), dataReader,
                                           rootPath + QStringLiteral(".data.pendantData"));
    if (pendantValue.isUndefined() || pendantValue.isNull()) {
        return Result<AvatarPendant>::failure(endpointError(
            ErrorKind::Api, QString::fromLatin1(PendantPath),
            QStringLiteral("no avatar pendant"), rootPath + QStringLiteral(".data.pendantData")));
    }
    if (!pendantValue.isObject())
        return itemTypeFailure<AvatarPendant>(QString::fromLatin1(PendantPath),
                                              rootPath + QStringLiteral(".data.pendantData"));

    JsonReader reader(pendantValue.toObject(), rootPath + QStringLiteral(".data.pendantData"));
    AvatarPendant pendant;
    pendant.id = reader.integer("id");
    pendant.name = reader.string("name");
    pendant.imageUrl = reader.string("imageUrl");
    pendant.androidUrl = reader.string("imageAndroidUrl");
    pendant.iosUrl = reader.string("imageIosUrl");
    pendant.pcUrl = reader.string("imagePCUrl");
    pendant.previewUrl = reader.string("previewImageUrl");
    pendant.type = static_cast<int>(reader.integer("type"));
    pendant.status = static_cast<int>(reader.integer("status"));
    if (!reader.ok())
        return parseFailure<AvatarPendant>(QString::fromLatin1(PendantPath), reader,
                                           rootPath + QStringLiteral(".data.pendantData"));
    return Result<AvatarPendant>::success(std::move(pendant));
}

void fetchAvatarPendant(NcmClient &client, QObject *context, AvatarPendantCallback callback)
{
    if (!callback)
        return;
    postFormJson(client, context, QString::fromLatin1(PendantPath), {},
                 [callback = std::move(callback)](Result<QJsonObject> root) {
        if (!root) {
            callback(Result<AvatarPendant>::failure(root.error));
            return;
        }
        callback(parseAvatarPendant(*root.value));
    }, true);
}

} // namespace ncm::sdk

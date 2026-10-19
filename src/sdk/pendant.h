#pragma once
// Current-user avatar pendant SDK.
//
// Raw endpoint:
//   GET /api/pendant/user/get   (personal MUSIC_U)
//
// The account/profile endpoints do not carry a pendant. This endpoint returns
// data.pendantData for the signed-in user (or null when none is worn). The
// regular image is `imageUrl`; Android/iOS have optional platform-specific
// assets. The desktop URL field is usually empty, so callers should prefer
// imageUrl and fall back to the platform variants.
#include "sdk/sdk_result.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>

class NcmClient;

namespace ncm::sdk {

struct AvatarPendant {
    qint64 id = 0;          // pendantData.id
    QString name;           // pendantData.name
    QString imageUrl;       // pendantData.imageUrl (raw)
    QString androidUrl;     // pendantData.imageAndroidUrl (raw)
    QString iosUrl;         // pendantData.imageIosUrl (raw)
    QString pcUrl;          // pendantData.imagePCUrl (raw, usually empty)
    QString previewUrl;     // pendantData.previewImageUrl (raw)
    int type = 0;           // pendantData.type
    int status = 0;         // pendantData.status
};

using AvatarPendantCallback = std::function<void(Result<AvatarPendant>)>;

void fetchAvatarPendant(NcmClient &client, QObject *context, AvatarPendantCallback callback);
Result<AvatarPendant> parseAvatarPendant(const QJsonObject &root);

} // namespace ncm::sdk

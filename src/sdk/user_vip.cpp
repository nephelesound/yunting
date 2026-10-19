#include "sdk/user_vip.h"

namespace ncm::sdk {

QString vipLevel(const QJsonObject &user)
{
    const QJsonObject rights = user.value(QStringLiteral("vipRights")).toObject();
    if (rights.value(QStringLiteral("redplus")).toObject()
            .value(QStringLiteral("rights")).toBool())
        return QStringLiteral("svip");
    if (rights.value(QStringLiteral("associator")).toObject()
            .value(QStringLiteral("rights")).toBool())
        return QStringLiteral("vip");
    if (user.value(QStringLiteral("vipType")).toDouble() > 0)
        return QStringLiteral("vip");
    return {};
}

} // namespace ncm::sdk

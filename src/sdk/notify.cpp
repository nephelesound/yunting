#include "sdk/notify.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonObject>
#include <QString>

namespace ncm::sdk {
namespace {

constexpr auto NotificationCountPath = "api/pl/count";

// JsonReader keeps the first field error; remember which field produced it so
// Error::path names the offending JSON key, not just the endpoint.
int countField(JsonReader &reader, const char *key, QString *badPath)
{
    const int value = static_cast<int>(reader.integer(key));
    if (!reader.ok() && badPath->isEmpty())
        *badPath = QString::fromLatin1(key);
    return value;
}

} // namespace

Result<NotificationCount> parseNotificationCount(const QJsonObject &root)
{
    JsonReader reader(root);
    NotificationCount count;
    QString badPath;
    count.msg = countField(reader, "msg", &badPath);
    count.comment = countField(reader, "comment", &badPath);
    count.event = countField(reader, "event", &badPath);
    count.follow = countField(reader, "follow", &badPath);
    count.notice = countField(reader, "notice", &badPath);
    count.forward = countField(reader, "forward", &badPath);
    count.newProgramCount = countField(reader, "newProgramCount", &badPath);
    if (!reader.ok())
        return parseFailure<NotificationCount>(NotificationCountPath, reader, badPath);
    return Result<NotificationCount>::success(count);
}

} // namespace ncm::sdk

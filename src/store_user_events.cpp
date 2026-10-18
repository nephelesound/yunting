// 用户动态：账号绑定、只读；别人的主页也能看，但匿名请求会被服务端拒绝。
#include "store.h"

#include "sdk/social.h"
#include "store_resource.h"

#include <QJsonObject>

using namespace ncm;

namespace {

// 动态里的用户只保留三个字段；头像 URL 保持原样，由界面决定尺寸。
QVariantMap eventUser(const sdk::EventUser &u)
{
    return {
        {"id", static_cast<double>(u.id)},
        {"name", u.name},
        {"avatar", u.avatar},
    };
}

QVariantMap eventResource(const sdk::EventResource &resource)
{
    const QString kind = resourceKind(resource.resourceType);
    const QString vid = kind == QLatin1String("video") && resource.id > 0
        ? QString::number(resource.id) : QString();
    return resourceMap(kind, resource.id, vid, resource.name, resource.cover, QString());
}

} // namespace

void Store::requestUserEvents(const QString &key, double uid, qint64 cursor)
{
    if (!m_client->hasMusicU() || !(uid > 0))
        return;
    const bool paging = cursor > 0;
    const quint64 epoch = m_epoch;
    const quint64 token = paging ? m_requests.token(QStringLiteral("userEvents"))
                                 : m_requests.begin(QStringLiteral("userEvents"));
    sdk::fetchUserEvents(*m_client, static_cast<qint64>(uid), cursor, 20, true, this,
                         [this, key, uid, cursor, paging, epoch, token]
                         (sdk::Result<sdk::EventPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("userEvents"), token))
            return;
        if (!result) {
            const QString restricted = result.error.kind == sdk::ErrorKind::Api
                ? privacyMessage(QJsonObject{{QStringLiteral("message"), result.error.message}})
                : QString();
            if (!restricted.isEmpty()) {
                // 与关注列表一致：隐私拒绝是页面内状态，不弹全局错误。
                put("userEvents", QVariantMap{
                    {"userId", uid},
                    {"next", static_cast<double>(cursor)},
                    {"more", false},
                    {"items", QVariantList{}},
                    {"restricted", true},
                    {"message", restricted},
                }, false);
                emit requestRestricted(key, restricted);
                return;
            }
            emit requestFailed(key, requestFailure(result.error, QStringLiteral("暂时取不到用户动态")));
            return;
        }
        const sdk::EventPage &page = *result.value;
        QVariantList items;
        for (const sdk::UserEvent &event : page.items) {
            items.append(QVariantMap{
                {"id", static_cast<double>(event.id)},
                {"action", event.actionName},
                // 用户自己写的话优先；没有正文时退回标题（SDK 已按 msg/title/资源名兜底）。
                {"text", event.description.isEmpty() ? event.title : event.description},
                {"time", static_cast<double>(event.eventTime)},
                {"user", eventUser(event.user)},
                {"resource", eventResource(event.resource)},
                {"likes", static_cast<double>(event.likeCount)},
                {"comments", static_cast<double>(event.commentCount)},
                {"shares", static_cast<double>(event.shareCount)},
            });
        }
        const bool samePerson = m_data.value("userEvents").toMap().value("userId").toDouble() == uid;
        putPage("userEvents", QVariantMap{
            {"userId", uid},
            {"next", static_cast<double>(page.lasttime)},
            {"more", page.more},
            {"items", items},
        }, paging && samePerson, false);
    });
}

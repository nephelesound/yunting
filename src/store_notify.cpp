// 通知计数与三类通知列表：计数走 /api/pl/count，系统/评论/@我 走 typed SDK，这里只做 QML 兼容形状。
#include "store.h"

#include "json_util.h"
#include "ncmclient.h"
#include "sdk/notification.h"
#include "sdk/notify.h"
#include "store_resource.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace ncm;

namespace {

QVariantMap notificationUser(const sdk::NotificationUser &user, qint64 fallbackId)
{
    const qint64 id = user.id > 0 ? user.id : fallbackId;
    return {
        {"id", static_cast<double>(id)},
        {"name", user.name},
        {"avatar", user.avatar},
    };
}

QVariantMap systemNoticeResource(const sdk::SystemNoticePayload &payload)
{
    if (payload.playlistId > 0) {
        return resourceMap(QStringLiteral("playlist"), payload.playlistId, {},
                           payload.playlistName, payload.playlistCover, {});
    }
    if (payload.resourceId > 0) {
        // specialResourceType 为 0 表示上游没给类型；此时不能按「0=歌曲」猜。
        const QString kind = payload.specialResourceType > 0
            ? resourceKind(payload.specialResourceType) : QStringLiteral("other");
        const QString vid = kind == QLatin1String("video")
            ? QString::number(payload.resourceId) : QString();
        return resourceMap(kind, payload.resourceId, vid, {}, {}, {});
    }
    if (payload.commentId > 0)
        return resourceMap(QStringLiteral("other"), payload.commentId, {}, {}, {}, {});
    return resourceMap(QStringLiteral("other"), 0, {}, {}, {}, {});
}

QString systemNoticeText(const sdk::SystemNoticePayload &payload)
{
    // 网易云通知 payload 的 type 号（非公开契约，取自 SDK 的捕获形状）：
    // 2 收藏歌单、3 关注、4 赞评论、5 回复评论、6 转发动态。
    // 1 及未知类型不猜：优先用 payload 自带的 actionDesc/content。
    switch (payload.type) {
    case 2:
        return QStringLiteral("收藏了你的歌单");
    case 3:
        return QStringLiteral("关注了你");
    case 4:
        return QStringLiteral("赞了你的评论");
    case 5:
        return QStringLiteral("回复了你的评论");
    case 6:
        return QStringLiteral("转发了你的动态");
    default:
        break;
    }
    if (!payload.actionDesc.isEmpty())
        return payload.actionDesc;
    if (!payload.content.isEmpty())
        return payload.content;
    if (!payload.commentContent.isEmpty())
        return QStringLiteral("评论了你的内容：%1").arg(payload.commentContent);
    return QStringLiteral("收到一条新通知");
}

QString notifyKey(const char *kind, qint64 cursor, bool paging)
{
    const QString base = QStringLiteral("notify:") + QLatin1String(kind);
    return paging ? base + u':' + QString::number(cursor) : base;
}

} // namespace

void Store::requestNotifications()
{
    if (!m_client->hasMusicU())
        return;
    const quint64 epoch = m_epoch;
    const quint64 token = m_requests.begin(QStringLiteral("notifications"));
    m_client->get(QStringLiteral("api/pl/count"), {}, true,
                  [this, epoch, token](const QJsonDocument &doc, bool ok) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("notifications"), token))
            return;
        const QString failure = requestFailure(doc, ok);
        if (!failure.isEmpty()) {
            emit requestFailed(QStringLiteral("notifications"), failure);
            return;
        }
        const QJsonObject r = body(doc);
        if (r.isEmpty()) {
            emit requestFailed(QStringLiteral("notifications"), QStringLiteral("暂时取不到通知计数"));
            return;
        }
        const auto parsed = sdk::parseNotificationCount(r);
        if (!parsed) {
            emit requestFailed(QStringLiteral("notifications"),
                               QStringLiteral("暂时取不到通知计数"));
            return;
        }
        const sdk::NotificationCount &count = *parsed.value;
        const int total = count.msg + count.comment + count.event + count.follow
                        + count.notice + count.forward;
        put("notifications", QVariantMap{
            {"msg", count.msg}, {"comment", count.comment}, {"event", count.event},
            {"follow", count.follow}, {"notice", count.notice}, {"forward", count.forward},
            {"newProgramCount", count.newProgramCount}, {"total", total},
        }, false);
    });
}

void Store::requestNotices(qint64 before)
{
    if (!m_client->hasMusicU())
        return;
    const bool paging = before > 0;
    const quint64 epoch = m_epoch;
    const quint64 token = paging ? m_requests.token(QStringLiteral("notices"))
                                 : m_requests.begin(QStringLiteral("notices"));
    sdk::fetchSystemNotices(*m_client, 30, before, this,
                            [this, before, paging, epoch, token]
                            (sdk::Result<sdk::SystemNoticePage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("notices"), token))
            return;
        if (!result) {
            emit requestFailed(notifyKey("notices", before, paging),
                               requestFailure(result.error, QStringLiteral("暂时取不到系统通知")));
            return;
        }
        const sdk::SystemNoticePage &page = *result.value;
        QVariantList items;
        for (const sdk::SystemNotice &notice : page.items) {
            // 外层 notice JSON 解析失败的行没有可读正文，跳过而不是塞空行。
            if (!notice.payloadValid)
                continue;
            items.append(QVariantMap{
                {"id", static_cast<double>(notice.id)},
                {"time", static_cast<double>(notice.time)},
                {"user", notificationUser(notice.payload.user, notice.userId)},
                {"text", systemNoticeText(notice.payload)},
                {"resource", systemNoticeResource(notice.payload)},
            });
        }
        putPage("notices", QVariantMap{
            {"next", static_cast<double>(page.nextTime)},
            {"more", page.more},
            {"items", items},
        }, paging, false);
    });
}

void Store::requestCommentNotices(qint64 before)
{
    // 接口要当前账号 uid：账号链还没回来时先不发，页面下次请求再取。
    if (!m_client->hasMusicU() || m_accountUid <= 0)
        return;
    const bool paging = before > 0;
    const quint64 epoch = m_epoch;
    const quint64 token = paging ? m_requests.token(QStringLiteral("commentNotices"))
                                 : m_requests.begin(QStringLiteral("commentNotices"));
    sdk::fetchCommentNotifications(*m_client, static_cast<qint64>(m_accountUid), 30, before, this,
                                   [this, before, paging, epoch, token]
                                   (sdk::Result<sdk::CommentNotificationPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("commentNotices"), token))
            return;
        if (!result) {
            emit requestFailed(notifyKey("comments", before, paging),
                               requestFailure(result.error, QStringLiteral("暂时取不到评论通知")));
            return;
        }
        const sdk::CommentNotificationPage &page = *result.value;
        QVariantList items;
        for (const sdk::CommentNotification &notification : page.items) {
            // 评论通知没有明确的资源 id；只把 resourceType 映射成 kind，其余留空。
            items.append(QVariantMap{
                {"id", notification.combinedId},
                {"time", static_cast<double>(notification.time)},
                {"user", notificationUser(notification.user, 0)},
                {"content", notification.content},
                {"repliedContent", notification.repliedContent},
                {"resource", resourceMap(resourceKind(notification.resourceType), 0, {},
                                         {}, {}, {})},
            });
        }
        putPage("commentNotices", QVariantMap{
            {"next", static_cast<double>(page.nextBeforeTime)},
            {"more", page.more},
            {"total", static_cast<double>(page.total)},
            {"items", items},
        }, paging, false);
    });
}

void Store::requestForwards(int offset)
{
    if (!m_client->hasMusicU() || offset < 0)
        return;
    const bool paging = offset > 0;
    const quint64 epoch = m_epoch;
    const quint64 token = paging ? m_requests.token(QStringLiteral("forwards"))
                                 : m_requests.begin(QStringLiteral("forwards"));
    sdk::fetchForwardNotifications(*m_client, 30, offset, this,
                                   [this, offset, paging, epoch, token]
                                   (sdk::Result<sdk::ForwardNotificationPage> result) {
        if (epoch != m_epoch || !m_requests.current(QStringLiteral("forwards"), token))
            return;
        if (!result) {
            emit requestFailed(notifyKey("forwards", offset, paging),
                               requestFailure(result.error, QStringLiteral("暂时取不到@我的消息")));
            return;
        }
        const sdk::ForwardNotificationPage &page = *result.value;
        QVariantList items;
        for (const sdk::ForwardNotification &notification : page.items) {
            const QString kind = resourceKind(notification.resourceKind);
            const QString vid = kind == QLatin1String("video") && notification.resourceId > 0
                ? QString::number(notification.resourceId) : QString();
            items.append(QVariantMap{
                {"id", static_cast<double>(notification.id)},
                {"time", static_cast<double>(notification.time)},
                {"user", notificationUser(notification.user, notification.userId)},
                {"message", notification.message},
                {"resource", resourceMap(kind, notification.resourceId, vid,
                                         notification.resourceTitle,
                                         notification.resourceCover,
                                         notification.resourceArtist)},
            });
        }
        putPage("forwards", QVariantMap{
            {"next", offset + static_cast<int>(page.items.size())},
            {"more", page.more},
            {"newCount", page.newCount},
            {"items", items},
        }, paging, false);
    });
}

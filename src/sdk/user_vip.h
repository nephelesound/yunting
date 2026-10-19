#pragma once
// 用户会员等级的唯一判定。网易云用户对象（评论作者、资料、关注/粉丝行、一起听房间成员）
// 都带 vipType 与 vipRights：vipRights.redplus.rights 为 true 是黑胶 SVIP；
// vipRights.associator.rights 为 true 或 vipType > 0 是黑胶 VIP。
// Store 的 QML map 只消费这里给出的三档字符串。
#include <QJsonObject>
#include <QString>

namespace ncm::sdk {

// ""（无会员）、"vip"（黑胶 VIP）、"svip"（黑胶 SVIP）。
// vipRights 缺失、为 null，或其中某一段为 null 时按没有该权益处理。
QString vipLevel(const QJsonObject &user);

} // namespace ncm::sdk

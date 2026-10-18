#pragma once
// Notification counter SDK — typed counts for /api/pl/count.
//
// Raw endpoint:
//   GET /api/pl/count    MUSIC_U attached; only signed-in callers get counters
//
// The response is a flat object of counters; absent (or null) counters normalize
// to 0. `total` is not a server field: the Store boundary sums the six
// notification counters (msg, comment, event, follow, notice, forward) for the
// badge.
#include "sdk/sdk_result.h"

#include <QJsonObject>

namespace ncm::sdk {

struct NotificationCount {
    int msg = 0;             // msg
    int comment = 0;         // comment
    int event = 0;           // event
    int follow = 0;          // follow
    int notice = 0;          // notice
    int forward = 0;         // forward
    int newProgramCount = 0; // newProgramCount (unread new programs)
};

// Pure parser, exposed for tests and callers that already hold a response.
Result<NotificationCount> parseNotificationCount(const QJsonObject &root);

} // namespace ncm::sdk

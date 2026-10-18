#include "sdk/song_wiki_service.h"

#include "ncmclient.h"
#include "sdk/service.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QUrlQuery>
#include <memory>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto WikiPath = "api/link/page/parent/relation/construct/info";
constexpr auto CreatorsPath = "api/song/creators";
constexpr auto ChorusPath = "api/song/chorus";

} // namespace

void fetchSongWiki(NcmClient &client, SongWikiRequest request, QObject *context,
                   SongWikiCallback callback)
{
    if (request.songId <= 0) {
        if (callback)
            callback(Result<SongWiki>::failure(
                endpointError(ErrorKind::InvalidRequest, WikiPath,
                              QStringLiteral("songId must be positive"))));
        return;
    }

    struct State {
        qint64 songId = 0;
        QJsonObject wiki;
        QJsonObject creators;
        QJsonObject chorus;
        int pending = 3;
        bool done = false;
        bool hasContext = false;
        QPointer<QObject> context;
        SongWikiCallback callback;
    };
    auto state = std::make_shared<State>();
    state->songId = request.songId;
    state->hasContext = context != nullptr;
    state->context = context;
    state->callback = std::move(callback);

    const auto finish = [state](Result<SongWiki> result) {
        if (state->done)
            return;
        state->done = true;
        if (state->hasContext && state->context.isNull())
            return;
        if (state->callback)
            state->callback(std::move(result));
    };
    const auto optionalSettled = [state, finish] {
        if (--state->pending != 0 || state->done)
            return;
        finish(SongWiki::fromApi(state->wiki, state->creators, state->chorus,
                                 state->songId));
    };

    QJsonObject playing{
        {"current", QString::number(state->songId)},
        {"scene", QStringLiteral("songWiki")},
    };
    QJsonObject ext{{"states", QJsonObject{{"playingResource", playing}}}};
    QUrlQuery wikiForm;
    wikiForm.addQueryItem(QStringLiteral("extJson"),
                          QString::fromUtf8(QJsonDocument(ext).toJson(QJsonDocument::Compact)));
    wikiForm.addQueryItem(QStringLiteral("positionCode"), QStringLiteral("songWikiMainPosition"));
    postFormJson(client, context, QString::fromLatin1(WikiPath), wikiForm,
                 [state, finish, optionalSettled](Result<QJsonObject> root) {
        if (!root) {
            finish(Result<SongWiki>::failure(root.error));
            return;
        }
        state->wiki = *root.value;
        optionalSettled();
    }, client.hasMusicU());

    QUrlQuery creatorsForm;
    creatorsForm.addQueryItem(QStringLiteral("songId"), QString::number(state->songId));
    postFormJson(client, context, QString::fromLatin1(CreatorsPath), creatorsForm,
                 [state, optionalSettled](Result<QJsonObject> root) {
        if (root)
            state->creators = *root.value;
        optionalSettled();
    }, client.hasMusicU());

    QUrlQuery chorusForm;
    chorusForm.addQueryItem(QStringLiteral("ids"),
                            QStringLiteral("[%1]").arg(state->songId));
    postFormJson(client, context, QString::fromLatin1(ChorusPath), chorusForm,
                 [state, optionalSettled](Result<QJsonObject> root) {
        if (root)
            state->chorus = *root.value;
        optionalSettled();
    }, client.hasMusicU());
}

} // namespace ncm::sdk

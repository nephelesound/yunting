#include "sdk/song_wiki_service.h"

#include "ncmclient.h"

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

Error endpointError(ErrorKind kind, const char *endpoint, const QString &message,
                    int code = 0)
{
    Error error;
    error.kind = kind;
    error.endpoint = QString::fromLatin1(endpoint);
    error.code = code;
    error.message = message;
    return error;
}

int responseCode(const QJsonObject &root)
{
    if (!root.value(QStringLiteral("code")).isDouble())
        return 0;
    return root.value(QStringLiteral("code")).toInt();
}

QString responseMessage(const QJsonObject &root)
{
    return root.value(QStringLiteral("message")).toString(
        root.value(QStringLiteral("msg")).toString());
}

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
    const auto fail = [finish](ErrorKind kind, const char *endpoint,
                               const QString &message, int code = 0) {
        finish(Result<SongWiki>::failure(endpointError(kind, endpoint, message, code)));
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
    client.postForm(QString::fromLatin1(WikiPath), wikiForm, client.hasMusicU(),
                    [state, finish, fail, optionalSettled](const QJsonDocument &doc, bool ok) {
        const QJsonObject root = doc.object();
        if (!ok) {
            fail(ErrorKind::Network, WikiPath, QStringLiteral("wiki request failed"));
            return;
        }
        const int code = responseCode(root);
        if (code != 200) {
            fail(ErrorKind::Api, WikiPath, responseMessage(root), code);
            return;
        }
        state->wiki = root;
        optionalSettled();
    });

    QUrlQuery creatorsForm;
    creatorsForm.addQueryItem(QStringLiteral("songId"), QString::number(state->songId));
    client.postForm(QString::fromLatin1(CreatorsPath), creatorsForm, false,
                    [state, optionalSettled](const QJsonDocument &doc, bool ok) {
        const QJsonObject root = doc.object();
        if (ok && responseCode(root) == 200)
            state->creators = root;
        optionalSettled();
    });

    QUrlQuery chorusForm;
    chorusForm.addQueryItem(QStringLiteral("ids"),
                            QStringLiteral("[%1]").arg(state->songId));
    client.postForm(QString::fromLatin1(ChorusPath), chorusForm, false,
                    [state, optionalSettled](const QJsonDocument &doc, bool ok) {
        const QJsonObject root = doc.object();
        if (ok && responseCode(root) == 200)
            state->chorus = root;
        optionalSettled();
    });
}

} // namespace ncm::sdk

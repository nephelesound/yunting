#include "sdk/lyrics.h"

#include "sdk/sdk_json.h"
#include "sdk/service.h"

#include <QJsonObject>
#include <utility>

namespace ncm::sdk {
namespace {

constexpr auto LyricsPath = "api/song/lyric/v1";

// One `<format>.lyric` string. The container and the string are both optional
// (missing/null means this format is not available); only a wrong JSON type is
// an error, reported with the path of the offending field.
Result<QString> lyricText(const QJsonObject &root, const char *key)
{
    JsonReader rootReader(root);
    const QJsonObject container = rootReader.object(key);
    if (!rootReader.ok())
        return parseFailure<QString>(QString::fromLatin1(LyricsPath), rootReader,
                                     QString::fromLatin1(key));

    JsonReader reader(container, QString::fromLatin1(key));
    const QString text = reader.string("lyric");
    if (!reader.ok())
        return parseFailure<QString>(QString::fromLatin1(LyricsPath), reader,
                                     QString::fromLatin1(key)
                                         + QStringLiteral(".lyric"));
    return Result<QString>::success(text);
}

} // namespace

Result<SongLyrics> parseSongLyrics(const QJsonObject &root, qint64 songId)
{
    // The Store issues this request through the plain GET path rather than the
    // SDK transport, so the envelope check lives here. A non-numeric code is a
    // parse error rather than a silent coercion to 0.
    JsonReader rootReader(root);
    const qint64 code = rootReader.integer("code");
    if (!rootReader.ok())
        return parseFailure<SongLyrics>(QString::fromLatin1(LyricsPath), rootReader,
                                        QStringLiteral("code"));
    if (code != 200)
        return Result<SongLyrics>::failure(endpointError(
            ErrorKind::Api, QString::fromLatin1(LyricsPath),
            QStringLiteral("code %1").arg(code), {}, static_cast<int>(code)));

    const auto lrc = lyricText(root, "lrc");
    if (!lrc)
        return Result<SongLyrics>::failure(lrc.error);
    const auto tlyric = lyricText(root, "tlyric");
    if (!tlyric)
        return Result<SongLyrics>::failure(tlyric.error);
    const auto yrc = lyricText(root, "yrc");
    if (!yrc)
        return Result<SongLyrics>::failure(yrc.error);
    const auto ytlrc = lyricText(root, "ytlrc");
    if (!ytlrc)
        return Result<SongLyrics>::failure(ytlrc.error);

    JsonReader flags(root);
    const bool pureMusic = flags.boolean("pureMusic");
    const bool noLyric = flags.boolean("nolyric");
    const bool uncollected = flags.boolean("uncollected");
    if (!flags.ok())
        return parseFailure<SongLyrics>(QString::fromLatin1(LyricsPath), flags,
                                        QStringLiteral("pureMusic/nolyric/uncollected"));

    SongLyrics lyrics;
    lyrics.songId = songId;
    lyrics.pureMusic = pureMusic || noLyric;
    lyrics.uncollected = uncollected;
    lyrics.lrc = *lrc.value;
    lyrics.tlyric = *tlyric.value;
    lyrics.yrc = *yrc.value;
    lyrics.ytlrc = *ytlrc.value;
    return Result<SongLyrics>::success(std::move(lyrics));
}

} // namespace ncm::sdk

#include "audioengine.h"
#include "json_util.h"
#include "ncmclient.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cstdio>

using namespace ncm;

namespace {
// 对数（滑条）刻度 → QAudioOutput 的线性刻度
double applyVolumeScale(double slider)
{
    return QAudio::convertVolume(slider, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
}
} // namespace

AudioEngine::AudioEngine(NcmClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    m_audio = new QAudioOutput(this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(applyVolumeScale(m_volume));

    connect(m_player, &QMediaPlayer::positionChanged, this, [this] {
        emit positionChanged();
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this] {
        emit durationChanged();
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            scrobbleFinish();
            setPlaying(false);
            emit finished();
        }
    });
    // 真正开始出声才打卡：地址还在解析时不算听过
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            scrobbleStart();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &errorString) {
        if (m_failed)
            return;
        m_failed = true;
        emit failed(QStringLiteral("暂时无法播放这首歌"));
        qWarning() << "playback error:" << errorString;
    });
}

double AudioEngine::position() const
{
    return m_player->position() / 1000.0;
}

double AudioEngine::duration() const
{
    const double media = m_player->duration() / 1000.0;
    return media > 0 ? media : m_track.value("dur").toDouble();
}

void AudioEngine::setTrack(const QVariantMap &track)
{
    if (track == m_track)
        return;
    // 切歌前把上一首的收听时长上报（位置 >= 1 秒才算）
    scrobbleFinish();
    m_track = track;
    m_failed = false;
    m_scrobbleStarted = false;
    m_scrobbleFinished = false;
    m_player->stop();
    m_player->setSource(QUrl());
    emit trackChanged();
    emit positionChanged();
    emit durationChanged();
    if (!track.isEmpty())
        resolveUrl(track.value("id").toDouble());
}

void AudioEngine::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    if (playing) {
        if (m_player->source().isValid())
            m_player->play();
        // 地址还没取到时只记意图，resolveUrl 落地后开播
    } else {
        m_player->pause();
        // 暂停也结算一次；恢复后不会重复上报
        scrobbleFinish();
    }
    emit playingChanged();
}

void AudioEngine::setVolume(double volume)
{
    const double clamped = std::clamp(volume, 0.0, 1.0);
    if (clamped == m_volume)
        return;
    m_volume = clamped;
    m_audio->setVolume(applyVolumeScale(clamped));
    emit volumeChanged();
}

void AudioEngine::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    m_audio->setMuted(muted);
    emit mutedChanged();
}

void AudioEngine::setQuality(const QString &quality)
{
    static const QStringList levels{
        QStringLiteral("auto"), QStringLiteral("standard"), QStringLiteral("higher"),
        QStringLiteral("exhigh"), QStringLiteral("lossless"), QStringLiteral("hires"),
    };
    const QString normalized = levels.contains(quality) ? quality : QStringLiteral("auto");
    if (normalized == m_quality)
        return;
    m_quality = normalized;
    emit qualityChanged();
}

QString AudioEngine::effectiveLevel() const
{
    if (m_quality != QLatin1String("auto"))
        return m_quality;
    const QString quality = m_track.value("quality").toString();
    if (quality == QLatin1String("hires"))
        return QStringLiteral("hires");
    if (quality == QLatin1String("lossless"))
        return QStringLiteral("lossless");
    return QStringLiteral("exhigh");
}

void AudioEngine::toggle()
{
    setPlaying(!m_playing);
}

void AudioEngine::seek(double seconds)
{
    const double d = duration();
    const qint64 ms = qint64(std::max(0.0, std::min(d > 0 ? d : seconds, seconds)) * 1000);
    if (m_player->source().isValid()) {
        m_player->setPosition(ms);
        emit seeked();
    } else {
        m_pendingSeek = ms; // 地址未落地，开播前应用
    }
}

// 播放地址有时效（接口给 expi 秒数，通常 20 分钟）：时效内重播、切回同一首都直接用缓存。
// 用 v1 的 level 而不是旧的 br：旧接口最高只到 lossless，v1 才能请求 hires。
void AudioEngine::resolveUrl(double songId)
{
    const QString level = effectiveLevel();
    const QString cacheKey = QStringLiteral("%1:%2").arg(num(songId), level);
    const auto cached = m_urls.constFind(cacheKey);
    if (cached != m_urls.constEnd() && QDateTime::currentDateTimeUtc() < cached->expires) {
        startSource(cached->url);
        return;
    }

    const bool lossless = level == QLatin1String("lossless") || level == QLatin1String("hires");
    m_client->postWeapi("song/enhance/player/url/v1",
                        QJsonObject{{"ids", QStringLiteral("[%1]").arg(num(songId))},
                                    {"level", level},
                                    {"encodeType", lossless ? QStringLiteral("flac") : QStringLiteral("mp3")}},
                        true,
                        [this, songId, cacheKey](const QJsonDocument &doc, bool ok) {
        const QJsonObject resp = body(doc);
        const QJsonObject item = ok && resp.value("code").toInt() == 200
            ? arr(resp, "data").at(0).toObject() : QJsonObject();
        const QString url = item.value("url").toString();
        if (!url.isEmpty()) {
            // 留一分钟余量，避免拿到即将失效的地址
            const int expi = item.value("expi").toInt(1200);
            m_urls.insert(cacheKey, {url, QDateTime::currentDateTimeUtc().addSecs(std::max(0, expi - 60))});
        }
        // 用户可能已经切到别的歌：迟到的地址只进缓存，不播
        if (m_track.value("id").toDouble() != songId)
            return;
        if (url.isEmpty()) {
            m_failed = true;
            emit failed(QStringLiteral("暂时无法播放这首歌"));
            return;
        }
        startSource(url);
    });
}

void AudioEngine::startSource(const QString &url)
{
    m_player->setSource(url);
    if (m_pendingSeek >= 0) {
        m_player->setPosition(m_pendingSeek);
        m_pendingSeek = -1;
        emit seeked();
    }
    if (m_playing)
        m_player->play();
}

void AudioEngine::scrobbleStart()
{
    if (m_scrobbleStarted || m_track.isEmpty() || !m_client->hasMusicU())
        return;
    m_scrobbleStarted = true;
    sendWeblog("startplay", 0);
    emit scrobbled(m_track);
}

void AudioEngine::scrobbleFinish()
{
    if (!m_scrobbleStarted || m_scrobbleFinished || m_track.isEmpty() || !m_client->hasMusicU())
        return;
    int seconds = qRound(position());
    if (seconds <= 0 && m_player->mediaStatus() == QMediaPlayer::EndOfMedia)
        seconds = qRound(duration());
    if (seconds <= 0)
        return;
    m_scrobbleFinished = true;
    sendWeblog("play", seconds);
}

void AudioEngine::sendWeblog(const char *action, int seconds)
{
    const double sourceId = m_track.value("sourceId").toDouble();
    QJsonObject json{
        {"id", m_track.value("id").toDouble()},
        {"type", QStringLiteral("song")},
        {"mainsite", QStringLiteral("1")},
        {"mainsiteWeb", QStringLiteral("1")},
        {"content", QStringLiteral("id=%1").arg(num(sourceId))},
    };
    if (QLatin1String(action) == QLatin1String("play")) {
        json.insert("download", 0);
        json.insert("end", QStringLiteral("playend"));
        json.insert("sourceId", num(sourceId));
        json.insert("time", seconds);
        json.insert("wifi", 0);
        json.insert("source", QStringLiteral("list"));
    }
    const QJsonArray logs{QJsonObject{{"action", QString::fromLatin1(action)}, {"json", json}}};
    const QString logsJson = QString::fromUtf8(QJsonDocument(logs).toJson(QJsonDocument::Compact));
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("logs"), logsJson);
    m_client->postForm(QStringLiteral("api/feedback/weblog"), form, true, nullptr);
}

int AudioEngine::smokeTest(double songId, int seconds)
{
    QVariantMap track{{"id", songId}, {"dur", 0}};
    setTrack(track);
    setPlaying(true);
    std::printf("[smoke-audio] resolving url for %lld\n", qlonglong(songId));
    std::fflush(stdout);

    // 状态变化全部打到 stdout：取址失败 / 源加载完成（duration 到位）/ 播放进度
    connect(this, &AudioEngine::failed, this, [](const QString &reason) {
        std::printf("[smoke-audio] failed: %s\n", qPrintable(reason));
        std::fflush(stdout);
        QCoreApplication::exit(1);
    });
    connect(this, &AudioEngine::durationChanged, this, [this] {
        std::printf("[smoke-audio] source loaded, duration=%.1fs\n", duration());
        std::fflush(stdout);
    });
    QTimer *report = new QTimer(this);
    connect(report, &QTimer::timeout, this, [this] {
        std::printf("[smoke-audio] playing=%d position=%.1f\n", m_playing ? 1 : 0, position());
        std::fflush(stdout);
    });
    report->start(1000);

    // 先结算打卡再退出，给请求留 2 秒发出（smoke 用来验证「最近播放」确实被写）
    QTimer::singleShot(seconds * 1000, this, [this] { scrobbleFinish(); });
    QTimer::singleShot((seconds + 2) * 1000, this, [] { QCoreApplication::exit(0); });
    return 0;
}

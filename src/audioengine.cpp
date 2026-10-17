#include "audioengine.h"
#include "json_util.h"
#include "ncmclient.h"

#include <QAudioOutput>
#include <QCoreApplication>
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
            setPlaying(false);
            emit finished();
        }
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
    m_track = track;
    m_failed = false;
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

// 播放地址有时效（接口给 expi 秒数，通常 20 分钟）：时效内重播、切回同一首都直接用缓存
void AudioEngine::resolveUrl(double songId)
{
    const auto cached = m_urls.constFind(songId);
    if (cached != m_urls.constEnd() && QDateTime::currentDateTimeUtc() < cached->expires) {
        startSource(cached->url);
        return;
    }
    m_client->get("api/song/enhance/player/url",
                  query({{"ids", QStringLiteral("[%1]").arg(num(songId))}, {"br", "320000"}}), true,
                  [this, songId](const QJsonDocument &doc, bool ok) {
        const QJsonObject item = ok ? arr(body(doc), "data").at(0).toObject() : QJsonObject();
        const QString url = item.value("url").toString();
        if (!url.isEmpty()) {
            // 留一分钟余量，避免拿到即将失效的地址
            const int expi = item.value("expi").toInt(1200);
            m_urls.insert(songId, {url, QDateTime::currentDateTimeUtc().addSecs(std::max(0, expi - 60))});
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

    QTimer::singleShot(seconds * 1000, this, [] { QCoreApplication::exit(0); });
    return 0;
}

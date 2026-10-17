#include "audioengine.h"
#include "json_util.h"
#include "ncmclient.h"
#include "perf.h"
#ifdef YUNTING_EXPERIMENTAL_EQ
#include "eqaudiosink.h"
#include <QAudioBufferOutput>
#endif
#ifdef YUNTING_NCBL
#include "ncbl.h"
#endif

#include <QAudioOutput>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrlQuery>
#include <QPointer>
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
    m_player = new QMediaPlayer(this);
#ifdef YUNTING_EXPERIMENTAL_EQ
    // DSP 链路：解码后的 PCM 由 QAudioBufferOutput 交给 EqAudioSink（默认启用）。
    m_eqSink = new EqAudioSink(this);
    m_bufferOutput = new QAudioBufferOutput(this);
    m_player->setAudioBufferOutput(m_bufferOutput);
    connect(m_bufferOutput, &QAudioBufferOutput::audioBufferReceived,
            m_eqSink, &EqAudioSink::bufferReceived);
    connect(m_eqSink, &EqAudioSink::unavailable, this, [this](const QString &reason) {
        if (m_audio)
            return;
        qWarning() << "eq unavailable, falling back to QAudioOutput:" << reason;
        m_player->setAudioBufferOutput(nullptr);
        m_audio = new QAudioOutput(this);
        m_player->setAudioOutput(m_audio);
        m_audio->setVolume(applyVolumeScale(m_volume));
        m_audio->setMuted(m_muted);
    });
#else
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    m_audio->setVolume(applyVolumeScale(m_volume));
#endif

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
        if (state == QMediaPlayer::PlayingState) {
            if (!m_playClockRunning) {
                m_playClock.start();
                m_playClockRunning = true;
            }
            if (!m_firstAudioLogged && !m_track.isEmpty()) {
                m_firstAudioLogged = true;
                qCDebug(lcPerf).nospace() << "audio first track="
                                          << qlonglong(m_track.value("id").toDouble())
                                          << " ms=" << m_trackClock.elapsed();
            }
            scrobbleStart();
        } else if (m_playClockRunning) {
            m_playedMs += m_playClock.elapsed();
            m_playClockRunning = false;
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
    // 切歌前把上一首的收听时长上报（位置 >= 1 秒才算）
    scrobbleFinish();
    m_track = track;
    m_pendingSeek = -1;   // 上一首的跳转只属于上一首，不能落到新曲目上
    m_failed = false;
    m_scrobbleStarted = false;
    m_scrobbleFinished = false;
    m_playedMs = 0;
    m_playClockRunning = false;
    m_firstAudioLogged = false;
    m_trackClock.start();
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
#ifdef YUNTING_EXPERIMENTAL_EQ
    if (m_eqSink)
        m_eqSink->setVolume(clamped);
#endif
    if (m_audio)
        m_audio->setVolume(applyVolumeScale(clamped));
    emit volumeChanged();
}

void AudioEngine::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
#ifdef YUNTING_EXPERIMENTAL_EQ
    if (m_eqSink)
        m_eqSink->setMuted(muted);
#endif
    if (m_audio)
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

void AudioEngine::setEqGain(int band, double db)
{
    if (band < 0 || band >= m_eqGains.size())
        return;
    const double clamped = std::clamp(db, -12.0, 12.0);
    if (qFuzzyCompare(m_eqGains.at(band).toDouble(), clamped))
        return;
    m_eqGains[band] = clamped;
#ifdef YUNTING_EXPERIMENTAL_EQ
    if (m_eqSink)
        m_eqSink->setGain(band, clamped);
#endif
    emit eqGainsChanged();
}

void AudioEngine::resetEq()
{
    for (int band = 0; band < m_eqGains.size(); ++band)
        setEqGain(band, 0.0);
    setBassBoost(0);
    setStereoWidth(1);
}

void AudioEngine::setBassBoost(double db)
{
    const double clamped = std::clamp(db, -12.0, 12.0);
    if (qFuzzyCompare(m_bassBoost, clamped))
        return;
    m_bassBoost = clamped;
#ifdef YUNTING_EXPERIMENTAL_EQ
    if (m_eqSink)
        m_eqSink->setBassBoost(clamped);
#endif
    emit bassBoostChanged();
}

void AudioEngine::setStereoWidth(double width)
{
    const double clamped = std::clamp(width, 0.0, 2.0);
    if (qFuzzyCompare(m_stereoWidth, clamped))
        return;
    m_stereoWidth = clamped;
#ifdef YUNTING_EXPERIMENTAL_EQ
    if (m_eqSink)
        m_eqSink->setStereoWidth(clamped);
#endif
    emit stereoWidthChanged();
}

void AudioEngine::applyEqPreset(const QString &preset)
{
    static const QHash<QString, QPair<QVariantList, QPair<double, double>>> presets = {
        {QStringLiteral("flat"),    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 1.0}}},
        {QStringLiteral("pop"),     {{-1, 0, 1, 2, 3, 2, 1, 0, -1, -1}, {2, 1.05}}},
        {QStringLiteral("rock"),    {{4, 3, 1, -1, -2, 0, 2, 3, 3, 2}, {3, 1.1}}},
        {QStringLiteral("bass"),    {{7, 6, 4, 2, 0, 0, 0, 0, 0, 0}, {6, 1.0}}},
        {QStringLiteral("vocal"),   {{-2, -1, 0, 1, 2, 3, 2, 1, 0, -1}, {-1, 0.95}}},
        {QStringLiteral("live"),    {{2, 2, 1, 0, 0, -1, -1, 0, 1, 2}, {2, 1.2}}},
        {QStringLiteral("spatial"), {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 1.6}}},
    };
    const auto it = presets.constFind(preset);
    if (it == presets.constEnd())
        return;
    for (int band = 0; band < it->first.size(); ++band)
        setEqGain(band, it->first.at(band).toDouble());
    setBassBoost(it->second.first);
    setStereoWidth(it->second.second);
}

QVariantList AudioEngine::audioOutputs() const
{
    QVariantList out;
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        out.append(QVariantMap{{"id", QString::fromUtf8(device.id())},
                               {"description", device.description()}});
    }
    return out;
}

void AudioEngine::setOutputDevice(const QString &id)
{
    if (id.isEmpty()) {
        if (m_outputDeviceId.isEmpty())
            return;
        m_outputDeviceId.clear();
        if (m_audio)
            m_audio->setDevice(QMediaDevices::defaultAudioOutput());
#ifdef YUNTING_EXPERIMENTAL_EQ
        if (m_eqSink)
            m_eqSink->setDevice(QAudioDevice());
#endif
        emit outputDeviceChanged();
        return;
    }
    for (const QAudioDevice &device : QMediaDevices::audioOutputs()) {
        if (QString::fromUtf8(device.id()) != id)
            continue;
        if (m_outputDeviceId == id)
            return;
        m_outputDeviceId = id;
        if (m_audio)
            m_audio->setDevice(device);
#ifdef YUNTING_EXPERIMENTAL_EQ
        if (m_eqSink)
            m_eqSink->setDevice(device);
#endif
        emit outputDeviceChanged();
        return;
    }
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
        qCDebug(lcPerf).nospace() << "audio url cache track=" << songId << " level=" << level;
        startSource(cached->url);
        return;
    }
    // 同一首歌、同一音质的取址已经在途（例如 A→B→A）：等那份回包，别再发一次，否则会重复 startSource
    if (m_urlRequests.contains(cacheKey))
        return;
    m_urlRequests.insert(cacheKey);

    const bool lossless = level == QLatin1String("lossless") || level == QLatin1String("hires");
    m_client->postWeapi("song/enhance/player/url/v1",
                        QJsonObject{{"ids", QStringLiteral("[%1]").arg(num(songId))},
                                    {"level", level},
                                    {"encodeType", lossless ? QStringLiteral("flac") : QStringLiteral("mp3")}},
                        true,
                        [this, songId, cacheKey](const QJsonDocument &doc, bool ok) {
        m_urlRequests.remove(cacheKey);
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
    qCDebug(lcPerf).nospace() << "audio url ready track="
                              << qlonglong(m_track.value("id").toDouble())
                              << " level=" << effectiveLevel() << " ms=" << m_trackClock.elapsed();
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
    m_scrobbleRoundMode = m_scrobbleMode;
    m_scrobbleAccountToken = m_client->cookieJar().value(QStringLiteral("MUSIC_U"));
    // NCBL 只在结算时上传 PLV/PLD；本地「最近播放」两种模式都要立即更新
    if (m_scrobbleRoundMode != QLatin1String("ncbl"))
        sendWeblog("startplay", 0);
    emit scrobbled(m_track);
}

void AudioEngine::scrobbleFinish()
{
    if (!m_scrobbleStarted || m_scrobbleFinished || m_track.isEmpty() || !m_client->hasMusicU())
        return;
    if (m_playClockRunning) {
        m_playedMs += m_playClock.elapsed();
        m_playClockRunning = false;
    }
    // 换号后旧轮次不能写到新账号
    if (m_client->cookieJar().value(QStringLiteral("MUSIC_U")) != m_scrobbleAccountToken) {
        m_scrobbleFinished = true;
        return;
    }
    int seconds = int(m_playedMs / 1000);
    if (seconds <= 0 && m_player->mediaStatus() == QMediaPlayer::EndOfMedia)
        seconds = qRound(duration());
    if (seconds <= 0)
        return;
    m_scrobbleFinished = true;
    if (m_scrobbleRoundMode == QLatin1String("ncbl"))
        scrobbleNcbl(seconds);
    else
        sendWeblog("play", seconds);
}

void AudioEngine::setScrobbleMode(const QString &mode)
{
    if (mode != QLatin1String("legacy") && mode != QLatin1String("ncbl"))
        return;
    if (m_scrobbleMode == mode)
        return;
    m_scrobbleMode = mode;
    emit scrobbleModeChanged();
}

void AudioEngine::scrobbleNcbl(int seconds)
{
#ifdef YUNTING_NCBL
    if (!m_client->hasMusicU() || m_track.isEmpty())
        return;
    const QString level = m_track.value("quality").toString();
    const QString levelName = (level.isEmpty() || level == QLatin1String("auto"))
        ? QStringLiteral("exhigh") : level;
    int bitrate = 320000;
    if (levelName == QLatin1String("hires") || levelName == QLatin1String("lossless"))
        bitrate = 999000;
    else if (levelName == QLatin1String("exhigh"))
        bitrate = 320000;
    else if (levelName == QLatin1String("higher"))
        bitrate = 192000;
    else if (levelName == QLatin1String("standard"))
        bitrate = 128000;
    const int rawFee = m_track.value("fee").toInt();
    const int fee = (rawFee == 1 || rawFee == 4 || rawFee == 8) ? rawFee : 0;

    Ncbl::Song song;
    song.id = m_track.value("id").toDouble();
    song.name = m_track.value("name").toString();
    song.artist = m_track.value("artist").toString();
    song.bitrate = bitrate;
    song.level = levelName;
    song.fee = fee;
    song.duration = qRound(duration());
    song.sourceId = num(song.id);
    song.sourceType = QStringLiteral("track");
    song.sourceName = QStringLiteral("track");

    const Ncbl::Context context = Ncbl::context(m_client->cookieJar());
    if (context.cookie.isEmpty() || context.meta.isEmpty())
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const auto record = [now](const char *action, const QJsonObject &json) {
        return Ncbl::records({{now, QByteArray(action) + '\x01'
                                       + QJsonDocument(json).toJson(QJsonDocument::Compact)}});
    };
    const Ncbl::Bytes plv = Ncbl::multipart(
        Ncbl::encrypt(context.meta, record("_plv", Ncbl::plv(song, context))));
    const int played = song.duration > 0 ? std::min(seconds, song.duration) : seconds;
    const Ncbl::Bytes pld = Ncbl::multipart(
        Ncbl::encrypt(context.meta, record("_pld", Ncbl::pld(song, context, played))));

    const auto check = [](const Ncbl::Bytes &bytes, const QJsonDocument &doc, bool ok) {
        if (!ok || doc.object().value("code").toInt() != 200)
            return false;
        const QJsonArray files = doc.object().value("data").toObject()
                                     .value("successfiles").toArray();
        for (const QJsonValue &v : files)
            if (v.toString() == bytes.fileName)
                return true;
        return false;
    };
    QPointer<AudioEngine> self(this);
    m_client->postClientLog(plv.body, plv.boundary, context.cookie,
                            [self, check, plv, pld, context](const QJsonDocument &doc, bool ok) {
        if (!self)
            return;
        if (!check(plv, doc, ok)) {
            qWarning() << "NCBL PLV upload failed:" << doc.object().value("code").toInt();
            return;
        }
        qCDebug(lcPerf) << "NCBL PLV accepted";
        self->m_client->postClientLog(pld.body, pld.boundary, context.cookie,
                                      [self, check, pld](const QJsonDocument &doc, bool ok) {
            if (!self)
                return;
            if (!check(pld, doc, ok))
                qWarning() << "NCBL PLD upload failed:" << doc.object().value("code").toInt();
            else
                qCDebug(lcPerf) << "NCBL PLD accepted";
        });
    });
#else
    Q_UNUSED(seconds);
    qWarning() << "NCBL support is not compiled in";
#endif
}

void AudioEngine::sendWeblog(const char *action, int seconds)
{
    // 曲目 map 没有歌单/专辑来源 id。参考实现允许省略来源并用歌曲 id 兜底
    // （ncm-api-enhanced/module/scrobble_v1.js：source.id = sourceId || songId），
    // 两个 weblog 字段都按这个值填，不能再固定成 0。
    const double sourceId = m_track.value("id").toDouble();
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

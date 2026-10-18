#include "audioengine.h"
#include "clientidentity.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "perf.h"
#include "ncbl.h"

#include <mpv/client.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace ncm;

namespace {
// 滑条 0–1 是感知线性的对数刻度；mpv volume 是 0–100 的线性幅度，所以先换算再喂给 mpv。
double linearVolume(double slider)
{
    slider = std::clamp(slider, 0.0, 1.0);
    if (slider > 0.99)
        return 1.0;
    return -std::log(1.0 - slider) / std::log(100.0);
}

// 10 段均衡的频点与旧 DSP 路径一致；lavfi 的 equalizer 是峰值滤波，Q 也取同一个值。
constexpr int kEqFrequencies[10] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

QString number(double value)
{
    return QString::number(value, 'f', 4);
}

// 一个带标签的 lavfi 滤镜链：10 段 EQ、120Hz 低架与中/侧宽度。
// 标签 eq 与滤镜实例名（equalizer@b0…）用于运行中的 af-command。
QString filterSpec(const QVariantList &gains, double bassBoost, double stereoWidth)
{
    QString graph;
    for (int band = 0; band < 10; ++band) {
        if (band > 0)
            graph += QLatin1Char(',');
        graph += QStringLiteral("equalizer@b%1=f=%2:width_type=q:width=1.2:gain=%3")
                     .arg(band)
                     .arg(kEqFrequencies[band])
                     .arg(number(gains.value(band).toDouble()));
    }
    // 120Hz、S=1 的低架等价于 Q=0.7071；extrastereo 的 m 就是中/侧宽度（0 单声道，1 原样，2 加宽）。
    graph += QStringLiteral(",bass@bass=f=120:width_type=q:width=0.70710678:gain=%1")
                 .arg(number(bassBoost));
    graph += QStringLiteral(",extrastereo@width=m=%1:c=0").arg(number(stereoWidth));
    return QStringLiteral("@eq:lavfi=[%1]").arg(graph);
}
} // namespace

AudioEngine::AudioEngine(NcmClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    // mpv 用 C locale 解析数字选项；必须在 QApplication 之后、mpv_create 之前改回来，否则拒绝初始化。
    std::setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "mpv_create failed";
        return;
    }

    const auto option = [this](const char *name, const char *value) {
        const int err = mpv_set_option_string(m_mpv, name, value);
        if (err < 0)
            qWarning() << "mpv option" << name << "failed:" << mpv_error_string(err);
    };
    option("vo", "null");            // 只出声，不建视频输出
    option("video", "no");
    option("audio-display", "no");
    option("ytdl", "no");
    option("terminal", "no");
    // 嵌入播放器不读用户的 mpv.conf / 脚本，否则外部配置会覆盖应用的音质与滤镜参数
    option("config", "no");
    option("load-scripts", "no");
    option("idle", "yes");
    option("gapless-audio", "weak");
    option("cache", "yes");          // 网络流
    option("audio-device", "auto");  // 跟随 PipeWire/PulseAudio 的默认设备
    const QByteArray userAgent = ncm::client::userAgent().toUtf8();
    option("user-agent", userAgent.constData());
    // 测试没有声卡：只认显式设置的变量，正常启动永远不会走到这里。
    if (qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL"))
        option("ao", "null");

    const int err = mpv_initialize(m_mpv);
    if (err < 0) {
        qWarning() << "mpv_initialize failed:" << mpv_error_string(err);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    // wakeup 回调来自 mpv 内部线程：只把一个 drainEvents 排到 Qt 线程，事件在拥有 mpv 的线程里取。
    mpv_set_wakeup_callback(m_mpv, [](void *ctx) {
        auto *self = static_cast<AudioEngine *>(ctx);
        if (self->m_destroying.load(std::memory_order_acquire))
            return;
        QMetaObject::invokeMethod(self, [self] { self->drainEvents(); }, Qt::QueuedConnection);
    }, this);
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "eof-reached", MPV_FORMAT_FLAG);

    double mpvVolume = linearVolume(m_volume) * 100.0;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &mpvVolume);
    int mpvMute = m_muted;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &mpvMute);
}

AudioEngine::~AudioEngine()
{
    m_destroying.store(true, std::memory_order_release);
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

double AudioEngine::position() const
{
    if (m_mpv) {
        double value = 0;
        if (mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &value) >= 0)
            return value;
    }
    // 停止或播完后 mpv 不再提供 time-pos：保留最后的位置
    return m_position;
}

double AudioEngine::duration() const
{
    if (m_mpv) {
        double value = 0;
        if (mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &value) >= 0 && value > 0)
            return value;
    }
    if (m_duration > 0)
        return m_duration;
    return m_track.value("dur").toDouble();
}

void AudioEngine::setTrack(const QVariantMap &track)
{
    if (track == m_track)
        return;
    // 切歌前把上一首的收听时长上报（位置 >= 1 秒才算）
    scrobbleFinish();
    if (m_mpv) {
        // 旧文件挂起的 end-file 事件不能落到新曲目上
        discardEvents();
        const char *args[] = {"stop", nullptr};
        mpv_command(m_mpv, args);
        discardEvents();
    }
    m_track = track;
    m_pendingSeek = -1;   // 上一首的跳转只属于上一首，不能落到新曲目上
    m_urlReady = false;
    m_fileLoaded = false;
    m_atEof = false;
    m_finishedEmitted = false;
    m_sourceUrl.clear();
    m_position = 0;
    m_duration = 0;
    m_failed = false;
    m_scrobbleStarted = false;
    m_scrobbleFinished = false;
    m_playedMs = 0;
    m_playClockRunning = false;
    m_firstAudioLogged = false;
    m_trackClock.start();
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
    if (m_mpv) {
        if (playing) {
            if (m_atEof && m_urlReady && !m_sourceUrl.isEmpty()) {
                // 播完后 mpv 已回到 idle，再次按播放要重新加载同一地址
                startSource(m_sourceUrl, m_playedLevel, m_playedBitrate);
            } else if (m_fileLoaded) {
                setPaused(false);
            }
            // 文件还在加载：FILE_LOADED 里再解除暂停
        } else {
            setPaused(true);
            // 暂停也结算一次；恢复后不会重复上报
            scrobbleFinish();
        }
    }
    emit playingChanged();
}

void AudioEngine::setPaused(bool paused)
{
    if (!m_mpv)
        return;
    int flag = paused;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
}

void AudioEngine::setVolume(double volume)
{
    const double clamped = std::clamp(volume, 0.0, 1.0);
    if (clamped == m_volume)
        return;
    m_volume = clamped;
    if (m_mpv) {
        double mpvVolume = linearVolume(clamped) * 100.0;
        mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &mpvVolume);
    }
    emit volumeChanged();
}

void AudioEngine::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    if (m_mpv) {
        int flag = muted;
        mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
    }
    emit mutedChanged();
}

QStringList AudioEngine::qualityLevels()
{
    return {
        QStringLiteral("auto"), QStringLiteral("standard"), QStringLiteral("higher"),
        QStringLiteral("exhigh"), QStringLiteral("lossless"), QStringLiteral("hires"),
    };
}

void AudioEngine::setQuality(const QString &quality)
{
    const QString normalized = qualityLevels().contains(quality) ? quality : QStringLiteral("auto");
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

void AudioEngine::applyFilters()
{
    if (!m_mpv)
        return;
    const QByteArray spec = filterSpec(m_eqGains, m_bassBoost, m_stereoWidth).toUtf8();
    const int err = mpv_set_property_string(m_mpv, "af", spec.constData());
    if (err < 0)
        qWarning() << "mpv af failed:" << mpv_error_string(err);
}

void AudioEngine::updateFilter(const char *target, const char *command, double value)
{
    if (!m_mpv)
        return;
    // 运行中改参数走 af-command：只更新一个滤波器的系数，音频链不重建、播放不断。
    // 整条替换会重置所有滤波器状态（增益跳变处可能出现咔哒声），只作为滤镜链还没建立时的后备。
    const QByteArray valueText = QByteArray::number(value, 'f', 4);
    const QByteArray targetText(target);
    const char *args[] = {"af-command", "eq", command, valueText.constData(), targetText.constData(), nullptr};
    if (mpv_command(m_mpv, args) >= 0)
        return;
    applyFilters();
}

void AudioEngine::setEqGain(int band, double db)
{
    if (band < 0 || band >= m_eqGains.size())
        return;
    const double clamped = std::clamp(db, -12.0, 12.0);
    if (qFuzzyCompare(m_eqGains.at(band).toDouble(), clamped))
        return;
    m_eqGains[band] = clamped;
    const QByteArray target = QStringLiteral("equalizer@b%1").arg(band).toUtf8();
    updateFilter(target.constData(), "gain", clamped);
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
    updateFilter("bass@bass", "gain", clamped);
    emit bassBoostChanged();
}

void AudioEngine::setStereoWidth(double width)
{
    const double clamped = std::clamp(width, 0.0, 2.0);
    if (qFuzzyCompare(m_stereoWidth, clamped))
        return;
    m_stereoWidth = clamped;
    updateFilter("extrastereo@width", "m", clamped);
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
    if (!m_mpv)
        return out;
    mpv_node node;
    if (mpv_get_property(m_mpv, "audio-device-list", MPV_FORMAT_NODE, &node) < 0)
        return out;
    if (node.format == MPV_FORMAT_NODE_ARRAY) {
        for (int i = 0; i < node.u.list->num; ++i) {
            const mpv_node &entry = node.u.list->values[i];
            if (entry.format != MPV_FORMAT_NODE_MAP)
                continue;
            QString id;
            QString description;
            for (int k = 0; k < entry.u.list->num; ++k) {
                const QByteArray key(entry.u.list->keys[k]);
                const mpv_node &value = entry.u.list->values[k];
                if (value.format != MPV_FORMAT_STRING)
                    continue;
                if (key == "name")
                    id = QString::fromUtf8(value.u.string);
                else if (key == "description")
                    description = QString::fromUtf8(value.u.string);
            }
            // mpv 自己的 auto 项不列出：菜单里的「系统默认」（空 id）就是它，列出来会有两个「默认」
            if (!id.isEmpty() && id != QLatin1String("auto"))
                out.append(QVariantMap{{"id", id}, {"description", description}});
        }
    }
    mpv_free_node_contents(&node);
    return out;
}

void AudioEngine::setOutputDevice(const QString &id)
{
    // 空 id 是「系统默认」（mpv 的 auto）；其余必须是列表里的设备，"auto" 本身不算
    const QString target = id.isEmpty() ? QStringLiteral("auto") : id;
    bool known = id.isEmpty();
    for (const QVariant &entry : audioOutputs()) {
        if (entry.toMap().value("id").toString() == target) {
            known = true;
            break;
        }
    }
    if (!known)
        return;
    if (m_outputDeviceId == id)
        return;
    m_outputDeviceId = id;
    if (m_mpv) {
        const QByteArray name = target.toUtf8();
        const int err = mpv_set_property_string(m_mpv, "audio-device", name.constData());
        // 设备不在了由 mpv 自己回退，这里只记一条警告，不重启播放
        if (err < 0)
            qWarning() << "mpv audio-device failed:" << mpv_error_string(err);
    }
    emit outputDeviceChanged();
}

void AudioEngine::toggle()
{
    setPlaying(!m_playing);
}

void AudioEngine::seek(double seconds)
{
    const double d = duration();
    const double target = std::max(0.0, std::min(d > 0 ? d : seconds, seconds));
    if (m_mpv && m_fileLoaded) {
        const QByteArray position = QByteArray::number(target, 'f', 3);
        const char *args[] = {"seek", position.constData(), "absolute", "exact", nullptr};
        if (mpv_command(m_mpv, args) >= 0) {
            m_position = target;
            emit seeked();
            emit positionChanged();
        }
        return;
    }
    m_pendingSeek = target; // 地址未落地或文件还在加载，FILE_LOADED 前应用
}

// 播放地址有时效（接口给 expi 秒数，通常 20 分钟）：时效内重播、切回同一首都直接用缓存。
// 用 v1 的 level 而不是旧的 br：旧接口最高只到 lossless，v1 才能请求 hires。
void AudioEngine::prefetch(double songId)
{
    if (songId <= 0 || m_track.value("id").toDouble() == songId)
        return;
    // resolveUrl 的回包只在 songId 仍是当前曲目时才 startSource；预取时只落缓存。
    resolveUrl(songId);
}

void AudioEngine::resolveUrl(double songId)
{
    const QString level = effectiveLevel();
    const QString cacheKey = QStringLiteral("%1:%2").arg(num(songId), level);
    const auto cached = m_urls.constFind(cacheKey);
    if (cached != m_urls.constEnd() && QDateTime::currentDateTimeUtc() < cached->expires) {
        qCDebug(lcPerf).nospace() << "audio url cache track=" << songId << " level=" << cached->level;
        startSource(cached->url, cached->level, cached->bitrate);
        return;
    }
    // 同一首歌、同一音质的取址已经在途（例如 A→B→A）：等那份回包，别再发一次，否则会重复 startSource
    if (m_urlRequests.contains(cacheKey))
        return;
    m_urlRequests.insert(cacheKey);

    const bool lossless = level == QLatin1String("lossless") || level == QLatin1String("hires");
    QPointer<AudioEngine> self(this);
    m_client->postWeapi("song/enhance/player/url/v1",
                        QJsonObject{{"ids", QStringLiteral("[%1]").arg(num(songId))},
                                    {"level", level},
                                    {"encodeType", lossless ? QStringLiteral("flac") : QStringLiteral("mp3")}},
                        true,
                        [self, songId, cacheKey, level](const QJsonDocument &doc, bool ok) {
        if (!self)
            return;
        self->m_urlRequests.remove(cacheKey);
        const QJsonObject resp = body(doc);
        const QJsonObject item = ok && resp.value("code").toInt() == 200
            ? arr(resp, "data").at(0).toObject() : QJsonObject();
        const QString url = item.value("url").toString();
        // 服务器可能降档给流（VIP/版权），上报必须用实际拿到的 level
        const NcmPlayedLevel played = ncmPlayedLevel(item, level);
        if (!url.isEmpty()) {
            // 留一分钟余量，避免拿到即将失效的地址
            const int expi = item.value("expi").toInt(1200);
            self->m_urls.insert(cacheKey, {url, played.level, played.bitrate,
                                           QDateTime::currentDateTimeUtc().addSecs(std::max(0, expi - 60))});
        }
        // 用户可能已经切到别的歌：迟到的地址只进缓存，不播
        if (self->m_track.value("id").toDouble() != songId)
            return;
        if (url.isEmpty()) {
            self->m_failed = true;
            emit self->failed(QStringLiteral("暂时无法播放这首歌"));
            return;
        }
        self->startSource(url, played.level, played.bitrate);
    });
}

void AudioEngine::startSource(const QString &url, const QString &level, int bitrate)
{
    m_playedLevel = level;
    m_playedBitrate = bitrate;
    qCDebug(lcPerf).nospace() << "audio url ready track="
                              << qlonglong(m_track.value("id").toDouble())
                              << " level=" << level << " ms=" << m_trackClock.elapsed();
    m_urlReady = true;
    m_fileLoaded = false;
    m_atEof = false;
    m_finishedEmitted = false;
    m_sourceUrl = url;
    m_position = 0;
    emit positionChanged();
    if (!m_mpv) {
        if (!m_failed) {
            m_failed = true;
            emit failed(QStringLiteral("暂时无法播放这首歌"));
        }
        return;
    }

    applyFilters();
    // 先暂停加载：挂起的 seek 要等 FILE_LOADED 后才能发
    setPaused(true);
    const QByteArray address = url.toUtf8();
    const char *args[] = {"loadfile", address.constData(), "replace", nullptr};
    const int err = mpv_command(m_mpv, args);
    if (err < 0) {
        if (!m_failed) {
            m_failed = true;
            emit failed(QStringLiteral("暂时无法播放这首歌"));
        }
        qWarning() << "mpv loadfile failed:" << mpv_error_string(err);
    }
}

void AudioEngine::notePlaybackStarted()
{
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
}

void AudioEngine::drainEvents()
{
    if (!m_mpv)
        return;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
}

void AudioEngine::discardEvents()
{
    if (!m_mpv)
        return;
    while (mpv_wait_event(m_mpv, 0)->event_id != MPV_EVENT_NONE) {
    }
}

void AudioEngine::handleEvent(const mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE:
        if (event->data)
            handleProperty(static_cast<const mpv_event_property *>(event->data));
        break;
    case MPV_EVENT_FILE_LOADED:
        onFileLoaded();
        break;
    case MPV_EVENT_END_FILE:
        if (event->data)
            onEndFile(static_cast<const mpv_event_end_file *>(event->data));
        break;
    default:
        break;
    }
}

void AudioEngine::handleProperty(const mpv_event_property *property)
{
    if (!property->data)
        return;
    const char *name = property->name;
    if (std::strcmp(name, "time-pos") == 0 && property->format == MPV_FORMAT_DOUBLE) {
        m_position = *static_cast<double *>(property->data);
        if (m_playing && m_position > 0)
            notePlaybackStarted();
        emit positionChanged();
    } else if (std::strcmp(name, "duration") == 0 && property->format == MPV_FORMAT_DOUBLE) {
        const double value = *static_cast<double *>(property->data);
        if (value > 0)
            m_duration = value;
        emit durationChanged();
    } else if (std::strcmp(name, "pause") == 0 && property->format == MPV_FORMAT_FLAG) {
        const bool paused = *static_cast<int *>(property->data);
        if (paused && m_playClockRunning) {
            m_playedMs += m_playClock.elapsed();
            m_playClockRunning = false;
        }
    } else if (std::strcmp(name, "idle-active") == 0 && property->format == MPV_FORMAT_FLAG) {
        // 结束/切歌由 end-file 与 FILE_LOADED 驱动；这里只记录，避免旧文件的事件落到新曲目
        qCDebug(lcPerf) << "mpv idle-active=" << *static_cast<int *>(property->data);
    } else if (std::strcmp(name, "eof-reached") == 0 && property->format == MPV_FORMAT_FLAG) {
        m_atEof = *static_cast<int *>(property->data);
    }
}

void AudioEngine::onFileLoaded()
{
    m_fileLoaded = true;
    if (m_mpv) {
        double value = 0;
        if (mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &value) >= 0 && value > 0)
            m_duration = value;
    }
    if (m_pendingSeek >= 0) {
        const double target = m_pendingSeek;
        m_pendingSeek = -1;
        const QByteArray position = QByteArray::number(target, 'f', 3);
        const char *args[] = {"seek", position.constData(), "absolute", "exact", nullptr};
        if (m_mpv && mpv_command(m_mpv, args) >= 0) {
            m_position = target;
            emit seeked();
            emit positionChanged();
        }
    }
    if (m_playing)
        setPaused(false);
    emit durationChanged();
}

void AudioEngine::onEndFile(const mpv_event_end_file *end)
{
    m_fileLoaded = false;
    switch (end->reason) {
    case MPV_END_FILE_REASON_EOF:
        m_atEof = true;
        if (m_finishedEmitted)
            break;
        m_finishedEmitted = true;
        scrobbleFinish();
        setPlaying(false);
        emit finished();
        break;
    case MPV_END_FILE_REASON_ERROR:
        if (m_failed)
            break;
        m_failed = true;
        emit failed(QStringLiteral("暂时无法播放这首歌"));
        qWarning() << "playback error:" << mpv_error_string(end->error);
        break;
    default:
        // STOP（切歌）与 REDIRECT 不是播放结束
        break;
    }
}

void AudioEngine::scrobbleStart()
{
    if (m_scrobbleStarted || m_track.isEmpty())
        return;
    if (!m_scrobblingAllowed) {
        // CLI 播放默认只读；直接写 stderr，smoke 重定向时 Qt 警告可能被隐藏
        std::fprintf(stderr, "listening reports disabled: pass --allow-write to enable\n");
        std::fflush(stderr);
        m_scrobbleStarted = true;
        return;
    }
    if (!m_client->hasMusicU())
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
    // 真实出声时间与是否上报无关：先结算，再决定要不要发请求
    if (m_playClockRunning) {
        m_playedMs += m_playClock.elapsed();
        m_playClockRunning = false;
    }
    if (!m_scrobblingAllowed || !m_scrobbleStarted || m_scrobbleFinished
        || m_track.isEmpty() || !m_client->hasMusicU())
        return;
    // 换号后旧轮次不能写到新账号
    if (m_client->cookieJar().value(QStringLiteral("MUSIC_U")) != m_scrobbleAccountToken) {
        m_scrobbleFinished = true;
        return;
    }
    int seconds = int(m_playedMs / 1000);
    if (seconds <= 0 && m_atEof)
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
    // 播放地址响应里实际拿到的档位；拿不到时退回本次请求的档位，不用曲目元数据里的最高档
    const NcmPlayedLevel reportedLevel = m_playedLevel.isEmpty()
        ? ncmPlayedLevel({}, effectiveLevel())
        : NcmPlayedLevel{m_playedLevel, m_playedBitrate};
    const int rawFee = m_track.value("fee").toInt();
    const int fee = (rawFee == 1 || rawFee == 4 || rawFee == 8) ? rawFee : 0;

    Ncbl::Song song;
    song.id = m_track.value("id").toDouble();
    song.name = m_track.value("name").toString();
    song.artist = m_track.value("artist").toString();
    song.bitrate = reportedLevel.bitrate;
    song.level = reportedLevel.level;
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

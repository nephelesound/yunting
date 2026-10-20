#include "audioengine.h"
#include "downloadpaths.h"
#include "clientidentity.h"
#include "json_util.h"
#include "ncmclient.h"
#include "normalize.h"
#include "perf.h"
#include "ncbl.h"

#include <MpvController>
#include <mpv/client.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

using namespace ncm;

namespace {
// 10 段均衡的频点与旧 DSP 路径一致；lavfi 的 equalizer 是峰值滤波，Q 也取同一个值。
constexpr int kEqFrequencies[10] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
// 淡出要快（按下去就该开始安静），淡入稍长（声音回来不突兀）
constexpr int kFadeOutMs = 250;
constexpr int kFadeInMs = 400;
// 跳转落地：time-pos 到了目标这么近就算到了；等这么久还没到（跳转失败、目标超出文件）就不再等
constexpr double kSeekNear = 0.5;
constexpr int kSeekLandMs = 1500;
// 睡醒后重建音频输出：PipeWire 刚醒时还在恢复设备，等一会儿再重建；有时第一次仍不出声，再补一次
constexpr int kWakeReloadMs = 1500;
constexpr int kWakeRetryMs = 6000;

QString number(double value)
{
    return QString::number(value, 'f', 4);
}

// 测试与打包构建（clean chroot）没有音频系统：只认显式设置的变量，正常启动永远不会走到这里。
// ao=null 还不够：读 audio-device-list 时 mpv 会逐个探测真实的音频后端（不看 ao），
// 连不上 PipeWire 时它的清理偶尔永久卡住，主线程随后的同步调用跟着卡死。
bool systemAudioOff()
{
    return qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL");
}

// MpvController 的 QVariant 返回值：失败时是 ErrorReturn，成功时是结果（可能为空）
bool isMpvError(const QVariant &reply)
{
    return reply.metaType() == QMetaType::fromType<ErrorReturn>();
}

// 读取一个数值属性；属性不可用（停止后没有 time-pos 等）返回 false
bool readDouble(MpvController *controller, const char *name, double *out)
{
    const QVariant value = controller->getProperty(QString::fromLatin1(name));
    if (isMpvError(value) || !value.isValid())
        return false;
    *out = value.toDouble();
    return true;
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
    : QObject(parent), m_client(client), m_controller(new MpvController(this))
{
    // MpvController 负责 locale、创建与初始化 mpv，以及把 mpv 事件排进 Qt 线程。
    // 它初始化后才暴露句柄，所以选项都按运行时属性设置（mpv 手册里标为 runtime 的那些）；
    // libmpv 默认不读用户的 mpv.conf 与脚本，不用另设。
    m_controller->init();

    const auto option = [this](const char *name, const QVariant &value) {
        const int err = m_controller->setProperty(QString::fromLatin1(name), value);
        if (err < 0)
            qWarning() << "mpv option" << name << "failed:" << MpvController::getError(err);
    };
    option("terminal", false);
    option("vo", "null");            // 只出声，不建视频输出
    option("vid", "no");
    option("audio-display", "no");
    option("ytdl", "no");
    option("idle", true);
    option("gapless-audio", "weak");
    option("cache", "yes");          // 网络流
    option("audio-device", "auto");  // 跟随 PipeWire/PulseAudio 的默认设备
    option("user-agent", ncm::client::userAgent());
    if (systemAudioOff())
        option("ao", "null");

    applyVolume();
    m_controller->setProperty(QStringLiteral("mute"), m_muted);

    m_fadeAnimation = new QVariantAnimation(this);
    connect(m_fadeAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_fade = value.toDouble();
        applyVolume();
    });
    connect(m_fadeAnimation, &QVariantAnimation::finished, this, [this] {
        if (const auto done = std::exchange(m_afterFade, {}))
            done();
    });

    connect(m_controller, &MpvController::propertyChanged, this, &AudioEngine::handleProperty);
    connect(m_controller, &MpvController::fileLoaded, this, &AudioEngine::onFileLoaded);
    connect(m_controller, &MpvController::endFile, this, &AudioEngine::onEndFile);

    m_controller->observeProperty(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
    m_controller->observeProperty(QStringLiteral("duration"), MPV_FORMAT_DOUBLE);
    m_controller->observeProperty(QStringLiteral("pause"), MPV_FORMAT_FLAG);
    m_controller->observeProperty(QStringLiteral("idle-active"), MPV_FORMAT_FLAG);
    m_controller->observeProperty(QStringLiteral("eof-reached"), MPV_FORMAT_FLAG);
    if (!systemAudioOff())
        m_controller->observeProperty(QStringLiteral("audio-device-list"), MPV_FORMAT_NODE);
    m_audioDeviceDebounce = new QTimer(this);
    m_audioDeviceDebounce->setSingleShot(true);
    m_audioDeviceDebounce->setInterval(100);
    connect(m_audioDeviceDebounce, &QTimer::timeout, this, &AudioEngine::reloadAudioOutput);
}

AudioEngine::~AudioEngine() = default;

double AudioEngine::position() const
{
    double value = 0;
    if (seekInFlight())
        return m_seekTarget;
    // 淡出中、以及 loadfile 之后新文件载入之前，time-pos 都还是上一首的；
    // 停止或播完后 mpv 不再提供 time-pos：这些时候都用保留的位置
    if (!m_switching && m_fileLoaded && readDouble(m_controller, "time-pos", &value))
        return value;
    // 载入中有挂起的跳转（换音质从原进度接上、载入前拖动）：那就是马上开始播放的位置
    if (!m_fileLoaded && m_pendingSeek >= 0)
        return m_pendingSeek;
    return m_position;
}

double AudioEngine::duration() const
{
    double value = 0;
    if (!m_switching && m_fileLoaded && readDouble(m_controller, "duration", &value) && value > 0)   // 同上
        return value;
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
    m_pendingSource.reset();
    if (!m_switching && m_playing && m_fileLoaded && m_fade > 0) {
        // 正在出声：上一首先淡出，淡完再换（finishSwitch）；连续切歌只换最后一首
        m_switching = true;
        fadeTo(0, kFadeOutMs, [this] { finishSwitch(); });
    } else if (!m_switching) {
        // 旧文件挂起的 end-file 事件不能落到新曲目上
        discardEvents();
        m_controller->command({QStringLiteral("stop")});
        discardEvents();
    }
    m_track = track;
    m_pendingSeek = -1;   // 上一首的跳转只属于上一首，不能落到新曲目上
    m_seekTarget = -1;
    m_urlReady = false;
    m_fileLoaded = false;
    m_atEof = false;
    m_finishedEmitted = false;
    m_sourceUrl.clear();
    m_position = 0;
    m_duration = 0;
    m_failed = false;
    m_reloadOnPlay = false;
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
    if (m_switching) {
        // 上一首正在淡出：新的一首加载完再按 m_playing 决定出不出声
    } else if (playing) {
        const bool failedWithNothingLoaded = m_failed && !m_fileLoaded;
        if (!m_track.isEmpty() && (failedWithNothingLoaded || m_reloadOnPlay)) {
            // 上次没放成（取址失败、网络断了、地址过期），或者刚睡醒（睡前的网络流已经断了）：
            // 重新载入，从原来的位置接上，不必切歌或重开应用。没放成时缓存的地址可能正是坏的那个，不再用。
            // 换音质失败但旧文件还在时不走这里：接着放旧文件
            const double songId = m_track.value("id").toDouble();
            if (failedWithNothingLoaded)
                m_urls.remove(QStringLiteral("%1:%2").arg(num(songId), effectiveLevel()));
            m_failed = false;
            m_reloadOnPlay = false;
            m_pendingSeek = position();
            resolveUrl(songId);
        } else if (m_atEof && m_urlReady && !m_sourceUrl.isEmpty()) {
            // 播完后 mpv 已回到 idle，再次按播放要重新加载；走取址而不是直接用上次的地址：
            // 停在结尾的这段时间（例如睡了一觉）里地址可能已经过期
            resolveUrl(m_track.value("id").toDouble());
        } else if (m_fileLoaded) {
            // 换音质没取到地址、旧文件还在：接着放旧文件，那次失败与它挂起的跳转都作废，
            // 否则之后重播会被当成失败重试、从那个旧位置开始。换音质还在取址时不动：跳转等新文件用
            if (std::exchange(m_failed, false))
                m_pendingSeek = -1;
            setPaused(false);
            fadeTo(1, kFadeInMs);
        }
        // 文件还在加载：FILE_LOADED 里再解除暂停
    } else {
        if (m_fileLoaded && !m_atEof)
            fadeTo(0, kFadeOutMs, [this] { setPaused(true); });
        else
            setPaused(true);   // 没在出声（还在加载、已播完）：没什么可淡的
        // 暂停也结算一次；恢复后不会重复上报
        scrobbleFinish();
    }
    emit playingChanged();
}

void AudioEngine::pauseForSleep()
{
    if (!m_playing)
        return;
    if (!m_switching) {
        m_fadeAnimation->stop();
        m_afterFade = {};
        m_fade = 0;
        applyVolume();
        setPaused(true);
    }
    m_playing = false;
    scrobbleFinish();
    emit playingChanged();
}

void AudioEngine::recoverAfterSleep()
{
    // 收不到 logind（Flatpak 没有那条权限）时睡前没暂停：醒来先停下，与收得到时一样，
    // 不让一条睡眠中断掉的流接着「放」
    pauseForSleep();
    // 停在一首歌中间睡的：网络流在睡眠中断了，地址也可能过期，下次按播放时重新载入
    if (m_fileLoaded && !m_atEof)
        m_reloadOnPlay = true;
    if (systemAudioOff())
        return;
    // 只重建输出，不读设备列表：刚醒时音频服务可能还没起来，读 audio-device-list 会去探测后端，
    // 连不上时可能永久卡住（见 systemAudioOff）。audio-device 本身不变，用户选的设备照旧
    const auto reload = [this] { m_controller->command({QStringLiteral("ao-reload")}); };
    QTimer::singleShot(kWakeReloadMs, this, reload);
    QTimer::singleShot(kWakeRetryMs, this, reload);
}

void AudioEngine::fadeTo(double target, int ms, std::function<void()> done)
{
    m_fadeAnimation->stop();
    m_afterFade = {};
    if (m_fade == target) {
        if (done)
            done();
        return;
    }
    m_afterFade = std::move(done);
    m_fadeAnimation->setStartValue(m_fade);
    m_fadeAnimation->setEndValue(target);
    m_fadeAnimation->setDuration(ms);
    m_fadeAnimation->start();
}

void AudioEngine::applyVolume()
{
    // mpv 的 volume 已是三次曲线；再叠一层对数换算会让 50% 的滑条几乎听不见
    m_controller->setProperty(QStringLiteral("volume"), m_volume * m_fade * 100.0);
}

void AudioEngine::finishSwitch()
{
    m_switching = false;
    // 旧文件挂起的事件（含淡出期间的 end-file）不能落到新曲目上
    discardEvents();
    m_controller->command({QStringLiteral("stop")});
    discardEvents();
    if (const auto source = std::exchange(m_pendingSource, std::nullopt))
        startSource(source->url, source->level, source->bitrate);
}

void AudioEngine::setPaused(bool paused)
{
    m_controller->setProperty(QStringLiteral("pause"), paused);
}

void AudioEngine::setVolume(double volume)
{
    const double clamped = std::clamp(volume, 0.0, 1.0);
    if (clamped == m_volume)
        return;
    m_volume = clamped;
    applyVolume();
    emit volumeChanged();
}

void AudioEngine::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    m_controller->setProperty(QStringLiteral("mute"), muted);
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
    // 正在播放时换档要立刻生效：重新取址并从当前进度接上，而不是等下一首。
    if (!m_track.isEmpty() && m_track.value("id").toDouble() > 0) {
        m_pendingSeek = position();
        resolveUrl(m_track.value("id").toDouble());
    }
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
    const int err = m_controller->setProperty(QStringLiteral("af"),
                                              filterSpec(m_eqGains, m_bassBoost, m_stereoWidth));
    if (err < 0)
        qWarning() << "mpv af failed:" << MpvController::getError(err);
}

void AudioEngine::updateFilter(const char *target, const char *command, double value)
{
    // 运行中改参数走 af-command：只更新一个滤波器的系数，音频链不重建、播放不断。
    // 整条替换会重置所有滤波器状态（增益跳变处可能出现咔哒声），只作为滤镜链还没建立时的后备。
    const QVariant reply = m_controller->command({QStringLiteral("af-command"), QStringLiteral("eq"),
                                                  QString::fromLatin1(command), number(value),
                                                  QString::fromLatin1(target)});
    if (!isMpvError(reply))
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
    // 用观察到的列表，不同步读 audio-device-list：读的时候 mpv 会去探测音频后端，音频服务没起来
    //（刚睡醒、测试环境）时可能卡住主线程（见 systemAudioOff）
    return audioOutputsFrom(m_deviceList);
}

QVariantList AudioEngine::audioOutputsFrom(const QVariant &deviceList)
{
    QVariantList out;
    for (const QVariant &item : deviceList.toList()) {
        const QVariantMap entry = item.toMap();
        const QString id = entry.value("name").toString();
        if (!id.isEmpty() && id != QLatin1String("auto"))
            out.append(QVariantMap{{"id", id}, {"description", entry.value("description").toString()}});
    }
    return out;
}

void AudioEngine::reloadAudioOutput()
{
    const QVariantList outputs = audioOutputs();
    bool selectedPresent = m_outputDeviceId.isEmpty();
    for (const QVariant &entry : outputs) {
        if (entry.toMap().value("id").toString() == m_outputDeviceId) {
            selectedPresent = true;
            break;
        }
    }
    // 列表是空的：设备还没回来（例如刚睡醒），不是用户选的那个被拔掉了，选择留着
    if (!selectedPresent && !outputs.isEmpty()) {
        m_outputDeviceId.clear();
        emit outputDeviceChanged();
    }
    const QString target = m_outputDeviceId.isEmpty() ? QStringLiteral("auto") : m_outputDeviceId;
    const int setError = m_controller->setProperty(QStringLiteral("audio-device"), target);
    const QVariant reload = m_controller->command({QStringLiteral("ao-reload")});
    if (setError < 0 || isMpvError(reload))
        qWarning() << "mpv audio device reload failed:"
                   << (setError < 0 ? MpvController::getError(setError) : MpvController::getError(
                           qvariant_cast<ErrorReturn>(reload).error));
    else
        qCDebug(lcPerf) << "mpv audio device reloaded" << target;
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
    const int err = m_controller->setProperty(QStringLiteral("audio-device"), target);
    // 设备不在了由 mpv 自己回退；设置后强制重开 AO，正在播放的流才会跟到新设备。
    if (err < 0)
        qWarning() << "mpv audio-device failed:" << MpvController::getError(err);
    else
        m_controller->command({QStringLiteral("ao-reload")});
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
    if (m_fileLoaded) {
        seekNow(target);
        return;
    }
    m_pendingSeek = target; // 地址未落地或文件还在加载，FILE_LOADED 前应用
    emit positionChanged();
}

void AudioEngine::seekNow(double target)
{
    const QVariant reply = m_controller->command({QStringLiteral("seek"), QString::number(target, 'f', 3),
                                                  QStringLiteral("absolute"), QStringLiteral("exact")});
    if (isMpvError(reply))
        return;
    // 跳转是异步的：落地前 time-pos 还在原处（刚载入时是 0），进度和歌词会先跳回去再跳过来。
    // 到了目标附近再认 time-pos
    m_seekTarget = target;
    m_seekClock.start();
    m_position = target;
    emit seeked();
    emit positionChanged();
}

bool AudioEngine::seekInFlight() const
{
    return m_seekTarget >= 0 && m_seekClock.isValid() && m_seekClock.elapsed() < kSeekLandMs;
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
    // Downloaded files win over the network: this is the offline path and it
    // also avoids re-resolving a signed URL after the file is present.
    if (const QString local = yuntingDownloadedFile(static_cast<qint64>(songId)); !local.isEmpty()) {
        startSource(QUrl::fromLocalFile(local).toString(), effectiveLevel(), 320000);
        return;
    }
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
        // 用户可能已经切到别的歌或换了音质：迟到的地址只进缓存，不播
        if (self->m_track.value("id").toDouble() != songId || level != self->effectiveLevel())
            return;
        if (url.isEmpty()) {
            self->m_failed = true;
            self->setPlaying(false);   // 取址失败不是播放中：界面必须回到暂停态
            emit self->failed(QStringLiteral("暂时无法播放这首歌"));
            return;
        }
        self->startSource(url, played.level, played.bitrate);
    });
}

void AudioEngine::startSource(const QString &url, const QString &level, int bitrate)
{
    if (m_switching) {
        m_pendingSource = PendingSource{url, level, bitrate};   // 上一首淡完再播
        return;
    }
    m_playedLevel = level;
    m_playedBitrate = bitrate;
    qCDebug(lcPerf).nospace() << "audio url ready track="
                              << qlonglong(m_track.value("id").toDouble())
                              << " level=" << level << " ms=" << m_trackClock.elapsed();
    // 同一首重新载入（换音质）：取址期间旧文件一直在播，用户也可能拖过。从此刻的位置接上，
    // 不用点下换音质时记的那个（否则会倒回取址花掉的时间，拖动也会丢）
    if (m_fileLoaded && m_pendingSeek >= 0)
        m_pendingSeek = position();
    m_urlReady = true;
    m_fileLoaded = false;
    m_atEof = false;
    m_finishedEmitted = false;
    m_sourceUrl = url;
    m_position = 0;
    m_seekTarget = -1;
    emit positionChanged();

    applyFilters();
    // 先暂停加载：挂起的 seek 要等 FILE_LOADED 后才能发
    setPaused(true);
    const QVariant reply = m_controller->command({QStringLiteral("loadfile"), url,
                                                  QStringLiteral("replace")});
    if (isMpvError(reply)) {
        if (!m_failed) {
            m_failed = true;
            setPlaying(false);
            emit failed(QStringLiteral("暂时无法播放这首歌"));
        }
        qWarning() << "mpv loadfile failed:"
                   << MpvController::getError(qvariant_cast<ErrorReturn>(reply).error);
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

void AudioEngine::discardEvents()
{
    // MpvController 与 AudioEngine 在同一线程消费事件队列：切歌前把旧文件挂起的事件丢掉，
    // 否则旧文件的错误会落到新曲目上。命令发出后 mpv 还会再产生 STOP，事件在事件循环里按序处理。
    while (mpv_wait_event(m_controller->mpv(), 0)->event_id != MPV_EVENT_NONE) {
    }
}

void AudioEngine::handleProperty(const QString &name, const QVariant &value)
{
    if (!value.isValid())
        return;
    if (name == QLatin1String("audio-device-list")) {
        m_deviceList = value;
        m_audioDeviceDebounce->start();
        return;
    }
    if (m_switching)
        return;   // 还在淡出的上一首：它的进度、时长、播完都不属于已经换上的新曲目
    if (!m_fileLoaded && (name == QLatin1String("time-pos") || name == QLatin1String("duration")))
        return;   // loadfile 之后、新文件载入之前，这两项还是上一首的
    if (name == QLatin1String("time-pos")) {
        if (seekInFlight() && std::abs(value.toDouble() - m_seekTarget) > kSeekNear)
            return;   // 跳转还没落地：这是跳之前的位置
        m_seekTarget = -1;
        m_position = value.toDouble();
        if (m_playing && m_position > 0)
            notePlaybackStarted();
        emit positionChanged();
    } else if (name == QLatin1String("duration")) {
        const double duration = value.toDouble();
        if (duration > 0)
            m_duration = duration;
        emit durationChanged();
    } else if (name == QLatin1String("pause")) {
        const bool paused = value.toBool();
        if (paused && m_playClockRunning) {
            m_playedMs += m_playClock.elapsed();
            m_playClockRunning = false;
        }
    } else if (name == QLatin1String("idle-active")) {
        // 结束/切歌由 endFile 与 fileLoaded 驱动；这里只记录
        qCDebug(lcPerf) << "mpv idle-active=" << value.toBool();
    } else if (name == QLatin1String("eof-reached")) {
        m_atEof = value.toBool();
    }
}

void AudioEngine::onFileLoaded()
{
    m_fileLoaded = true;
    double value = 0;
    if (readDouble(m_controller, "duration", &value) && value > 0)
        m_duration = value;
    if (m_pendingSeek >= 0)
        seekNow(std::exchange(m_pendingSeek, -1));
    if (m_playing) {
        setPaused(false);
        fadeTo(1, kFadeInMs);   // 切歌、暂停后淡出过才会淡入；自然接下一首时音量本来就在
    }
    emit durationChanged();
}

void AudioEngine::onEndFile(const QString &reason)
{
    if (m_switching)
        return;   // 淡出中的上一首播完了：新曲目还没开始，不算它播完
    m_fileLoaded = false;
    m_seekTarget = -1;
    if (reason == QLatin1String("eof")) {
        m_atEof = true;
        if (m_finishedEmitted)
            return;
        m_finishedEmitted = true;
        scrobbleFinish();
        setPlaying(false);
        emit finished();
    } else if (reason == QLatin1String("error")) {
        if (m_failed)
            return;
        m_failed = true;
        setPlaying(false);
        emit failed(QStringLiteral("暂时无法播放这首歌"));
        qWarning() << "playback error";
    }
    // STOP（切歌）不是播放结束
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
    m_client->postForm(QStringLiteral("api/feedback/weblog"), form, true,
                        NcmClient::GetCallback{});
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

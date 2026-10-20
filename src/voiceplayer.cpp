#include "voiceplayer.h"

#include "clientidentity.h"

#include <MpvController>

VoicePlayer::VoicePlayer(QObject *parent)
    : QObject(parent), m_controller(new MpvController(this))
{
    m_controller->init();
    // 与 AudioEngine 相同的只出声设置（见那里的说明）；测试里同样不出声
    m_controller->setProperty(QStringLiteral("terminal"), false);
    m_controller->setProperty(QStringLiteral("vo"), QStringLiteral("null"));
    m_controller->setProperty(QStringLiteral("vid"), QStringLiteral("no"));
    m_controller->setProperty(QStringLiteral("audio-display"), QStringLiteral("no"));
    m_controller->setProperty(QStringLiteral("ytdl"), QStringLiteral("no"));
    // 与 AudioEngine 相同：init 后 load-scripts 停不掉已加载的脚本，逐个 load-* 关
    m_controller->setProperty(QStringLiteral("load-scripts"), false);
    m_controller->setProperty(QStringLiteral("osc"), false);
    m_controller->setProperty(QStringLiteral("load-stats-overlay"), false);
    m_controller->setProperty(QStringLiteral("load-console"), false);
    m_controller->setProperty(QStringLiteral("load-auto-profiles"), false);
    m_controller->setProperty(QStringLiteral("load-select"), false);
    m_controller->setProperty(QStringLiteral("load-positioning"), false);
    m_controller->setProperty(QStringLiteral("load-commands"), false);
    m_controller->setProperty(QStringLiteral("load-context-menu"), false);
    // 语音评论只有几十秒：4 MiB 预读绰绰有余（默认 150 MiB）
    m_controller->setProperty(QStringLiteral("cache"), QStringLiteral("yes"));
    m_controller->setProperty(QStringLiteral("demuxer-max-bytes"), 4 * 1024 * 1024);
    m_controller->setProperty(QStringLiteral("demuxer-max-back-bytes"), 1024 * 1024);
    m_controller->setProperty(QStringLiteral("idle"), true);
    m_controller->setProperty(QStringLiteral("user-agent"), ncm::client::userAgent());
    // mpv 默认不限网络等待：卡住的下载既不载入也不报错，这条会一直「在放」、歌一直停着。
    // 超时后按加载失败处理（end-file error），回到空闲
    m_controller->setProperty(QStringLiteral("network-timeout"), 10);
    if (qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL"))
        m_controller->setProperty(QStringLiteral("ao"), QStringLiteral("null"));

    connect(m_controller, &MpvController::propertyChanged, this, &VoicePlayer::handleProperty);
    // 「没有在放」以 idle-active 为准：换一条时旧的那条会报 end-file（reason stop），那时新的一条
    // 已经在加载，mpv 并不空闲。但 loadfile 之后、新文件载入之前到达的 idle 是旧状态（包括 observe
    // 时补发的初始值），不能清掉刚开始的这条；这段时间里加载失败由 end-file 的 error 报告
    connect(m_controller, &MpvController::fileLoaded, this, [this] { m_starting = false; });
    connect(m_controller, &MpvController::endFile, this, [this](const QString &reason) {
        if (m_starting && reason == QLatin1String("error")) {
            m_starting = false;
            setSource({});
        }
    });
    m_controller->observeProperty(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
    m_controller->observeProperty(QStringLiteral("idle-active"), MPV_FORMAT_FLAG);
}

VoicePlayer::~VoicePlayer() = default;

void VoicePlayer::setVolume(double volume)
{
    volume = qBound(0.0, volume, 1.0);
    if (qFuzzyCompare(volume, m_volume))
        return;
    m_volume = volume;
    m_controller->setProperty(QStringLiteral("volume"), m_volume * 100);
    emit volumeChanged();
}

void VoicePlayer::toggle(const QString &url)
{
    if (url.isEmpty() || url == m_source) {
        stop();
        return;
    }
    m_position = 0;
    emit positionChanged();
    // 上一条挂起的事件（加载失败的 end-file、最后几个 time-pos）丢掉，不能落到这一条上
    discardEvents();
    m_starting = true;
    setSource(url);
    m_controller->command({QStringLiteral("loadfile"), url, QStringLiteral("replace")});
}

void VoicePlayer::stop()
{
    m_starting = false;
    m_controller->command({QStringLiteral("stop")});
    setSource({});
}

void VoicePlayer::recoverAfterSleep()
{
    stop();
    if (!qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL"))
        m_controller->command({QStringLiteral("ao-reload")});
}

void VoicePlayer::discardEvents()
{
    // 与 AudioEngine::discardEvents 同理：MpvController 在同一线程消费这个队列
    while (mpv_wait_event(m_controller->mpv(), 0)->event_id != MPV_EVENT_NONE) {
    }
}

void VoicePlayer::handleProperty(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("time-pos")) {
        if (!value.isValid() || m_source.isEmpty() || m_starting)
            return;   // 新的一条载入之前，time-pos 还是上一条的
        m_position = value.toDouble();
        emit positionChanged();
    } else if (name == QLatin1String("idle-active") && value.toBool() && !m_starting) {
        setSource({});
    }
}

void VoicePlayer::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    if (m_source.isEmpty()) {
        m_position = 0;
        emit positionChanged();
    }
    emit sourceChanged();
}

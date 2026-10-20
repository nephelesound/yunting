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
    m_controller->setProperty(QStringLiteral("idle"), true);
    m_controller->setProperty(QStringLiteral("user-agent"), ncm::client::userAgent());
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

void VoicePlayer::handleProperty(const QString &name, const QVariant &value)
{
    if (name == QLatin1String("time-pos")) {
        if (!value.isValid() || m_source.isEmpty())
            return;
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

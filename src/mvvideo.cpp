#include "mvvideo.h"
#include "clientidentity.h"

#include <MpvController>

#include <QDebug>
#include <algorithm>

namespace {
// MpvController 的 QVariant 返回值：失败时是 ErrorReturn，成功时是结果（可能为空）
bool isMpvError(const QVariant &reply)
{
    return reply.metaType() == QMetaType::fromType<ErrorReturn>();
}
} // namespace

MvVideo::MvVideo(QQuickItem *parent)
    : MpvAbstractItem(parent)
{
    // MpvAbstractItem 已在工作线程初始化 mpv，所以这些选项按运行时属性写入。
    // libmpv 默认不读用户的 mpv.conf 与脚本，不用另设
    const auto option = [this](const char *name, const QVariant &value) {
        const int err = setPropertyBlocking(QString::fromLatin1(name), value);
        if (err < 0)
            qWarning() << "MvVideo option" << name << "failed:" << MpvController::getError(err);
    };
    option("terminal", false);
    option("audio-display", "no");
    option("ytdl", "no");
    option("cache", "yes");
    option("gapless-audio", "weak");
    option("audio-device", "auto");
    option("user-agent", ncm::client::userAgent());
    // 测试没有声卡：只认显式设置的变量
    if (qEnvironmentVariableIsSet("YUNTING_TEST_AO_NULL"))
        option("ao", "null");
    option("volume", 80);

    observeProperty(QStringLiteral("time-pos"), MPV_FORMAT_DOUBLE);
    observeProperty(QStringLiteral("duration"), MPV_FORMAT_DOUBLE);
    observeProperty(QStringLiteral("pause"), MPV_FORMAT_FLAG);
    observeProperty(QStringLiteral("eof-reached"), MPV_FORMAT_FLAG);

    // controller 在工作线程；信号排队回到 QML 线程
    MpvController *controller = mpvController();
    connect(controller, &MpvController::propertyChanged, this, &MvVideo::onPropertyChanged,
            Qt::QueuedConnection);
    connect(controller, &MpvController::fileLoaded, this, &MvVideo::onFileLoaded,
            Qt::QueuedConnection);
    connect(controller, &MpvController::endFile, this, &MvVideo::onEndFile,
            Qt::QueuedConnection);
    connect(this, &MpvAbstractItem::ready, this, &MvVideo::onRendererReady);
}

void MvVideo::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    m_position = 0;
    m_duration = 0;
    m_pendingSeek = -1;
    m_fileLoaded = false;
    m_stopped = true;
    m_atEof = false;
    m_loadPending = false;
    setPlaying(false);
    setError(QString());
    emit sourceChanged();
    emit positionChanged();
    emit durationChanged();
    command(QStringList{QStringLiteral("stop")});
    if (!m_source.isEmpty()) {
        m_loadPending = true;
        if (m_rendererReady)
            load();
    }
}

void MvVideo::setPosition(double milliseconds)
{
    const double target = m_duration > 0
        ? std::clamp(milliseconds, 0.0, m_duration)
        : std::max(0.0, milliseconds);
    if (!m_fileLoaded) {
        m_pendingSeek = target;   // 地址还没加载完：记下来，onFileLoaded 再跳
        m_position = target;
        emit positionChanged();
        return;
    }
    m_position = target;
    emit positionChanged();
    command(QStringList{QStringLiteral("seek"), QString::number(target / 1000.0, 'f', 3),
                        QStringLiteral("absolute"), QStringLiteral("exact")});
}

void MvVideo::play()
{
    if (m_source.isEmpty())
        return;
    if (m_atEof || m_stopped) {
        // 播完或停止后再按播放：从同一地址重新加载（与 QML 播放器的 play() 同义）
        m_loadPending = true;
        if (m_rendererReady)
            load();
        return;
    }
    setPaused(false);
    setPlaying(true);
}

void MvVideo::pause()
{
    if (m_source.isEmpty())
        return;
    setPaused(true);
    setPlaying(false);
}

void MvVideo::stop()
{
    command(QStringList{QStringLiteral("stop")});
    m_fileLoaded = false;
    m_stopped = true;
    m_atEof = false;
    m_loadPending = false;
    m_pendingSeek = -1;
    setPlaying(false);
    if (m_position != 0) {
        m_position = 0;
        emit positionChanged();
    }
}

void MvVideo::load()
{
    // 渲染上下文还没建好：先记住，ready() 到了再加载（见头文件里的原因）
    if (!m_rendererReady) {
        m_loadPending = true;
        return;
    }
    m_loadPending = false;
    m_fileLoaded = false;
    m_stopped = false;
    m_atEof = false;
    setError(QString());
    setProperty(QStringLiteral("pause"), false);
    const QVariant reply = commandBlocking(QStringList{QStringLiteral("loadfile"),
                                                       m_source.toString(),
                                                       QStringLiteral("replace")});
    if (isMpvError(reply)) {
        setError(QStringLiteral("这个 MV 无法播放"));
        m_stopped = true;
        setPlaying(false);
        return;
    }
    setPlaying(true);
}

void MvVideo::onRendererReady()
{
    m_rendererReady = true;
    if (m_loadPending)
        load();
}

void MvVideo::setPaused(bool paused)
{
    setProperty(QStringLiteral("pause"), paused);
}

void MvVideo::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void MvVideo::setError(const QString &message)
{
    if (message == m_errorString)
        return;
    m_errorString = message;
    emit errorStringChanged();
}

void MvVideo::onPropertyChanged(const QString &name, const QVariant &value)
{
    if (!value.isValid())
        return;
    if (name == QLatin1String("time-pos")) {
        m_position = value.toDouble() * 1000.0;
        emit positionChanged();
    } else if (name == QLatin1String("duration")) {
        const double duration = value.toDouble() * 1000.0;
        if (duration > 0) {
            m_duration = duration;
            emit durationChanged();
        }
    } else if (name == QLatin1String("pause")) {
        if (m_fileLoaded && !m_stopped)
            setPlaying(!value.toBool());
    } else if (name == QLatin1String("eof-reached")) {
        m_atEof = value.toBool();
    }
}

void MvVideo::onFileLoaded()
{
    m_fileLoaded = true;
    m_stopped = false;
    const QVariant duration = getProperty(QStringLiteral("duration"));
    if (!isMpvError(duration) && duration.isValid() && duration.toDouble() > 0) {
        m_duration = duration.toDouble() * 1000.0;
        emit durationChanged();
    }
    if (m_pendingSeek >= 0) {
        const double target = m_pendingSeek;
        m_pendingSeek = -1;
        m_position = target;
        emit positionChanged();
        command(QStringList{QStringLiteral("seek"), QString::number(target / 1000.0, 'f', 3),
                            QStringLiteral("absolute"), QStringLiteral("exact")});
    }
    if (m_playing)
        setPaused(false);
}

void MvVideo::onEndFile(const QString &reason)
{
    m_fileLoaded = false;
    if (reason == QLatin1String("eof")) {
        m_atEof = true;
        m_stopped = true;
        setPlaying(false);
        if (m_duration > 0 && m_position != m_duration) {
            m_position = m_duration;
            emit positionChanged();
        }
    } else if (reason == QLatin1String("error")) {
        m_stopped = true;
        setPlaying(false);
        setError(QStringLiteral("这个 MV 无法播放"));
    }
    // STOP：切歌时旧文件的结束，不改变当前状态
}

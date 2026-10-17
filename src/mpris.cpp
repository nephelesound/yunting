#include "mpris.h"
#include "audioengine.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QRegularExpression>

// ── org.mpris.MediaPlayer2 ──
class MprisBaseAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
public:
    explicit MprisBaseAdaptor(Mpris *mpris) : QDBusAbstractAdaptor(mpris), m_mpris(mpris) {}

    Q_PROPERTY(bool CanQuit READ CanQuit CONSTANT)
    Q_PROPERTY(bool CanSetFullscreen READ CanSetFullscreen CONSTANT)
    Q_PROPERTY(bool CanRaise READ CanRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ HasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ Identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ DesktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ SupportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ SupportedMimeTypes CONSTANT)

    bool CanQuit() const { return false; }
    bool CanSetFullscreen() const { return false; }
    // Plasma 媒体弹窗里点曲目会调 Raise：窗口在 QML 手里，这里只发信号
    bool CanRaise() const { return true; }
    bool HasTrackList() const { return false; }
    QString Identity() const { return QStringLiteral("云听"); }
    QString DesktopEntry() const { return QStringLiteral("ncm-player"); }
    QStringList SupportedUriSchemes() const { return {}; }
    QStringList SupportedMimeTypes() const { return {}; }

public slots:
    void Quit() {}
    void Raise() { emit m_mpris->raiseRequested(); }

private:
    Mpris *m_mpris;
};

// ── org.mpris.MediaPlayer2.Player ──
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
public:
    explicit MprisPlayerAdaptor(Mpris *mpris, AudioEngine *audio)
        : QDBusAbstractAdaptor(mpris), m_mpris(mpris), m_audio(audio)
    {
    }

    // PlaybackStatus：Playing / Paused / Stopped
    Q_PROPERTY(QString PlaybackStatus READ PlaybackStatus NOTIFY dummy)
    Q_PROPERTY(QString LoopStatus READ LoopStatus)
    Q_PROPERTY(double Rate READ Rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ Shuffle)
    Q_PROPERTY(QVariantMap Metadata READ Metadata CONSTANT)
    Q_PROPERTY(double Volume READ Volume WRITE setVolume)
    Q_PROPERTY(qint64 Position READ Position)
    Q_PROPERTY(double MinimumRate READ MinimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ MaximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ CanGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ CanGoPrevious CONSTANT)
    Q_PROPERTY(bool CanPlay READ CanPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ CanPause CONSTANT)
    Q_PROPERTY(bool CanSeek READ CanSeek CONSTANT)
    Q_PROPERTY(bool CanControl READ CanControl CONSTANT)

    QString PlaybackStatus() const
    {
        if (!m_audio->track().isEmpty())
            return m_audio->playing() ? QStringLiteral("Playing") : QStringLiteral("Paused");
        return QStringLiteral("Stopped");
    }
    QString LoopStatus() const { return QStringLiteral("None"); }
    double Rate() const { return 1.0; }
    void setRate(double) {}
    bool Shuffle() const { return false; }
    // MPRIS 的 Volume 与 AudioEngine.volume 同刻度（对数滑条 0–1），透传即可
    double Volume() const { return m_audio->volume(); }
    void setVolume(double volume) { m_audio->setVolume(volume); }
    double MinimumRate() const { return 1.0; }
    double MaximumRate() const { return 1.0; }
    bool CanGoNext() const { return true; }
    bool CanGoPrevious() const { return true; }
    bool CanPlay() const { return true; }
    bool CanPause() const { return true; }
    bool CanSeek() const { return true; }
    bool CanControl() const { return true; }

    // MPRIS 元数据：id 用 ncmtrack/<id>；长度是微秒
    QVariantMap Metadata() const
    {
        const QVariantMap t = m_audio->track();
        if (t.isEmpty())
            return {};
        return {{QStringLiteral("mpris:trackid"),
                 QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/ncmplayer/track/%1")
                                                        .arg(t.value("id").toLongLong())))},
                {QStringLiteral("mpris:length"), qlonglong(t.value("dur").toDouble()) * 1000000ll},
                // 封面：Plasma 媒体弹窗、锁屏都用它；换成 512 的尺寸参数
                {QStringLiteral("mpris:artUrl"), t.value("cover").toString().replace(
                     QRegularExpression(QStringLiteral("\\?param=\\d+y\\d+$")), QStringLiteral("?param=512y512"))},
                {QStringLiteral("xesam:title"), t.value("name").toString()},
                // 多位歌手在曲目里是「A / B」，MPRIS 要列表
                {QStringLiteral("xesam:artist"), t.value("artist").toString().split(QStringLiteral(" / "))},
                {QStringLiteral("xesam:album"), t.value("album").toString()}};
    }
    qint64 Position() const { return qlonglong(m_audio->position() * 1000000.0); }

signals:
    void Seeked(qint64 Position);
    void dummy();

public slots:
    void Next() { emit m_mpris->nextRequested(); }
    void Previous() { emit m_mpris->previousRequested(); }
    void Pause() { m_audio->setPlaying(false); }
    void Play() { m_audio->setPlaying(true); }
    void PlayPause() { m_audio->toggle(); }
    void Stop() { m_audio->setPlaying(false); }
    // 微秒
    void Seek(qint64 offset) { m_audio->seek(m_audio->position() + offset / 1000000.0); }
    void SetPosition(const QDBusObjectPath &, qint64 pos) { m_audio->seek(pos / 1000000.0); }

private:
    Mpris *m_mpris;
    AudioEngine *m_audio;
};

Mpris::Mpris(AudioEngine *audio, QObject *parent)
    : QObject(parent)
{
    m_base = new MprisBaseAdaptor(this);
    m_player = new MprisPlayerAdaptor(this, audio);

    QDBusConnection bus = QDBusConnection::sessionBus();
    m_registered = bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.ncmplayer"));
    if (!m_registered) {
        // 已有实例在跑（或环境没有会话总线）：不注册，媒体键留给先到的实例
        qWarning() << "mpris service registration failed:" << bus.lastError().message();
        return;
    }
    bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this);

    // 换歌 / 播放状态变化时广播 PropertiesChanged，Plasma 媒体控件与锁屏才不会停留在旧曲目
    auto emitChanged = [this](const QStringList &props) {
        QDBusMessage msg = QDBusMessage::createSignal(
            QStringLiteral("/org/mpris/MediaPlayer2"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"));
        QVariantMap changed;
        for (const QString &p : props)
            changed.insert(p, m_player->property(p.toUtf8().constData()));
        msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << changed << QStringList();
        QDBusConnection::sessionBus().send(msg);
    };
    connect(audio, &AudioEngine::trackChanged, this,
            [emitChanged] { emitChanged({QStringLiteral("Metadata")}); });
    connect(audio, &AudioEngine::playingChanged, this, [emitChanged] {
        emitChanged({QStringLiteral("PlaybackStatus"), QStringLiteral("CanSeek")});
    });
    connect(audio, &AudioEngine::volumeChanged, this,
            [emitChanged] { emitChanged({QStringLiteral("Volume")}); });
    // 跳转后发 Seeked，Plasma 弹窗的进度条才会跟上（Position 本身不广播）
    connect(audio, &AudioEngine::seeked, this,
            [this] { emit m_player->Seeked(m_player->Position()); });
}

Mpris::~Mpris()
{
    if (m_registered) {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
        QDBusConnection::sessionBus().unregisterService(QStringLiteral("org.mpris.MediaPlayer2.ncmplayer"));
    }
}

#include "mpris.moc"

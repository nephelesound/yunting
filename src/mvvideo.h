#pragma once
// MV 画面：MpvQt 的 MpvAbstractItem 把 mpv 的 vid 直接渲染进 QML。
// 对外接口只覆盖 MvPage 用到的播放器能力，单位与 QML 播放器接口相同：
// position / duration 是毫秒，playing 是「正在出画面」。
#include <MpvAbstractItem>
#include <QUrl>

class MvVideo : public MpvAbstractItem {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
public:
    explicit MvVideo(QQuickItem *parent = nullptr);

    QUrl source() const { return m_source; }
    bool playing() const { return m_playing; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QString errorString() const { return m_errorString; }

    // 换地址即开始播放
    void setSource(const QUrl &source);
    void setPosition(double milliseconds);

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void errorStringChanged();

private:
    void load();
    void onRendererReady();
    void setPaused(bool paused);
    void setPlaying(bool playing);
    void setError(const QString &message);
    void onPropertyChanged(const QString &name, const QVariant &value);
    void onFileLoaded();
    void onEndFile(const QString &reason);

    QUrl m_source;
    bool m_playing = false;
    double m_position = 0;
    double m_duration = 0;
    QString m_errorString;
    bool m_fileLoaded = false;
    bool m_stopped = true;
    bool m_atEof = false;
    // MpvAbstractItem 建好渲染上下文前 loadfile 会让 vo=libmpv 报
    // 「No render context set」并关掉视频，所以等 ready() 再加载
    bool m_rendererReady = false;
    bool m_loadPending = false;
    double m_pendingSeek = -1;   // 毫秒；文件加载完再应用
};

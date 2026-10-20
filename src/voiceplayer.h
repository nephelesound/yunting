#pragma once
// VoicePlayer：评论里的语音（几十秒的 m4a）。用自己的一个 mpv，与放歌的 AudioEngine 互不干扰；
// 歌要不要先停下由界面决定（prototype/main.qml）。同一时间只放一条：放新的一条会替换正在放的，
// 再点正在放的那条就停下。原型里的替身是 prototype/SimVoice.qml，接口逐字段一致。
#include <QObject>
#include <QString>

class MpvController;

class VoicePlayer : public QObject {
    Q_OBJECT
    // 正在放的那条的地址；空 = 没有在放（放完、停下或加载失败都回到空）
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    // 已放到的秒数
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    // 0–1，与放歌的音量同一刻度（界面把歌的音量绑过来）
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
public:
    explicit VoicePlayer(QObject *parent = nullptr);
    ~VoicePlayer() override;

    QString source() const { return m_source; }
    double position() const { return m_position; }
    double volume() const { return m_volume; }
    void setVolume(double volume);

    Q_INVOKABLE void toggle(const QString &url);
    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void positionChanged();
    void volumeChanged();

private:
    void handleProperty(const QString &name, const QVariant &value);
    void setSource(const QString &source);

    MpvController *m_controller = nullptr;
    QString m_source;
    bool m_starting = false;   // loadfile 已发出、新文件还没载入
    double m_position = 0;
    double m_volume = 1;
};

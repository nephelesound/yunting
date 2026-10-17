#pragma once
// 实验性 EQ 输出：QMediaPlayer → QAudioBufferOutput → EqProcessor → QAudioSink。
// YUNTING_EXPERIMENTAL_EQ 默认开启；关闭时播放仍走 QAudioOutput。
#include "eqprocessor.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QObject>
#include <QString>
#include <QVariantList>

class QAudioBuffer;
class QAudioSink;
class QIODevice;

class EqAudioSink : public QObject {
    Q_OBJECT
public:
    explicit EqAudioSink(QObject *parent = nullptr);
    ~EqAudioSink() override;

    void setVolume(double slider);
    void setMuted(bool muted);
    void setGain(int band, double db);
    void setBassBoost(double db) { m_eq.setBassBoost(db); }
    void setStereoWidth(double width) { m_eq.setStereoWidth(width); }
    void setDevice(const QAudioDevice &device);

signals:
    // 输出设备不可用 / QAudioSink 起不来：AudioEngine 据此退回 QAudioOutput
    void unavailable(const QString &reason);

public slots:
    void bufferReceived(const QAudioBuffer &buffer);

private:
    void ensureSink(const QAudioFormat &format);
    QByteArray render(const QAudioBuffer &buffer, bool *processed);
    void applyVolume();

    QAudioSink *m_sink = nullptr;
    QIODevice *m_io = nullptr;
    QAudioFormat m_format;
    QAudioDevice m_device;
    EqProcessor m_eq;
    double m_volume = 0.7;
    bool m_muted = false;
    bool m_loggedUnsupported = false;
    bool m_unavailable = false;
    qint64 m_dropped = 0;
};

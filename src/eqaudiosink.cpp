#include "eqaudiosink.h"

#include "perf.h"

#include <QAudioBuffer>
#include <QAudioSink>
#include <QAudio>
#include <QMediaDevices>
#include <QIODevice>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
double linearVolume(double slider)
{
    return QAudio::convertVolume(slider, QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale);
}
} // namespace

EqAudioSink::EqAudioSink(QObject *parent)
    : QObject(parent)
{
}

EqAudioSink::~EqAudioSink()
{
    if (m_sink)
        m_sink->stop();
}

void EqAudioSink::setVolume(double slider)
{
    m_volume = std::clamp(slider, 0.0, 1.0);
    applyVolume();
}

void EqAudioSink::setMuted(bool muted)
{
    m_muted = muted;
    applyVolume();
}

void EqAudioSink::setGain(int band, double db)
{
    m_eq.setGain(band, db);
}

void EqAudioSink::setDevice(const QAudioDevice &device)
{
    if (m_device == device)
        return;
    m_device = device;
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        m_io = nullptr;
        m_format = QAudioFormat();
    }
}

void EqAudioSink::applyVolume()
{
    if (m_sink)
        m_sink->setVolume(m_muted ? 0.0 : linearVolume(m_volume));
}

void EqAudioSink::ensureSink(const QAudioFormat &format)
{
    if (m_sink && m_format == format)
        return;
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
        m_io = nullptr;
    }

    const QAudioDevice device = m_device.isNull() ? QMediaDevices::defaultAudioOutput() : m_device;
    if (device.isNull()) {
        if (!m_unavailable) {
            m_unavailable = true;
            emit unavailable(QStringLiteral("没有可用的音频输出设备"));
        }
        return;
    }
    m_format = format;
    m_eq.configure(format.sampleRate(), format.channelCount());
    m_sink = new QAudioSink(device, format, this);
    m_io = m_sink->start();
    applyVolume();
    if (!m_io) {
        qWarning() << "eq: QAudioSink failed to start";
        if (!m_unavailable) {
            m_unavailable = true;
            emit unavailable(QStringLiteral("音频输出格式不被设备接受"));
        }
    } else {
        m_unavailable = false;
        qCDebug(lcPerf).nospace() << "eq sink format=" << format.sampleRate()
                                  << "Hz/" << format.channelCount() << "ch/"
                                  << int(format.sampleFormat());
    }
}

QByteArray EqAudioSink::render(const QAudioBuffer &buffer, bool *processed)
{
    *processed = false;
    const QAudioFormat fmt = buffer.format();
    const qsizetype frames = buffer.frameCount();
    const int channels = fmt.channelCount();
    if (frames <= 0 || channels <= 0)
        return {};

    const auto raw = QByteArray::fromRawData(buffer.constData<char>(),
                                             buffer.byteCount());
    switch (fmt.sampleFormat()) {
    case QAudioFormat::Float: {
        QByteArray out = raw;
        m_eq.process(reinterpret_cast<float *>(out.data()), size_t(frames));
        *processed = true;
        return out;
    }
    case QAudioFormat::Int16: {
        const auto *src = buffer.constData<qint16>();
        std::vector<float> tmp(size_t(frames * channels));
        for (size_t i = 0; i < tmp.size(); ++i)
            tmp[i] = float(src[i]) / 32768.0f;
        m_eq.process(tmp.data(), size_t(frames));
        QByteArray out(int(tmp.size() * sizeof(qint16)), Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint16 *>(out.data());
        for (size_t i = 0; i < tmp.size(); ++i)
            dst[i] = qint16(std::clamp(std::lround(tmp[i] * 32767.0f), -32768L, 32767L));
        *processed = true;
        return out;
    }
    case QAudioFormat::Int32: {
        const auto *src = buffer.constData<qint32>();
        std::vector<float> tmp(size_t(frames * channels));
        for (size_t i = 0; i < tmp.size(); ++i)
            tmp[i] = float(double(src[i]) / 2147483648.0);
        m_eq.process(tmp.data(), size_t(frames));
        QByteArray out(int(tmp.size() * sizeof(qint32)), Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint32 *>(out.data());
        for (size_t i = 0; i < tmp.size(); ++i)
            dst[i] = qint32(std::clamp(std::llround(double(tmp[i]) * 2147483647.0),
                                       -2147483648LL, 2147483647LL));
        *processed = true;
        return out;
    }
    default:
        if (!m_loggedUnsupported) {
            m_loggedUnsupported = true;
            qWarning() << "eq: unsupported sample format" << int(fmt.sampleFormat())
                       << "- passing through";
        }
        return raw;
    }
}

void EqAudioSink::bufferReceived(const QAudioBuffer &buffer)
{
    const QAudioFormat format = buffer.format();
    if (!format.isValid())
        return;
    ensureSink(format);
    if (!m_io)
        return;

    bool processed = false;
    const QByteArray data = render(buffer, &processed);
    if (data.isEmpty())
        return;
    if (m_sink->bytesFree() < data.size()) {
        if (++m_dropped == 1 || m_dropped % 100 == 0)
            qCDebug(lcPerf).nospace() << "eq drop=" << m_dropped
                                      << " free=" << m_sink->bytesFree()
                                      << " need=" << data.size();
        return;
    }
    m_io->write(data);
    if (!processed)
        qCDebug(lcPerf) << "eq passthrough";
}

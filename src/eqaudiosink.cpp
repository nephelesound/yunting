#include "eqaudiosink.h"

#include "perf.h"

#include <QAudioBuffer>
#include <QAudioSink>
#include <QAudio>
#include <QMediaDevices>
#include <QIODevice>
#include <QLoggingCategory>
#include <QTimer>
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
    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(10);
    connect(m_drainTimer, &QTimer::timeout, this, &EqAudioSink::drain);
}

EqAudioSink::~EqAudioSink()
{
    stopAndClear();
}

void EqAudioSink::stopAndClear()
{
    m_pending.clear();
    if (m_drainTimer)
        m_drainTimer->stop();
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    m_io = nullptr;
    m_format = QAudioFormat();
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
    stopAndClear();
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
    stopAndClear();

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
    // 默认 sink 缓冲区只有一两帧的量；主线程被 UI 或内存回收拖住时，
    // QAudioBufferOutput 的下一块来不及写进去就会断音。留 500ms 余量。
    const qint64 bufferBytes = format.bytesForDuration(500000);
    if (bufferBytes > 0)
        m_sink->setBufferSize(bufferBytes);
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
                                  << int(format.sampleFormat())
                                  << " buffer=" << m_sink->bufferSize();
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
    // 平直曲线下不需要转换采样格式，也不跑 biquad；直接把原始 PCM 交给 sink。
    if (m_eq.neutral())
        return raw;
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
    Q_UNUSED(processed)

    // sink 满时不丢整块：先入队，再由 drain() 按空闲空间写入。
    m_pending.append(data);
    const qint64 maxPending = m_format.bytesForDuration(1000000);
    if (maxPending > 0 && m_pending.size() > maxPending) {
        int drop = int(m_pending.size() - maxPending);
        const int frameBytes = m_format.bytesPerFrame();
        if (frameBytes > 0)
            drop -= drop % frameBytes;
        if (drop > 0) {
            m_pending.remove(0, drop);
            if (++m_dropped == 1 || m_dropped % 100 == 0)
                qCDebug(lcPerf).nospace() << "eq drop=" << m_dropped
                                          << " pending=" << m_pending.size()
                                          << " max=" << maxPending;
        }
    }
    drain();
}

void EqAudioSink::drain()
{
    if (!m_io || !m_sink) {
        m_pending.clear();
        if (m_drainTimer)
            m_drainTimer->stop();
        return;
    }
    const int frameBytes = m_format.bytesPerFrame();
    while (!m_pending.isEmpty() && frameBytes > 0) {
        const qint64 free = m_sink->bytesFree();
        if (free <= 0)
            break;
        qint64 count = std::min<qint64>(free, m_pending.size());
        count -= count % frameBytes;
        if (count <= 0)
            break;
        const qint64 written = m_io->write(m_pending.constData(), count);
        if (written <= 0)
            break;
        m_pending.remove(0, written);
        if (written < count)
            break;
    }
    if (m_pending.isEmpty()) {
        if (m_drainTimer)
            m_drainTimer->stop();
    } else if (m_drainTimer && !m_drainTimer->isActive()) {
        m_drainTimer->start();
    }
}

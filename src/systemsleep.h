#pragma once
// SystemSleep：系统要睡眠、刚从睡眠恢复。
//   aboutToSleep：systemd-logind 的 PrepareForSleep(true)，系统总线上的信号（Amarok 的 PowerManager 同样
//                 在这里暂停）。Flatpak 没有 --system-talk-name=org.freedesktop.login1 时收不到，没有这一步。
//   resumed：PrepareForSleep(false)；收不到 logind 时由时钟发现：睡眠期间 CLOCK_BOOTTIME 在走、
//            CLOCK_MONOTONIC 不走，两者之差增加了多少就是睡了多久。每次睡眠只发一次。
// 恢复后 PipeWire/PulseAudio 里睡前开着的音频流可能再也不出声（pipewire#2707 等），用的人要重建输出。
#include <QObject>

#include <functional>

class QTimer;

class SystemSleep : public QObject {
    Q_OBJECT
public:
    // suspendedMs：到目前为止系统一共睡了多少毫秒（默认读两个时钟之差）；测试替换它
    explicit SystemSleep(QObject *parent = nullptr, std::function<qint64()> suspendedMs = {});

signals:
    void aboutToSleep();
    void resumed();

private slots:
    void onPrepareForSleep(bool sleeping);
    void checkClock();

private:
    std::function<qint64()> m_suspendedMs;
    qint64 m_seenSuspendedMs = 0;
    QTimer *m_clock = nullptr;
};

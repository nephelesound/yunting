#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QMutex>
#include <QSysInfo>
#include <QtGlobal>

#include <cstdio>
#include <deque>

namespace {

constexpr std::size_t kKept = 200;

QMutex lock;
std::deque<QString> lines;
QtMessageHandler previous = nullptr;

const char *level(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "debug";
    case QtInfoMsg: return "info";
    case QtWarningMsg: return "warning";
    case QtCriticalMsg: return "critical";
    case QtFatalMsg: return "fatal";
    }
    return "?";
}

void handle(QtMsgType type, const QMessageLogContext &ctx, const QString &message)
{
    Diagnostics::record(type, ctx.category, message);
    if (previous)
        previous(type, ctx, message);
    else
        std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, ctx, message)));
}

} // namespace

void Diagnostics::install()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    previous = qInstallMessageHandler(handle);
}

void Diagnostics::record(QtMsgType type, const char *category, const QString &message)
{
    const bool ours = category && qstrncmp(category, "yunting.", 8) == 0;
    if (type == QtDebugMsg || (type == QtInfoMsg && !ours))
        return;
    const QString line = QStringLiteral("%1 %2 %3: %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                  QLatin1String(level(type)),
                                  QLatin1String(category ? category : "default"), message);
    QMutexLocker guard(&lock);
    lines.push_back(line);
    if (lines.size() > kKept)
        lines.pop_front();
}

QStringList Diagnostics::recent()
{
    QMutexLocker guard(&lock);
    return QStringList(lines.begin(), lines.end());
}

QString Diagnostics::report() const
{
    QStringList out{
        QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()),
        QStringLiteral("Qt %1, %2, %3").arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName(),
                                             QSysInfo::prettyProductName()),
        QStringLiteral("桌面：%1").arg(qEnvironmentVariable("XDG_CURRENT_DESKTOP")),
        QString(),
    };
    const QStringList log = recent();
    if (log.isEmpty())
        out << QStringLiteral("（最近没有警告或错误）");
    else
        out << log;
    return out.join(u'\n');
}

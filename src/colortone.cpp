#include "colortone.h"

#include "materialyou.h"

#include <KColorScheme>
#include <KSharedConfig>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QVariant>

namespace {

constexpr auto kSchemeProperty = "KDE_COLOR_SCHEME_PATH";

struct State {
    bool applied = false;
    bool refreshing = false;
    QString path;
    QFileSystemWatcher *watcher = nullptr;
    QTimer *timer = nullptr;
};

State &state()
{
    static State s;
    return s;
}

QString kdeglobalsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/kdeglobals");
}

KSharedConfigPtr systemConfig()
{
    KSharedConfigPtr config = KSharedConfig::openConfig();
    config->reparseConfiguration();
    return config;
}

QColor systemColor(KColorScheme::ColorSet set)
{
    return KColorScheme(QPalette::Active, set, systemConfig()).background().color();
}

bool shouldSynthesize()
{
    if (qEnvironmentVariable("YUNTING_TONE") == QLatin1String("0"))
        return false;
    // kde-material-you-colors 在用（当前生效的是它的方案）：由它决定配色。看的是生效的方案而不是装没装：
    // 装了却选回 Breeze 时它并没有接管什么；它的深色方案背景彩度很低，按颜色看也像中性，只能认方案名
    if (MaterialYou::activeSchemeIsMaterialYou())
        return false;
    // 有色调的自定义方案保持原样，只给中性方案（Breeze 默认）生成
    return ColorTone::isNeutral(systemColor(KColorScheme::Window));
}

QString schemePath(const MaterialYou::Scheme &scheme, bool dark)
{
    // 缓存目录是几个进程共用的：并行截图的 qmlshot、--screenshot 与正在运行的应用。所以临时文件带进程号，
    // 方案文件按内容命名（同样的方案写出同样的名字，谁先写成都行），也不删任何 tone-*.colors：
    // 别的进程可能正引用着它。一份约 4 KB，只按「强调色 × 深浅」区分，攒不了几份
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString temporary = dir + QStringLiteral("/tone-%1.tmp").arg(QCoreApplication::applicationPid());
    if (MaterialYou::writeKdeScheme(scheme, dark, temporary).isEmpty())
        return {};

    QFile file(temporary);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray content = file.readAll();
    file.close();

    const QString path = dir + QStringLiteral("/tone-")
        + QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex().left(12))
        + QStringLiteral(".colors");
    // 改名不成功时多半是另一个进程刚写好同一份：在就用它，不在就算失败
    if (QFile::exists(path) || !QFile::rename(temporary, path))
        QFile::remove(temporary);
    return QFile::exists(path) ? path : QString();
}

QString synthesize()
{
    const KSharedConfigPtr config = systemConfig();
    const QColor seed = KColorScheme(QPalette::Active, KColorScheme::Selection, config).background().color();
    const QColor window = KColorScheme(QPalette::Active, KColorScheme::Window, config).background().color();
    const bool dark = window.lightnessF() < 0.5;

    const MaterialYou::Scheme scheme = MaterialYou::schemeFor(seed, dark);
    const QString path = schemePath(scheme, dark);
    if (path.isEmpty())
        return {};

    if (state().applied && state().path == path)
        return path;

    QCoreApplication::instance()->setProperty(kSchemeProperty, path);
    QGuiApplication::setPalette(
        KColorScheme::createApplicationPalette(KSharedConfig::openConfig(path, KConfig::SimpleConfig)));
    state().applied = true;
    state().path = path;
    return path;
}

void clearOverride()
{
    if (!state().applied)
        return;
    QCoreApplication::instance()->setProperty(kSchemeProperty, QVariant());
    QGuiApplication::setPalette(KColorScheme::createApplicationPalette(systemConfig()));
    state().applied = false;
    state().path.clear();
}

void refresh()
{
    if (state().refreshing)
        return;
    state().refreshing = true;
    if (shouldSynthesize())
        synthesize();
    else
        clearOverride();
    state().refreshing = false;
}

void installWatcher()
{
    if (state().watcher)
        return;
    QFileSystemWatcher *watcher = new QFileSystemWatcher(QCoreApplication::instance());
    QTimer *timer = new QTimer(watcher);
    timer->setSingleShot(true);
    timer->setInterval(200);
    state().watcher = watcher;
    state().timer = timer;

    const QString path = kdeglobalsPath();
    const auto addPath = [watcher, path] {
        if (QFileInfo::exists(path) && !watcher->files().contains(path))
            watcher->addPath(path);
    };
    addPath();
    const QString directory = QFileInfo(path).absolutePath();
    if (QFileInfo::exists(directory) && !watcher->directories().contains(directory))
        watcher->addPath(directory);

    const auto changed = [addPath, timer] {
        addPath();
        timer->start();
    };
    QObject::connect(watcher, &QFileSystemWatcher::fileChanged, watcher, changed);
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, watcher, changed);
    QObject::connect(timer, &QTimer::timeout, watcher, [] { refresh(); });
}

} // namespace

namespace ColorTone {

bool isNeutral(const QColor &background)
{
    return background.hslSaturationF() < 0.15;
}

QString apply()
{
    installWatcher();
    if (!shouldSynthesize()) {
        clearOverride();
        return {};
    }
    return synthesize();
}

} // namespace ColorTone

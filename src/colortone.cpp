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
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString temporary = dir + QStringLiteral("/tone.tmp");
    MaterialYou::writeKdeScheme(scheme, dark, temporary);

    QFile file(temporary);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray content = file.readAll();
    file.close();

    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(content);
    const QString path = dir + QStringLiteral("/tone-")
        + QString::fromLatin1(hash.result().toHex().left(12)) + QStringLiteral(".colors");
    if (QFile::exists(path))
        QFile::remove(temporary);
    else
        QFile::rename(temporary, path);

    // 只留当前与上一份：旧文件可能还被上一次的平台主题 / 窗口属性引用
    const QString previous = state().path;
    const QFileInfoList stale = QDir(dir).entryInfoList({QStringLiteral("tone-*.colors")}, QDir::Files);
    for (const QFileInfo &entry : stale) {
        const QString absolute = entry.absoluteFilePath();
        if (absolute != path && absolute != previous)
            QFile::remove(absolute);
    }
    return path;
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

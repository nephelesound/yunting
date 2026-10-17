#include "tray.h"
#include "audioengine.h"

#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QWindow>

namespace {
constexpr char kAppId[] = "io.github.sudoskys.Yunting";

// 图标：应用自己的主题图标（与应用 id 同名），包里没有时退回通用音频图标（DESIGN「Tray」）
QString iconName()
{
    const QString own = QLatin1String(kAppId);
    return QIcon::hasThemeIcon(own) ? own : QStringLiteral("multimedia-audio-player");
}
} // namespace

Tray::Tray(AudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
    // Id 是托盘宿主记忆配置的键，固定为应用 id，不随显示名变
    , m_item(new KStatusNotifierItem(QLatin1String(kAppId), this))
{
    m_item->setTitle(QStringLiteral("云听"));
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_item->setIconByName(iconName());

    // KStatusNotifierItem 自带默认菜单只有一行英文应用名（ncm-player）；
    // DESIGN 的菜单第一行就是当前曲目，移除默认行，完全自持（见下方 setStandardActionsEnabled）。
    QMenu *menu = m_item->contextMenu();
    const QList<QAction *> defaultActions = menu->actions();
    for (QAction *a : defaultActions)
        menu->removeAction(a);
    m_trackAction = menu->addSection(QString());   // section：不可点
    m_playAction = menu->addAction(QString());
    QAction *prev = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-backward")),
                                    QStringLiteral("上一首"));
    QAction *next = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-forward")),
                                    QStringLiteral("下一首"));

    // KStatusNotifierItem 默认只在弹出前追加自己的「恢复/最小化」「退出」项（文案随 locale、
    // 恢复/最小化与左键重复）；关掉它，菜单完全自持：DESIGN 的五行 + 自带 退出 行为等价
    // （qApp->quit()）的「退出」。
    m_item->setStandardActionsEnabled(false);
    QAction *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                    QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    connect(m_playAction, &QAction::triggered, m_audio, &AudioEngine::toggle);
    connect(prev, &QAction::triggered, this, &Tray::previousRequested);
    connect(next, &QAction::triggered, this, &Tray::nextRequested);

    // 中键播放/暂停（secondaryActivate）；滚轮音量 ±5%，直接作用于 AudioEngine
    connect(m_item, &KStatusNotifierItem::secondaryActivateRequested, m_audio, &AudioEngine::toggle);
    connect(m_item, &KStatusNotifierItem::scrollRequested, this, [this](int delta, Qt::Orientation o) {
        if (o != Qt::Vertical)
            return;
        m_audio->setVolume(m_audio->volume() + (delta >= 0 ? 0.05 : -0.05));
    });

    connect(m_audio, &AudioEngine::trackChanged, this, &Tray::refreshTrackUi);
    connect(m_audio, &AudioEngine::playingChanged, this, &Tray::refreshPlayAction);
    refreshTrackUi();
    refreshPlayAction();
}

void Tray::setAssociatedWindow(QWindow *window)
{
    m_item->setAssociatedWindow(window);
}

void Tray::refreshTrackUi()
{
    const QVariantMap t = m_audio->track();
    const QString name = t.value("name").toString();
    const QString artist = t.value("artist").toString();
    // 「曲名 — 歌手」与 DESIGN 逐字一致（U+2014 两侧空格）
    const QString row = t.isEmpty() ? QStringLiteral("未在播放")
                                    : QStringLiteral("%1 — %2").arg(name, artist);
    m_trackAction->setText(row);
    m_item->setToolTip(iconName(),t.isEmpty() ? QStringLiteral("云听") : name,
                       t.isEmpty() ? QStringLiteral("未在播放") : artist);
}

void Tray::refreshPlayAction()
{
    m_playAction->setText(m_audio->playing() ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

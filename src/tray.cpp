#include "tray.h"
#include "audioengine.h"

#include <KStatusNotifierItem>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QWindow>

namespace {
// 图标：应用自己的主题图标（与 .desktop 同名），包里没有时退回通用音频图标（DESIGN「Tray」）
QString iconName()
{
    const QString own = QGuiApplication::desktopFileName();
    return QIcon::hasThemeIcon(own) ? own : QStringLiteral("multimedia-audio-player");
}
} // namespace

Tray::Tray(AudioEngine *audio, QObject *parent)
    : QObject(parent)
    , m_audio(audio)
    // Id 是托盘宿主记忆配置的键，固定为应用 id（.desktop 名），不随显示名变
    , m_item(new KStatusNotifierItem(QGuiApplication::desktopFileName(), this))
{
    m_item->setTitle(QGuiApplication::applicationDisplayName());
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    // 默认是 Passive，Plasma 会把它收进托盘的隐藏区：关窗口后看不到图标，像是程序退出了。
    // 关窗口只是隐藏到托盘，托盘图标是回到窗口的唯一入口，所以始终 Active
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setIconByName(iconName());

    // KStatusNotifierItem 自带默认菜单只有一行应用名；
    // DESIGN 的菜单第一行就是当前曲目，移除默认行，完全自持（见下方 setStandardActionsEnabled）。
    QMenu *menu = m_item->contextMenu();
    const QList<QAction *> defaultActions = menu->actions();
    for (QAction *a : defaultActions)
        menu->removeAction(a);
    m_trackAction = menu->addSection(QString());   // section：不可点
    m_playAction = menu->addAction(QString());
    m_likeAction = menu->addAction(QIcon::fromTheme(QStringLiteral("love-symbolic")), QString());
    QAction *prev = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-backward")),
                                    QStringLiteral("上一首"));
    QAction *next = menu->addAction(QIcon::fromTheme(QStringLiteral("media-skip-forward")),
                                    QStringLiteral("下一首"));
    menu->addSeparator();
    m_lyricsAction = menu->addAction(QStringLiteral("桌面歌词"));
    m_lyricsAction->setCheckable(true);
    m_lyricsLockAction = menu->addAction(QStringLiteral("锁定桌面歌词"));
    m_lyricsLockAction->setCheckable(true);
    QAction *mini = menu->addAction(QIcon::fromTheme(QStringLiteral("window-minimize-pip")),
                                    QStringLiteral("迷你播放器"));

    // KStatusNotifierItem 默认只在弹出前追加自己的「恢复/最小化」「退出」项（文案随 locale、
    // 恢复/最小化与左键重复）；关掉它，菜单完全自持，退出项等价于 qApp->quit()。
    m_item->setStandardActionsEnabled(false);
    menu->addSeparator();
    QAction *quit = menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")),
                                    QStringLiteral("退出"));
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    connect(m_playAction, &QAction::triggered, m_audio, &AudioEngine::toggle);
    connect(m_likeAction, &QAction::triggered, this, &Tray::likeToggleRequested);
    connect(prev, &QAction::triggered, this, &Tray::previousRequested);
    connect(next, &QAction::triggered, this, &Tray::nextRequested);
    connect(m_lyricsAction, &QAction::triggered, this, &Tray::desktopLyricsToggleRequested);
    connect(m_lyricsLockAction, &QAction::triggered, this, &Tray::desktopLyricsLockToggleRequested);
    connect(mini, &QAction::triggered, this, &Tray::miniRequested);

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
    refreshLikeAction();
    refreshLyricsActions();
}

void Tray::setAssociatedWindow(QWindow *window)
{
    m_item->setAssociatedWindow(window);
}

void Tray::notify(const QString &message)
{
    m_item->showMessage(QGuiApplication::applicationDisplayName(), message, iconName());
}

void Tray::setLiked(bool liked)
{
    if (liked == m_liked)
        return;
    m_liked = liked;
    refreshLikeAction();
    emit likedChanged();
}

void Tray::setDesktopLyricsShown(bool shown)
{
    if (shown == m_desktopLyricsShown)
        return;
    m_desktopLyricsShown = shown;
    refreshLyricsActions();
    emit desktopLyricsShownChanged();
}

void Tray::setDesktopLyricsLocked(bool locked)
{
    if (locked == m_desktopLyricsLocked)
        return;
    m_desktopLyricsLocked = locked;
    refreshLyricsActions();
    emit desktopLyricsLockedChanged();
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
    m_item->setToolTip(iconName(), t.isEmpty() ? QGuiApplication::applicationDisplayName() : name,
                       t.isEmpty() ? QStringLiteral("未在播放") : artist);
    refreshLikeAction();
}

void Tray::refreshPlayAction()
{
    m_playAction->setText(m_audio->playing() ? QStringLiteral("暂停") : QStringLiteral("播放"));
}

void Tray::refreshLikeAction()
{
    const bool hasTrack = !m_audio->track().isEmpty();
    m_likeAction->setEnabled(hasTrack);
    m_likeAction->setText(m_liked ? QStringLiteral("取消喜欢") : QStringLiteral("喜欢"));
}

void Tray::refreshLyricsActions()
{
    m_lyricsAction->setChecked(m_desktopLyricsShown);
    m_lyricsLockAction->setEnabled(m_desktopLyricsShown);
    m_lyricsLockAction->setChecked(m_desktopLyricsLocked);
}

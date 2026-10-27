#pragma once
// 托盘：KF6 KStatusNotifierItem（DESIGN「Tray」）。图标与悬停提示随曲目更新；
// 左键显示/隐藏主窗口由 setAssociatedWindow 后的 KStatusNotifierItem 自己处理，
// 中键播放/暂停、滚轮音量 ±5% 直接作用于 AudioEngine；
// 上一首/下一首、喜欢、迷你模式、桌面歌词只发信号：切歌规则只有 Playback 那一份，
// 红心与窗口状态只有 QML 那一份，QML 接对应信号并回写 liked / desktopLyricsShown /
// desktopLyricsLocked，菜单里的勾选与文案才和界面一致。
// 右键菜单在 KStatusNotifierItem 默认菜单（标题行 + 退出项）之上插入播放控制项。
#include <QObject>
#include <QVariantMap>

class QAction;
class AudioEngine;
class QWindow;
class KStatusNotifierItem;

class Tray : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool liked READ liked WRITE setLiked NOTIFY likedChanged)
    Q_PROPERTY(bool desktopLyricsShown READ desktopLyricsShown WRITE setDesktopLyricsShown
                   NOTIFY desktopLyricsShownChanged)
    Q_PROPERTY(bool desktopLyricsLocked READ desktopLyricsLocked WRITE setDesktopLyricsLocked
                   NOTIFY desktopLyricsLockedChanged)
public:
    explicit Tray(AudioEngine *audio, QObject *parent = nullptr);

    // QML 主窗口创建完成后调用；之后左键才是显示/隐藏
    void setAssociatedWindow(QWindow *window);
    // 系统通知（freedesktop 通知，经 KStatusNotifierItem）：只给窗口不在眼前时的失败用，
    // 窗口在前台时界面自己的提示条就够了
    Q_INVOKABLE void notify(const QString &message);

    bool liked() const { return m_liked; }
    void setLiked(bool liked);
    bool desktopLyricsShown() const { return m_desktopLyricsShown; }
    void setDesktopLyricsShown(bool shown);
    bool desktopLyricsLocked() const { return m_desktopLyricsLocked; }
    void setDesktopLyricsLocked(bool locked);

signals:
    void nextRequested();
    void previousRequested();
    void likeToggleRequested();
    void miniRequested();
    void desktopLyricsToggleRequested();
    void desktopLyricsLockToggleRequested();
    void likedChanged();
    void desktopLyricsShownChanged();
    void desktopLyricsLockedChanged();

private:
    // 当前曲目行（菜单）与悬停提示同源更新：没有曲目时是「未在播放」/「云听 · 未在播放」
    void refreshTrackUi();
    void refreshPlayAction();
    void refreshLikeAction();
    void refreshLyricsActions();

    AudioEngine *m_audio;
    KStatusNotifierItem *m_item;
    QAction *m_trackAction;
    QAction *m_playAction;
    QAction *m_likeAction;
    QAction *m_lyricsAction;
    QAction *m_lyricsLockAction;
    bool m_liked = false;
    bool m_desktopLyricsShown = false;
    bool m_desktopLyricsLocked = false;
};

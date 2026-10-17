#pragma once
// 托盘：KF6 KStatusNotifierItem（DESIGN「Tray」）。图标与悬停提示随曲目更新；
// 左键显示/隐藏主窗口由 setAssociatedWindow 后的 KStatusNotifierItem 自己处理，
// 中键播放/暂停、滚轮音量 ±5% 直接作用于 AudioEngine；
// 上一首/下一首只发信号——切歌规则只有 Playback 那一份，QML 接 nextRequested/previousRequested。
// 右键菜单在 KStatusNotifierItem 默认菜单（标题行 + 退出项）之上插入播放控制项。
#include <QObject>
#include <QVariantMap>

class QAction;
class AudioEngine;
class QWindow;
class KStatusNotifierItem;

class Tray : public QObject {
    Q_OBJECT
public:
    explicit Tray(AudioEngine *audio, QObject *parent = nullptr);

    // QML 主窗口创建完成后调用；之后左键才是显示/隐藏
    void setAssociatedWindow(QWindow *window);

signals:
    void nextRequested();
    void previousRequested();

private:
    // 当前曲目行（菜单）与悬停提示同源更新：没有曲目时是「未在播放」/「云听 · 未在播放」
    void refreshTrackUi();
    void refreshPlayAction();

    AudioEngine *m_audio;
    KStatusNotifierItem *m_item;
    QAction *m_trackAction;
    QAction *m_playAction;
};

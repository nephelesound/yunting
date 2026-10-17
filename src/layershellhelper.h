#pragma once
// Wayland overlay 小助手：把 QML 创建的 QWindow 交给 LayerShellQt。
// anchors 与 LayerShellQt 一致：1 top · 2 bottom · 4 left · 8 right。
// X11 或没有 LayerShellQt 时全部 no-op，调用方不需要分支。
#include <QObject>
#include <QString>

class QWindow;

class LayerShellHelper : public QObject {
    Q_OBJECT
public:
    explicit LayerShellHelper(QObject *parent = nullptr);

    Q_INVOKABLE void makeOverlay(QWindow *window, int anchors, int margin,
                                 bool keyboardOnDemand, const QString &scope);
    // 改已是 overlay 的窗口的锚定边与边距（overlay 不能被合成器拖动，移动靠这个）。
    // 与窗口尺寸的改动在同一次提交里生效，所以同一轮里一起改，不会有一帧错位
    Q_INVOKABLE void placeOverlay(QWindow *window, int anchors, int left, int top, int right, int bottom);
    Q_INVOKABLE void release(QWindow *window);
};

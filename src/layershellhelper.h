#pragma once
// Wayland overlay 小助手：把 QML 创建的 QWindow 交给 LayerShellQt。
// anchors 与 LayerShellQt 一致：1 top · 2 bottom · 4 left · 8 right。
// X11 或没有 LayerShellQt 时全部 no-op，调用方不需要分支。
#include <QObject>
#include <QString>

#include <memory>

class QWindow;
class RelativePointerManager;
class RelativePointer;

class LayerShellHelper : public QObject {
    Q_OBJECT
public:
    explicit LayerShellHelper(QObject *parent = nullptr);
    ~LayerShellHelper() override;

    Q_INVOKABLE void makeOverlay(QWindow *window, int anchors, int margin,
                                 bool keyboardOnDemand, const QString &scope);
    // 改已是 overlay 的窗口的锚定边与边距（overlay 不能被合成器拖动，移动靠这个）
    Q_INVOKABLE void placeOverlay(QWindow *window, int anchors, int left, int top, int right, int bottom);

    // 拖动 overlay：按住期间把指针的相对位移（zwp_relative_pointer_v1）发成 relativeMotion，
    // 调用方据此改边距。不读窗口内坐标，所以窗口移动不会反过来改变位移，也不必改窗口尺寸。
    // 返回 false：不是 Wayland 或合成器不支持，调用方改用 startSystemMove
    Q_INVOKABLE bool beginRelativeDrag();
    Q_INVOKABLE void endRelativeDrag();

Q_SIGNALS:
    void relativeMotion(qreal dx, qreal dy);

private:
    std::unique_ptr<RelativePointerManager> m_relativeManager;
    std::unique_ptr<RelativePointer> m_relativePointer;
};

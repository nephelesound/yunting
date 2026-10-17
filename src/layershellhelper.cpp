#include "layershellhelper.h"

#include <QGuiApplication>
#include <QMargins>
#include <QWindow>

#ifdef YUNTING_LAYERSHELL
#include <LayerShellQt/window.h>
#endif

LayerShellHelper::LayerShellHelper(QObject *parent)
    : QObject(parent)
{
}

void LayerShellHelper::makeOverlay(QWindow *window, int anchors, int margin,
                                   bool keyboardOnDemand, const QString &scope)
{
    if (!window)
        return;
#ifdef YUNTING_LAYERSHELL
    // LayerShellQt 只在 Wayland 会话里有意义；X11 下保持普通窗口行为。
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;

    window->setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    auto *layer = LayerShellQt::Window::get(window);
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::Anchors::fromInt(anchors)));
    layer->setMargins(QMargins(margin, margin, margin, margin));
    layer->setExclusiveZone(-1);
    layer->setKeyboardInteractivity(keyboardOnDemand
        ? LayerShellQt::Window::KeyboardInteractivityOnDemand
        : LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setScope(scope.isEmpty() ? QStringLiteral("yunting-overlay") : scope);
    layer->setCloseOnDismissed(false);
    layer->setActivateOnShow(false);
#else
    Q_UNUSED(anchors);
    Q_UNUSED(margin);
    Q_UNUSED(keyboardOnDemand);
    Q_UNUSED(scope);
#endif
}

void LayerShellHelper::placeOverlay(QWindow *window, int anchors, int left, int top, int right, int bottom)
{
    if (!window)
        return;
#ifdef YUNTING_LAYERSHELL
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return;
    auto *layer = LayerShellQt::Window::get(window);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::Anchors::fromInt(anchors)));
    layer->setMargins(QMargins(left, top, right, bottom));
#else
    Q_UNUSED(anchors);
    Q_UNUSED(left);
    Q_UNUSED(top);
    Q_UNUSED(right);
    Q_UNUSED(bottom);
#endif
}

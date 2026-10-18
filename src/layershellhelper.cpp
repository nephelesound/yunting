#include "layershellhelper.h"

#include <QGuiApplication>
#include <QMargins>
#include <QQuickWindow>
#include <QWindow>

#ifdef YUNTING_LAYERSHELL
#include <LayerShellQt/window.h>
#endif

#ifdef YUNTING_RELATIVE_POINTER
#include <QWaylandClientExtensionTemplate>
#include <functional>

#include "qwayland-relative-pointer-unstable-v1.h"

class RelativePointerManager
    : public QWaylandClientExtensionTemplate<RelativePointerManager, &QtWayland::zwp_relative_pointer_manager_v1::destroy>,
      public QtWayland::zwp_relative_pointer_manager_v1
{
public:
    RelativePointerManager()
        : QWaylandClientExtensionTemplate(1)
    {
        initialize();
    }
};

class RelativePointer : public QtWayland::zwp_relative_pointer_v1
{
public:
    RelativePointer(::zwp_relative_pointer_v1 *object, std::function<void(qreal, qreal)> onMotion)
        : QtWayland::zwp_relative_pointer_v1(object)
        , m_onMotion(std::move(onMotion))
    {
    }
    ~RelativePointer() override { destroy(); }

protected:
    // 用加速后的位移：与系统指针走的距离一致，窗口跟手
    void zwp_relative_pointer_v1_relative_motion(uint32_t, uint32_t, wl_fixed_t dx, wl_fixed_t dy,
                                                 wl_fixed_t, wl_fixed_t) override
    {
        m_onMotion(wl_fixed_to_double(dx), wl_fixed_to_double(dy));
    }

private:
    std::function<void(qreal, qreal)> m_onMotion;
};
#else
class RelativePointerManager {};
class RelativePointer {};
#endif

namespace {
bool onWayland()
{
    return QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
}
}

LayerShellHelper::LayerShellHelper(QObject *parent)
    : QObject(parent)
{
}

LayerShellHelper::~LayerShellHelper() = default;

void LayerShellHelper::makeOverlay(QWindow *window, int anchors, int margin,
                                   bool keyboardOnDemand, const QString &scope)
{
    if (!window)
        return;
#ifdef YUNTING_LAYERSHELL
    // LayerShellQt 只在 Wayland 会话里有意义；X11 下保持普通窗口行为。
    if (!onWayland())
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
    if (!onWayland())
        return;
    auto *layer = LayerShellQt::Window::get(window);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::Anchors::fromInt(anchors)));
    layer->setMargins(QMargins(left, top, right, bottom));
    // 边距要等窗口下一次提交才生效，LayerShellQt 自己不提交：画一帧，否则拖动时内容不变的窗口不跟手
    if (auto *quick = qobject_cast<QQuickWindow *>(window))
        quick->update();
    else
        window->requestUpdate();
#else
    Q_UNUSED(anchors);
    Q_UNUSED(left);
    Q_UNUSED(top);
    Q_UNUSED(right);
    Q_UNUSED(bottom);
#endif
}

bool LayerShellHelper::beginRelativeDrag()
{
#if defined(YUNTING_LAYERSHELL) && defined(YUNTING_RELATIVE_POINTER)
    if (!onWayland())
        return false;
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->pointer())
        return false;
    if (!m_relativeManager)
        m_relativeManager = std::make_unique<RelativePointerManager>();
    if (!m_relativeManager->isActive())
        return false;
    m_relativePointer = std::make_unique<RelativePointer>(
        m_relativeManager->get_relative_pointer(wayland->pointer()),
        [this](qreal dx, qreal dy) { Q_EMIT relativeMotion(dx, dy); });
    return true;
#else
    return false;
#endif
}

void LayerShellHelper::endRelativeDrag()
{
    m_relativePointer.reset();
}

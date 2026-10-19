#include "links.h"

#include "link_parser.h"

#include <QWindow>

Links::Links(QObject *parent) : QObject(parent) { }

void Links::setWindow(QWindow *window)
{
    m_window = window;
}

QString Links::firstLink(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (parseOpenLink(argument).valid())
            return argument;
    }
    return {};
}

void Links::dispatch(const QString &text)
{
    const OpenLink link = parseOpenLink(text);
    if (!link.valid())
        return;
    emit opened(linkKindName(link.kind), static_cast<double>(link.id));
}

void Links::start(const QStringList &arguments)
{
    m_ready = true;
    // 激活请求可能比 QML 先到：它比冷启动参数新，优先处理。
    const QString text = m_pending.isEmpty() ? firstLink(arguments) : m_pending;
    m_pending.clear();
    if (!text.isEmpty())
        dispatch(text);
}

void Links::activate(const QStringList &arguments)
{
    raiseWindow();
    const QString text = firstLink(arguments);
    if (text.isEmpty())
        return;
    if (!m_ready) {
        m_pending = text;
        return;
    }
    dispatch(text);
}

void Links::raiseWindow()
{
    if (!m_window)
        return;
    if (m_window->visibility() == QWindow::Minimized)
        m_window->showNormal();
    else if (!m_window->isVisible())
        m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

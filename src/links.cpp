#include "links.h"

#include "link_parser.h"
#include "ncmclient.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QWindow>

Links::Links(NcmClient *client, QObject *parent) : QObject(parent), m_client(client) { }

void Links::setWindow(QWindow *window)
{
    m_window = window;
    connect(window, &QWindow::activeChanged, this, [this] {
        if (m_window && m_window->isActive())
            offerClipboard();
    });
}

QString Links::firstLink(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (findLink(argument).found())
            return argument;
    }
    return {};
}

void Links::dispatch(const FoundLink &found)
{
    if (found.link.valid()) {
        emit opened(linkKindName(found.link.kind), static_cast<double>(found.link.id));
        return;
    }
    if (!found.shortUrl.isValid()) {
        emit failed(QStringLiteral("这里没有网易云音乐的链接"));
        return;
    }
    QPointer<Links> self(this);
    m_client->resolveRedirect(found.shortUrl, [self](const QUrl &target) {
        if (!self)
            return;
        if (target.isEmpty()) {
            emit self->failed(QStringLiteral("短链接没有打开，检查一下网络再试"));
            return;
        }
        const OpenLink link = parseOpenLink(target.toString());
        if (!link.valid()) {
            emit self->failed(QStringLiteral("这个短链接指向的内容云听打不开"));
            return;
        }
        emit self->opened(linkKindName(link.kind), static_cast<double>(link.id));
    });
}

void Links::open(const QString &text)
{
    dispatch(findLink(text));
}

void Links::start(const QStringList &arguments)
{
    m_ready = true;
    // 激活请求可能比 QML 先到：它比冷启动参数新，优先处理。
    const QString text = m_pending.isEmpty() ? firstLink(arguments) : m_pending;
    m_pending.clear();
    if (!text.isEmpty())
        open(text);
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
    open(text);
}

void Links::offerClipboard()
{
    const QClipboard *clipboard = QGuiApplication::clipboard();
    // 应用自己复制的（分享对话框的「复制链接」）不回头再提示
    if (!clipboard || clipboard->ownsClipboard())
        return;
    const QString text = clipboard->text().trimmed();
    if (text.isEmpty() || m_offered.contains(text) || !findLink(text).found())
        return;
    m_offered.insert(text);
    emit clipboardOffered(text);
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

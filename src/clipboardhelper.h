#pragma once
// QML 的 Clipboard 单例在无窗口 TextEdit 上调用 copy() 并不可靠（Wayland 实测复制不到系统剪贴板）。
// 真正的剪贴板由这个后端对象负责，main.qml 把它注入 Clipboard.live。
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QString>

class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE bool copy(const QString &text)
    {
        if (text.isEmpty())
            return false;
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(text);
            return true;
        }
        return false;
    }

    // 分享图：QML 的 grabToImage 只给得出文件，从文件读回来再放进剪贴板
    Q_INVOKABLE bool copyImage(const QString &path)
    {
        const QImage image(path);
        if (image.isNull())
            return false;
        if (QClipboard *clipboard = QGuiApplication::clipboard()) {
            clipboard->setImage(image);
            return true;
        }
        return false;
    }
};

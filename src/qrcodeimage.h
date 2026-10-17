#pragma once
// QML Image provider：image://qrcode/<percent-encoded-url> → 白底黑模块的二维码。
// 依赖 libqrencode；没有库时 CMake 不编译本文件，QML 隐藏二维码。
#include <QQuickImageProvider>

class QrCodeImageProvider : public QQuickImageProvider {
public:
    QrCodeImageProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

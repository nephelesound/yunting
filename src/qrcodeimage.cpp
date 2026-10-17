#include "qrcodeimage.h"

#include <QUrl>
#include <qrencode.h>

QrCodeImageProvider::QrCodeImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage QrCodeImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString text = QUrl::fromPercentEncoding(id.toUtf8());
    if (text.isEmpty())
        return {};

    QRcode *code = QRcode_encodeString(text.toUtf8().constData(), 2, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!code)
        return {};

    // 每个模块取整数像素；请求尺寸只决定目标边长，模块本身保持锐利
    const int modules = code->width;
    const int target = requestedSize.isValid() && requestedSize.width() > 0
                           ? requestedSize.width() : 240;
    const int scale = std::max(1, target / modules);
    QImage image(modules * scale, modules * scale, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (code->data[y * modules + x] & 1) {
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        image.setPixel(x * scale + dx, y * scale + dy, qRgb(0, 0, 0));
            }
        }
    }
    QRcode_free(code);
    if (size)
        *size = image.size();
    return image;
}

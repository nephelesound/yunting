// 二维码：白底（要高对比，深色主题下也是白底）圆角块，码按整数倍放大到 maxSide 以内，白底紧贴着码。
// Prison 只按整数倍画模块（保证清晰）：给它一个固定的框时，长链接的码放不下 2 倍就退回 1 倍，
// 一小块码漂在一大块白底里。这里反过来用码自己的最小尺寸（含静区）算倍数，再让白底跟着码走
import QtQuick
import org.kde.prison as Prison

Rectangle {
    id: qr

    property alias content: code.content
    property real maxSide: Tokens.tileSm

    // 码的最小尺寸已含 QR 规范要求的静区，白底只留圆角那一圈
    readonly property int pad: Tokens.space1
    readonly property int factor: code.minimumWidth > 0
        ? Math.max(1, Math.floor((qr.maxSide - 2 * qr.pad) / code.minimumWidth)) : 1

    implicitWidth: code.width + 2 * qr.pad
    implicitHeight: implicitWidth
    radius: Tokens.radius
    color: "white"

    Prison.Barcode {
        id: code
        anchors.centerIn: parent
        width: code.minimumWidth * qr.factor
        height: width
        barcodeType: Prison.Barcode.QRCode
    }
}

// 封面：真实图片 + 默认圆角，无描边无投影。全应用只有这一种封面画法。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: cover

    property alias source: image.source
    property int pixelSize: 300
    property int radius: Tokens.radius

    Rectangle {
        anchors.fill: parent
        radius: cover.radius
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.placeholder)
        visible: image.status !== Image.Ready
    }

    Image {
        id: image
        anchors.fill: parent
        sourceSize.width: cover.pixelSize
        sourceSize.height: cover.pixelSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        visible: false
    }

    Kirigami.ShadowedTexture {
        anchors.fill: parent
        source: image.status === Image.Ready ? image : null
        radius: cover.radius
        color: "transparent"
    }
}

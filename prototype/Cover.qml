// 封面：真实图片 + 默认圆角，无描边无投影。全应用只有这一种封面画法（含占位）。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: cover

    property alias source: image.source
    property int pixelSize: 300
    property int radius: Tokens.radius

    // 占位：加载中、没有封面、链接失效时都一样——淡底 + 专辑图标，不留一个看起来像渲染出错的空方块
    Rectangle {
        anchors.fill: parent
        radius: cover.radius
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.placeholder)
        visible: image.status !== Image.Ready

        Kirigami.Icon {
            anchors.centerIn: parent
            width: cover.width < Tokens.height12 * 2 ? Tokens.iconSm : Tokens.iconLg
            height: width
            source: "emblem-music-symbolic"   // 16 与 32 都清晰；media-album-cover 在 16 下只剩一个方框
            color: Kirigami.Theme.textColor
            isMask: true
            opacity: Tokens.faded              // 遮罩图标的 color 不吃 alpha，淡化只能用 opacity
        }
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

    // 图片到了再淡入（只动 opacity）：换歌、列表滚动加载时封面不会「啪」地跳出来
    Kirigami.ShadowedTexture {
        anchors.fill: parent
        source: image.status === Image.Ready ? image : null
        radius: cover.radius
        color: "transparent"
        opacity: image.status === Image.Ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
        }
    }
}

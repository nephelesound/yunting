// 封面：真实图片 + 默认圆角，无描边，默认无投影。全应用只有这一种封面画法（含占位）。
// 投影只给正在播放页的大封面（shadowed）：那一页它是主角，像一张放在桌上的封套
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: cover

    property alias source: image.source
    property int pixelSize: 300
    property int radius: Tokens.radius
    property bool shadowed: false

    // 占位：加载中、没有封面、链接失效时都一样，是淡底 + 专辑图标，不留一个看起来像渲染出错的空方块
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
        onStatusChanged: cover.watchLoading()
        // 加载中途换图，状态一直是 Loading、不发 statusChanged；内存里有的图又会跳过 Loading 直接 Ready。都从换图这一刻重新算
        onSourceChanged: {
            cover.waited = false;
            cover.watchLoading();
        }
    }

    // 只有真在等网络的封面才淡入。磁盘缓存里的几毫秒就解码好了，页面每次导航都重建，
    // 若照样淡入，回到看过的页面时每张封面都像重新下载了一遍
    property bool waited: false
    function watchLoading() {
        if (image.status === Image.Loading) {
            cover.waited = false;
            patience.restart();
        } else {
            patience.stop();
        }
    }
    Component.onCompleted: cover.watchLoading()   // 首次设的 source 不一定触发 onStatusChanged
    Timer {
        id: patience
        interval: Tokens.motionFast
        onTriggered: cover.waited = true
    }

    // 等来的图片淡入（只动 opacity）：换歌、列表滚动加载时封面不会「啪」地跳出来
    Kirigami.ShadowedTexture {
        objectName: "coverTexture"
        anchors.fill: parent
        source: image.status === Image.Ready ? image : null
        radius: cover.radius
        color: "transparent"
        shadow.size: cover.shadowed ? Tokens.contactShadowSize : 0
        shadow.yOffset: cover.shadowed ? Tokens.contactShadowY : 0
        shadow.color: Qt.alpha("black", Tokens.contactShadowOpacity)
        opacity: image.status === Image.Ready ? 1 : 0
        Behavior on opacity {
            enabled: cover.waited
            NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
        }
    }
}

// 封面氛围底：正在播放页的背景（装修阶段第一项，功能是「这首歌的氛围」，这一屏密度最低、氛围就是内容）。
// 预模糊：封面按 32px 取图、平滑放大到整页，放大本身就是柔和的色场，不做任何实时模糊；走封面磁盘缓存。
// 可读性：上面盖 85% 的系统底色，文字对比度与普通页面基本一致，只透出封面的色调；亮暗配色都成立。
// 换歌：两层交叉淡化（只动 opacity，最慢一档：被动观看的环境变化）。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: ambience

    property string cover   // 曲目封面地址（任意尺寸参数，这里统一换成 32px）

    readonly property string tinySource: ambience.cover ? Source.sized(ambience.cover, 32) : ""

    // 交叉淡化：新图加载好后在另一层淡入，旧层淡出
    property bool frontIsA: true
    onTinySourceChanged: {
        const back = ambience.frontIsA ? layerB : layerA;
        back.source = ambience.tinySource;
    }

    component Layer: Image {
        anchors.fill: parent
        sourceSize.width: 32
        sourceSize.height: 32
        fillMode: Image.PreserveAspectCrop
        smooth: true
        asynchronous: true
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: Tokens.motionSlow; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeInOut } }
    }

    Layer {
        id: layerA
        onStatusChanged: if (status === Image.Ready && String(source) === ambience.tinySource) ambience.show(layerA, layerB)
    }
    Layer {
        id: layerB
        onStatusChanged: if (status === Image.Ready && String(source) === ambience.tinySource) ambience.show(layerB, layerA)
    }
    function show(front, back) {
        front.opacity = 1;
        back.opacity = 0;
        ambience.frontIsA = front === layerA;
    }

    // 可读性遮罩：系统底色 85%
    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Kirigami.Theme.backgroundColor, Tokens.ambienceScrim)
    }
}

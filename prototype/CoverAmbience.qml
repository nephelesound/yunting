// 封面氛围底：正在播放页的背景（装修阶段第一项，功能是「这首歌的氛围」，这一屏密度最低、氛围就是内容）。
// 预模糊：封面按 8px 取图（服务器缩图时取平均）、平滑放大到整页，只剩颜色的渐变，不做任何实时模糊；走封面磁盘缓存。
// 不用 32px：对比强的封面（暗柱子、亮中间）放大后会在页面两侧留下一道道明暗条纹。
// 可读性：上面盖 85% 的系统底色，文字对比度与普通页面基本一致，只透出封面的色调；亮暗配色都成立。
// 换歌：两层交叉淡化（只动 opacity，最慢一档：被动观看的环境变化）。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: ambience

    property string cover   // 曲目封面地址（任意尺寸参数，这里统一换成 8px）

    readonly property string tinySource: ambience.cover ? Source.sized(ambience.cover, 8) : ""

    // 交叉淡化：新图加载好后在另一层淡入，旧层淡出
    property bool frontIsA: true
    onTinySourceChanged: {
        const back = ambience.frontIsA ? layerB : layerA;
        back.source = ambience.tinySource;
    }

    component Layer: Image {
        anchors.fill: parent
        sourceSize.width: 8
        sourceSize.height: 8
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

    // 与窗口标题栏软过渡：顶边从标题栏的颜色（Breeze 标题栏用 Header 配色）渐变到透明，不留一条硬边。
    // 不铺进标题栏：那是系统装饰，改成自绘标题栏就不原生了
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Tokens.height12 * 2
        Kirigami.Theme.colorSet: Kirigami.Theme.Header
        Kirigami.Theme.inherit: false
        gradient: Gradient {
            GradientStop { position: 0; color: Kirigami.Theme.backgroundColor }
            GradientStop { position: 1; color: Qt.alpha(Kirigami.Theme.backgroundColor, 0) }
        }
    }
}

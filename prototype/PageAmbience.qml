// 内容页的氛围底（歌单、专辑、歌手、用户）：页面上部透出这一页封面的色调，往下淡出到系统底色。
// 与正在播放页同一做法（CoverAmbience：小图放大的色场 + 85% 系统底色遮罩），只占上部且随高度消失：
// 一眼认出在哪一页，列表读起来与普通页面一样。没有封面时就是普通底色。
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: ambience

    property string cover

    // 底与渐变取窗口底色那一组（Header，与标题栏同色，见 main.qml 的 background），不取页面文字用的 View
    Kirigami.Theme.colorSet: Kirigami.Theme.Header
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }
    CoverAmbience {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.min(parent.height, Tokens.measureMax + Tokens.trackColumn)   // 680
        cover: ambience.cover

        // 往下淡出：上面三分之一保留色调，到底边完全是系统底色，不留一条边
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.3; color: Qt.alpha(Kirigami.Theme.backgroundColor, 0) }
                GradientStop { position: 1.0; color: Kirigami.Theme.backgroundColor }
            }
        }
    }
}

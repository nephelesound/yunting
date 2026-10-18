// 一行可点的「东西」：小封面 + 名称 + 一行说明（百科里的 MV、动态里分享的歌与歌单、通知里提到的歌单）。
// 与人物行同一套块状可点：悬停淡色调，键盘焦点描边。不可点时（例如视频）没有悬停，只是展示。
// 行的淡底比内容多出 4：放在列里时用 Layout.leftMargin: -Tokens.space1，封面仍落在列的对齐线上
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

AbstractButton {
    id: row

    property string cover
    property string title
    property string detail
    property real aspect: 1   // 封面宽高比：视频 16:9
    property bool round       // 人物（歌手）用圆形

    implicitHeight: Tokens.trackRow
    leftPadding: Tokens.space1
    rightPadding: Tokens.space3
    hoverEnabled: row.enabled
    Accessible.name: row.title

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor,
                        row.down ? Tokens.tintPressed : row.hovered ? Tokens.tintHover : 0)
        border.width: row.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
    }

    contentItem: RowLayout {
        spacing: Tokens.space3
        Cover {
            Layout.preferredHeight: Tokens.height10
            Layout.preferredWidth: Math.round(Tokens.height10 * row.aspect)
            source: Source.sized(row.cover, 160)
            pixelSize: 160
            radius: row.round ? Tokens.height10 / 2 : Tokens.radius
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: row.title
                font.pixelSize: Tokens.textBase
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.detail
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
        }
    }
}

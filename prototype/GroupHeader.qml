// 分组标题行：固定 40 高，左＝24 细体标题，右＝可选的文字动作（「全部」之类），动作右缘贴页边线。
// 给了 pivots 时，标题换成同字号的一排枢轴（一组里有几种同类内容：「歌单 / 歌手 / 专辑」）
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: header

    property string text
    property string actionText
    property var pivots: []      // [{ key, text }]
    property string current

    signal actionTriggered()
    signal pivotActivated(string key)

    Layout.fillWidth: true
    Layout.bottomMargin: Tokens.space3
    implicitHeight: Tokens.height10

    // 标题过长时省略，永远给右侧动作留位置
    Label {
        visible: header.pivots.length === 0
        anchors.left: parent.left
        anchors.right: action.visible ? action.left : parent.right
        anchors.rightMargin: action.visible ? Tokens.space4 : 0
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        font.pixelSize: Tokens.text2xl
        font.weight: Font.Light
        elide: Text.ElideRight
    }
    Pivot {
        visible: header.pivots.length > 0
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        items: header.pivots
        current: header.current
        onActivated: key => header.pivotActivated(key)
    }
    Button {
        id: action
        anchors.right: parent.right
        anchors.rightMargin: -Tokens.space2
        anchors.verticalCenter: parent.verticalCenter
        visible: header.actionText.length > 0
        implicitHeight: Tokens.height8
        flat: true
        text: header.actionText
        font.pixelSize: Tokens.textSm
        onClicked: header.actionTriggered()
    }
}

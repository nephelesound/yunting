// 分组标题行：固定 40 高，左＝24 细体标题，右＝可选的文字动作（「全部」之类），动作右缘贴页边线。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: header

    property string text
    property string actionText

    signal actionTriggered()

    Layout.fillWidth: true
    Layout.bottomMargin: Tokens.space3
    implicitHeight: Tokens.height10

    Label {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        font.pixelSize: Tokens.text2xl
        font.weight: Font.Light
    }
    Button {
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

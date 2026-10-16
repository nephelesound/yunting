// 顶栏（高 32，所有页一致，保证大标题在每一页都落在同一高度）：
// 左＝返回（子页面才有），右＝账号状态与它唯一的动作。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

RowLayout {
    id: bar

    property bool backVisible
    property bool loggedIn

    signal backRequested()
    signal loginRequested()

    implicitHeight: Tokens.height8
    spacing: Tokens.space2

    IconButton {
        Layout.leftMargin: -Tokens.space2 // 图形对齐页边线
        visible: bar.backVisible
        icon.name: "go-previous"
        text: "返回"
        onClicked: bar.backRequested()
    }

    Item { Layout.fillWidth: true }

    Label {
        text: bar.loggedIn ? "m1ssuo" : "未登录"
        font.pixelSize: Tokens.textSm
        color: Kirigami.Theme.disabledTextColor
    }
    Button {
        Layout.rightMargin: -Tokens.space2
        visible: !bar.loggedIn
        implicitHeight: Tokens.height8
        flat: true
        icon.name: "view-barcode-qr"
        icon.width: Tokens.iconSm
        icon.height: Tokens.iconSm
        text: "扫码登录"
        font.pixelSize: Tokens.textSm
        onClicked: bar.loginRequested()
    }
}

// 每一屏的骨架：一行页头（左＝标题/枢轴/面包屑，右＝账号状态与登录）→ space6 → 内容。
// 页头高 48、起点固定，切换页面时标题与内容起点都不跳动。内容区占满宽度，自己负责页边距。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Personal.js" as Personal

Kirigami.Page {
    id: screen

    property alias heading: title.text
    property alias pivots: title.pivots
    property alias currentPivot: title.current
    property alias parentText: title.parentText
    property bool loggedIn
    default property alias content: contentArea.data

    signal pivotActivated(string key)
    signal leaveRequested()
    signal loginRequested()

    padding: 0
    background: null
    globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Tokens.space4
        spacing: Tokens.space6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.pageMargin - 2 // 细体大字的左侧字距补偿，视觉上对齐页边线
            Layout.rightMargin: Tokens.pageMargin
            Layout.preferredHeight: Tokens.height12
            spacing: Tokens.space2

            PageTitle {
                id: title
                Layout.fillWidth: true
                Layout.maximumWidth: implicitWidth
                onPivotActivated: key => screen.pivotActivated(key)
                onParentActivated: screen.leaveRequested()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: screen.loggedIn ? Personal.account.name : "未登录"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Button {
                Layout.rightMargin: -Tokens.space2
                visible: !screen.loggedIn
                implicitHeight: Tokens.height8
                flat: true
                icon.name: "view-barcode-qr"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: "扫码登录"
                font.pixelSize: Tokens.textSm
                onClicked: screen.loginRequested()
            }
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}

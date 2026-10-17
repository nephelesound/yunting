// 每一屏的骨架：一行页头（左＝标题/枢轴/面包屑，右＝账号状态与登录）→ space6 → 内容。
// 页头高 48、起点固定，切换页面时标题与内容起点都不跳动。内容区占满宽度，自己负责页边距。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components

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
                // 宽度：想要 naturalWidth，空间不够时被压缩（最后一项省略）；不读子项宽度，避免绑定环
                Layout.fillWidth: true
                Layout.preferredWidth: naturalWidth
                Layout.maximumWidth: naturalWidth
                onPivotActivated: key => screen.pivotActivated(key)
                onParentActivated: screen.leaveRequested()
            }
            Item { Layout.fillWidth: true }

            // 状态挨着它的处理办法放：离线 → 重试；登录过期 → 重新登录。都不另起横幅
            RowLayout {
                visible: !Source.online
                spacing: Tokens.space1
                Kirigami.Icon {
                    implicitWidth: Tokens.iconSm
                    implicitHeight: Tokens.iconSm
                    source: "network-offline-symbolic"
                    color: Kirigami.Theme.neutralTextColor
                    isMask: true
                }
                Label {
                    text: "离线，显示的是上次的内容"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
                Button {
                    implicitHeight: Tokens.height8
                    flat: true
                    text: "重试"
                    font.pixelSize: Tokens.textSm
                    onClicked: Source.retry()
                }
            }
            Kirigami.Separator {
                visible: !Source.online
                Layout.preferredHeight: Tokens.iconSm
                Layout.leftMargin: Tokens.space2
                Layout.rightMargin: Tokens.space2
            }

            // 头像：人物用圆形；图片没到或没有时，Avatar 自己显示名字首字
            Components.Avatar {
                visible: screen.loggedIn && !Source.sessionExpired
                implicitWidth: Tokens.height7
                implicitHeight: Tokens.height7
                name: screen.loggedIn ? Source.account.name : ""
                source: screen.loggedIn ? Source.sized(Source.account.avatar, 96) : ""
                // 没有图片时用通用人像图标：中文名取首字母会显示成乱码
                initialsMode: Components.Avatar.InitialsMode.UseIcon
            }
            Label {
                text: Source.sessionExpired ? "登录已过期" : screen.loggedIn ? Source.account.name : "未登录"
                font.pixelSize: Tokens.textSm
                color: Source.sessionExpired ? Kirigami.Theme.neutralTextColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Button {
                Layout.rightMargin: -Tokens.space2
                visible: !screen.loggedIn || Source.sessionExpired
                implicitHeight: Tokens.height8
                flat: true
                icon.name: "view-barcode-qr"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: Source.sessionExpired ? "重新登录" : "扫码登录"
                font.pixelSize: Tokens.textSm
                onClicked: screen.loginRequested()
            }
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            // 进入：内容淡入并上移 8px（页头不动，作为空间锚点）。只动 opacity 与 transform
            opacity: 0
            transform: Translate { id: rise; y: Tokens.space2 }
            ParallelAnimation {
                running: true
                NumberAnimation { target: contentArea; property: "opacity"; to: 1; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
                NumberAnimation { target: rise; property: "y"; to: 0; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
            }
        }
    }
}

// 每一屏的骨架：一行页头（左＝标题/枢轴/面包屑，右＝账号状态与登录）→ space6 → 内容。
// 页头高 48、起点固定，切换页面时标题与内容起点都不跳动。内容区占满宽度，自己负责页边距。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

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

    // 返回这一页时恢复离开时的样子（页面每次导航都会重建）：页面把主滚动区交给 scroller，
    // 把要记住的属性名放进 kept（枢轴 pane、搜索词等）。main.qml 离开时存 saveView()，回来时 restoreView()
    property Flickable scroller: null
    property var kept: []
    function saveView() {
        const props = {};
        for (const name of screen.kept)
            props[name] = screen[name];
        return { props: props, y: screen.scroller ? screen.scroller.contentY - screen.scroller.originY : 0 };
    }
    function restoreView(view) {
        if (!view)
            return;
        for (const name in view.props)
            screen[name] = view.props[name];
        screen.pendingY = view.y;
        restoreTimeout.restart();
        screen.applyPendingY();
    }
    // 内容是异步到的：内容够高了再滚过去，没到就等；用户先动了滚轮，或 3 秒还没到，就不再跳
    property real pendingY: -1
    function applyPendingY() {
        const f = screen.scroller;
        if (screen.pendingY < 0 || !f)
            return;
        const room = f.contentHeight - f.height;
        if (room < screen.pendingY && restoreTimeout.running)
            return;
        f.contentY = f.originY + Math.max(0, Math.min(screen.pendingY, room));
        screen.pendingY = -1;
    }
    onScrollerChanged: screen.applyPendingY()
    Timer {
        id: restoreTimeout
        interval: 3000
        onTriggered: screen.applyPendingY()
    }
    Connections {
        target: screen.scroller
        enabled: screen.pendingY >= 0
        function onContentHeightChanged() { screen.applyPendingY(); }
        function onHeightChanged() { screen.applyPendingY(); }
        function onMovementStarted() { screen.pendingY = -1; }
    }

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

            // 账号：头像 + 名字。登录时整块可点，直接进自己的主页（会员、等级与退出登录都在那里）
            AbstractButton {
                id: account
                readonly property bool active: screen.loggedIn && !Source.sessionExpired
                Layout.rightMargin: account.active ? -Tokens.space2 : 0
                enabled: account.active && Source.account !== null && Source.account.id > 0
                hoverEnabled: true
                implicitHeight: Tokens.height8
                leftPadding: account.active ? Tokens.space1 : 0
                rightPadding: account.active ? Tokens.space2 : 0
                Accessible.name: "账号：" + accountName.text
                onClicked: Actions.openUser(Source.account.id)

                background: Rectangle {
                    radius: Tokens.radius
                    color: Qt.alpha(Kirigami.Theme.highlightColor, account.down ? Tokens.tintPressed : account.hovered ? Tokens.tintHover : 0)
                    border.width: account.visualFocus ? Tokens.lineStrong : 0
                    border.color: Kirigami.Theme.highlightColor
                }
                contentItem: RowLayout {
                    spacing: Tokens.space2
                    // 头像：人物用圆形；没有图片时用通用人像图标（中文名取首字母会显示成乱码）
                    PersonAvatar {
                        visible: account.active
                        implicitWidth: Tokens.height7
                        implicitHeight: Tokens.height7
                        name: screen.loggedIn ? Source.account.name : ""
                        source: screen.loggedIn ? Source.sized(Source.account.avatar, 96) : ""
                        pendant: screen.loggedIn ? Source.account.avatarPendant || "" : ""
                        pendantMinSize: 0
                    }
                    Label {
                        id: accountName
                        text: Source.sessionExpired ? "登录已过期" : screen.loggedIn ? Source.account.name : "未登录"
                        font.pixelSize: Tokens.textSm
                        color: Source.sessionExpired ? Kirigami.Theme.neutralTextColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    VipTag {
                        Layout.alignment: Qt.AlignVCenter
                        level: account.active ? Source.account.vipLevel || "" : ""
                    }
                }
            }
            Button {
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
            // 关于：应用信息与反馈用的诊断信息（KDE 应用没有菜单栏时的惯常位置：页头最右）
            IconButton {
                Layout.rightMargin: -Tokens.space2   // 平铺按钮的图标与页边线对齐
                icon.source: Tokens.aboutIcon
                text: "关于云听"
                onClicked: Actions.openAbout()
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

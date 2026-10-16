// 每一屏的骨架：顶栏 → 展示标题 → 事实行 → 内容。页头各行都挂在左页边线上，
// 页头到内容固定 space6，所以切换页面时标题与内容起点不跳动。内容区占满宽度，自己负责页边距（滚动渐隐需要）。
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: screen

    property alias heading: title.text
    property alias pivots: title.pivots
    property alias currentPivot: title.current
    property alias facts: factRow.items
    property alias backVisible: topBar.backVisible
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
        spacing: 0

        TopBar {
            id: topBar
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.pageMargin
            Layout.rightMargin: Tokens.pageMargin
            loggedIn: screen.loggedIn
            onBackRequested: screen.leaveRequested()
            onLoginRequested: screen.loginRequested()
        }

        PageTitle {
            id: title
            Layout.leftMargin: Tokens.pageMargin - 2 // 细体大字的左侧字距补偿，视觉上对齐页边线
            Layout.topMargin: Tokens.space2
            onPivotActivated: key => screen.pivotActivated(key)
        }

        Facts {
            id: factRow
            Layout.leftMargin: Tokens.pageMargin
            Layout.topMargin: Tokens.space1
            visible: items.length > 0
        }

        Item {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Tokens.space6
        }
    }
}

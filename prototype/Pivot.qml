// 区块内的枢轴：一排 24 细体标签，当前项实色、其余淡出，点淡出的切换。
// 与页头枢轴同一种语言，只是小一档；高度默认与 GroupHeader 相同（40），放在分组标题的位置上。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Row {
    id: pivot

    property var items: []        // [{ key, text }]
    property string current
    property int rowHeight: Tokens.height10

    signal activated(string key)

    spacing: Tokens.space6

    Repeater {
        model: pivot.items
        delegate: Label {
            id: tab
            required property var modelData
            readonly property bool active: modelData.key === pivot.current
            height: pivot.rowHeight
            verticalAlignment: Text.AlignVCenter
            text: modelData.text
            font.pixelSize: Tokens.text2xl
            font.weight: Font.Light
            // 键盘：非当前项进 Tab 顺序，回车 / 空格切换；聚焦时与悬停同样提亮，并加下划线
            activeFocusOnTab: !tab.active
            font.underline: tab.activeFocus
            opacity: active ? 1 : hover.hovered || tab.activeFocus ? Tokens.fadedHover : Tokens.faded
            Behavior on opacity { NumberAnimation { duration: Tokens.motionFast } }
            Accessible.role: Accessible.PageTab
            Accessible.name: text
            Keys.onReturnPressed: pivot.activated(tab.modelData.key)
            Keys.onSpacePressed: pivot.activated(tab.modelData.key)
            HoverHandler { id: hover; enabled: !tab.active; cursorShape: Qt.PointingHandCursor }
            TapHandler { enabled: !tab.active; onTapped: pivot.activated(tab.modelData.key) }
        }
    }
}

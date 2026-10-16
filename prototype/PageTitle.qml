// 展示标题行（36px 细体），三种形态，起点永远在页边线上：
//   顶层页：各枢轴并排，当前实色、其余淡出，点淡出的切换；
//   子页：「上级  本页」面包屑，上级淡出、点它返回（代替返回箭头，标题不因返回键而右移）；
//   否则：只有页名。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Row {
    id: title

    property string text
    property var pivots: []          // [{ key, text }]
    property string current
    property string parentText       // 非空即为子页

    signal pivotActivated(string key)
    signal parentActivated()

    readonly property var items: title.parentText.length > 0
        ? [{ key: "..", text: title.parentText }, { key: "", text: title.text }]
        : title.pivots.length > 0 ? title.pivots : [{ key: "", text: title.text }]

    spacing: Tokens.space6

    Repeater {
        model: title.items
        delegate: Label {
            id: item
            required property var modelData
            readonly property bool active: modelData.key === title.current || modelData.key === ""

            text: modelData.text
            font.pixelSize: Tokens.text4xl
            font.weight: Font.Light
            elide: Text.ElideRight
            opacity: active ? 1 : hover.hovered ? Tokens.fadedHover : Tokens.faded
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }

            HoverHandler { id: hover; enabled: !item.active; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                enabled: !item.active
                onTapped: item.modelData.key === ".." ? title.parentActivated() : title.pivotActivated(item.modelData.key)
            }
        }
    }
}

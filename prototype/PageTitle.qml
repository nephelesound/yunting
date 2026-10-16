// 展示标题：36px 细体。有枢轴时，各枢轴并排成一行展示字（当前实色、其余淡出）；没有就显示页名。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Row {
    id: title

    property string text
    property var pivots: []          // [{ key, text }]
    property string current

    signal pivotActivated(string key)

    spacing: Tokens.space6

    Repeater {
        model: title.pivots.length > 0 ? title.pivots : [{ key: "", text: title.text }]
        delegate: Label {
            id: pivot
            required property var modelData
            readonly property bool active: modelData.key === title.current || title.pivots.length === 0

            text: modelData.text
            font.pixelSize: Tokens.text4xl
            font.weight: Font.Light
            opacity: active ? 1 : hover.hovered ? 0.5 : 0.25
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }

            HoverHandler { id: hover; enabled: !pivot.active; cursorShape: Qt.PointingHandCursor }
            TapHandler { enabled: !pivot.active; onTapped: title.pivotActivated(pivot.modelData.key) }
        }
    }
}

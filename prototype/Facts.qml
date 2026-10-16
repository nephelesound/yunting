// 事实行：数值用前景色、单位/说明用弱化色，事实之间只用间距分隔（不用「·」）。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Row {
    id: facts

    property var items: [] // [{ value, label }]，value 可省略

    spacing: Tokens.space4

    Repeater {
        model: facts.items
        delegate: Row {
            id: fact
            required property var modelData
            spacing: Tokens.space1
            Label {
                visible: text.length > 0
                text: fact.modelData.value || ""
                font.pixelSize: Tokens.textBase
                font.weight: Font.Medium
                font.features: { "tnum": 1 }
            }
            Label {
                text: fact.modelData.label
                font.pixelSize: Tokens.textBase
                color: Kirigami.Theme.disabledTextColor
            }
        }
    }
}

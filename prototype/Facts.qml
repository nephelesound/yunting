// 事实行：数值用前景色、单位/说明用弱化色，事实之间只用间距分隔（不用「·」）；宽度不够时整条事实换行。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Flow {
    id: facts

    property var items: [] // [{ value, label, artistId }]，value、artistId 可省略

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
            // 带 artistId 的事实（专辑页的歌手）可点：正文色 + 悬停下划线，进歌手页
            Label {
                id: label
                readonly property bool link: (fact.modelData.artistId || 0) > 0
                visible: text.length > 0
                text: fact.modelData.label || ""
                font.pixelSize: Tokens.textBase
                font.underline: label.link && linkHover.hovered
                color: label.link ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                Accessible.role: label.link ? Accessible.Link : Accessible.StaticText
                HoverHandler { id: linkHover; enabled: label.link; cursorShape: Qt.PointingHandCursor }
                TapHandler { enabled: label.link; onTapped: Actions.openArtist(fact.modelData.artistId) }
            }
        }
    }
}

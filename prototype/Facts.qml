// 事实行：数值用前景色、单位/说明用弱化色，事实之间只用间距分隔（不用「·」）；宽度不够时整条事实换行。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Flow {
    id: facts

    property var items: [] // [{ value, label, artistId, albumId }]，除 label 外都可省略

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
            // 带 artistId / albumId 的事实可点（专辑页的歌手、正在播放的歌手与专辑）：正文色 + 悬停下划线
            Label {
                id: label
                readonly property real artistId: fact.modelData.artistId || 0
                readonly property real albumId: fact.modelData.albumId || 0
                readonly property bool link: label.artistId > 0 || label.albumId > 0
                visible: text.length > 0
                text: fact.modelData.label || ""
                font.pixelSize: Tokens.textBase
                font.underline: label.link && linkHover.hovered
                color: label.link ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                Accessible.role: label.link ? Accessible.Link : Accessible.StaticText
                HoverHandler { id: linkHover; enabled: label.link; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: label.link
                    onTapped: label.artistId > 0 ? Actions.openArtist(label.artistId) : Actions.openAlbum(label.albumId)
                }
            }
        }
    }
}

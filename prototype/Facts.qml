// 事实行：数值用前景色、单位/说明用弱化色，事实之间只用间距分隔（不用「·」）；宽度不够时整条事实换行。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Flow {
    id: facts

    property var items: [] // [{ value, label, artistId, albumId, artists }]，都可省略

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
            // 带 artistId / albumId 的事实可点（专辑页的歌手、正在播放的歌手与专辑）：正文色，悬停下划线（LinkText）。
            // 歌手事实可以带 artists（[{ text, artistId }]），多位歌手各自可点
            LinkText {
                readonly property bool link: (fact.modelData.artistId || 0) > 0 || (fact.modelData.albumId || 0) > 0
                visible: text.length > 0
                parts: fact.modelData.artists || (fact.modelData.label
                    ? [{ text: fact.modelData.label, artistId: fact.modelData.artistId, albumId: fact.modelData.albumId }] : [])
                font.pixelSize: Tokens.textBase
                color: link || fact.modelData.artists ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }
    }
}

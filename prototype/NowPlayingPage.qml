// 正在播放：密度最低的一屏，只有一个视觉焦点——当前这句歌词。
// 版面按 48 的行距排：歌词列表高度、封面边长、封面顶边都是 48 的整数倍，封面与歌词行在同一网格上。
// 左半屏居中放封面与歌手/专辑，右半屏是歌词；当前句用展示字号（36），其余 24 弱化，当前句与封面中线同高。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Data.js" as Data

ScreenFrame {
    id: page

    property var track
    property real elapsed

    heading: page.track ? page.track.name : ""

    readonly property int pitch: Tokens.height12
    readonly property int rows: Math.floor((areaHeight - Tokens.space6) / pitch)
    readonly property int areaHeight: lyrics.parent ? lyrics.parent.height : 0
    // 封面占奇数行，中线正好穿过某一行的中心，当前句就停在那一行；留两行给封面下方的事实
    readonly property int coverRows: { const n = Math.min(7, page.rows - 2); return n % 2 ? n : n - 1; }
    readonly property int coverTopRow: Math.floor((page.rows - page.coverRows - 1) / 2)
    readonly property int focusRow: page.coverTopRow + Math.floor(page.coverRows / 2)

    // 快照只抓了一首歌的歌词；其它歌曲显示空状态
    readonly property var lines: page.track && page.track.id === Data.lyricsTrackId ? Data.lyrics : []
    readonly property int lyricIndex: {
        let i = 0;
        while (i + 1 < page.lines.length && page.lines[i + 1].t <= page.elapsed)
            i++;
        return i;
    }

    // ── 左半：封面与事实 ──
    ColumnLayout {
        x: Math.round((parent.width / 2 - width) / 2)
        y: page.coverTopRow * page.pitch
        width: page.coverRows * page.pitch
        spacing: Tokens.space4

        Cover {
            Layout.preferredWidth: page.coverRows * page.pitch
            Layout.preferredHeight: Layout.preferredWidth
            source: page.track ? page.track.cover.replace("120y120", "800y800") : ""
            pixelSize: 800
        }
        Facts {
            items: page.track ? [{ value: page.track.artist, label: "" }, { label: page.track.album }] : []
        }
    }

    // ── 右半：歌词 ──
    ListView {
        id: lyrics
        x: parent.width / 2
        width: parent.width / 2 - Tokens.pageMargin
        height: page.rows * page.pitch
        clip: true
        model: page.lines
        currentIndex: page.lyricIndex
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: page.focusRow * page.pitch
        preferredHighlightEnd: (page.focusRow + 1) * page.pitch
        highlightMoveDuration: Kirigami.Units.longDuration

        delegate: Label {
            required property int index
            required property var modelData
            readonly property bool current: index === page.lyricIndex
            width: ListView.view.width
            height: page.pitch
            verticalAlignment: Text.AlignVCenter
            text: modelData.text
            font.pixelSize: current ? Tokens.text4xl : Tokens.text2xl
            font.weight: current ? Font.Medium : Font.Normal
            color: current ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            elide: Text.ElideRight
        }
    }

    Label {
        x: lyrics.x
        y: page.focusRow * page.pitch
        height: page.pitch
        visible: page.lines.length === 0
        verticalAlignment: Text.AlignVCenter
        text: "暂无歌词"
        font.pixelSize: Tokens.text2xl
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }
}

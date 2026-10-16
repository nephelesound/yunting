// 正在播放：密度最低的一屏。标题＝曲名，事实行＝歌手与专辑；左边封面（顶边与歌词首行对齐），
// 右边歌词是这一屏的主体，用 2xl 字号；当前句实色，其余弱化，当前句停在封面上三分之一处。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "Data.js" as Data

ScreenFrame {
    id: page

    property var track
    property real elapsed

    heading: page.track ? page.track.name : ""
    backVisible: true
    facts: page.track ? [{ label: page.track.artist }, { label: page.track.album }] : []

    readonly property int lyricIndex: {
        let i = 0;
        while (i + 1 < Data.lyrics.length && Data.lyrics[i + 1].t <= page.elapsed)
            i++;
        return i;
    }

    Cover {
        id: cover
        anchors.left: parent.left
        anchors.leftMargin: Tokens.pageMargin
        anchors.top: parent.top
        // 边长取歌词行距的整数倍，封面底边也落在某一句歌词的底线上
        width: Math.floor(Math.min(parent.height - Tokens.space6, Tokens.measureMax) / lyrics.pitch) * lyrics.pitch
        height: width
        source: page.track ? page.track.cover.replace("120y120", "800y800") : ""
        pixelSize: 800
    }

    // 歌词按 40px 行距排在网格上：列表高度取行距整数倍，当前句固定在第 3 行，
    // 所以首行顶边与封面顶边对齐，边缘不会出现被切掉半截的句子。
    ListView {
        id: lyrics
        readonly property int pitch: Tokens.height10
        anchors.left: cover.right
        anchors.leftMargin: Tokens.space6
        anchors.right: parent.right
        anchors.rightMargin: Tokens.pageMargin
        anchors.top: parent.top
        height: Math.floor((parent.height - Tokens.space6) / pitch) * pitch
        clip: true
        model: Data.lyrics
        currentIndex: page.lyricIndex
        highlightRangeMode: ListView.StrictlyEnforceRange
        preferredHighlightBegin: 2 * pitch
        preferredHighlightEnd: 3 * pitch
        highlightMoveDuration: Kirigami.Units.longDuration

        delegate: Label {
            required property int index
            required property var modelData
            width: ListView.view.width
            height: lyrics.pitch
            verticalAlignment: Text.AlignVCenter
            text: modelData.text
            font.pixelSize: Tokens.text2xl
            font.weight: index === page.lyricIndex ? Font.Medium : Font.Normal
            color: index === page.lyricIndex ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
            elide: Text.ElideRight
        }
    }
}

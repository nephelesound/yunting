// 文字链接：一行文字由几段组成，带 artistId / albumId 的段可点，打开歌手页 / 专辑页；
// 带 link: true 的段点了发 activated(index)，由调用方决定去哪。
// 全应用的文字链接只有这一种样子：静止时与周围文字相同（颜色由调用方给），
// 指针下的那一段加下划线并提到正文色、指针变手形；块状的可点物（行、磁贴、按钮）用淡色调底。
// 几段拼成一个 StyledText，整行一起省略；不带 id 的段（没有 id 的合作歌手）只是文字。
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Label {
    id: link

    property var parts: []   // [{ text, artistId, albumId, link }]，都可省略；link: true 的段点了发 activated
    property string separator: " / "

    signal activated(int index)

    readonly property int hoveredPart: area.containsMouse ? link.partAt(area.mouseX) : -1

    function linked(part) { return (part.artistId || 0) > 0 || (part.albumId || 0) > 0 || part.link === true; }
    function escaped(text) { return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }

    // 指针下是哪一段可点的：按字体量各段的宽度累加，不在可点段上返回 -1
    function partAt(x) {
        let start = 0;
        for (let i = 0; i < link.parts.length; ++i) {
            if (i > 0)
                start += metrics.advanceWidth(link.separator);
            const end = start + metrics.advanceWidth(link.parts[i].text);
            if (x < end)
                return x >= start && link.linked(link.parts[i]) ? i : -1;
            start = end;
        }
        return -1;
    }

    text: link.parts.map((p, i) => i === link.hoveredPart
        ? "<u><font color='" + Kirigami.Theme.textColor + "'>" + link.escaped(p.text) + "</font></u>"
        : link.escaped(p.text)).join(link.escaped(link.separator))
    textFormat: Text.StyledText
    elide: Text.ElideRight
    Accessible.role: link.parts.some(p => link.linked(p)) ? Accessible.Link : Accessible.StaticText
    Accessible.name: link.parts.map(p => p.text).join(link.separator)

    FontMetrics { id: metrics; font: link.font }
    // 按下时按按下点判断：在可点段上才接下这次点击，否则放给外层（所在的曲目行照常播放）。
    // 不按悬停状态判断：按下前不一定来过移动事件
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: link.hoveredPart >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: mouse => mouse.accepted = link.partAt(mouse.x) >= 0
        onClicked: mouse => {
            const index = link.partAt(mouse.x);
            const part = link.parts[index];
            if (!part)
                return;
            if ((part.artistId || 0) > 0)
                Actions.openArtist(part.artistId);
            else if ((part.albumId || 0) > 0)
                Actions.openAlbum(part.albumId);
            else
                link.activated(index);
        }
    }
}

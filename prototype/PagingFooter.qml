// 分页列表的底部状态行（评论、歌手的全部歌曲）：在取 / 没取到（可点重试）/ 已经全部。
// paging 是 Source 的分页累积 { loading, failed, more, items }；为 null 或还能继续取时不占位
import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Label {
    id: footer

    property var paging: null
    property string loadingText
    property string doneText

    signal retryRequested()

    readonly property bool failed: footer.paging !== null && (footer.paging.failed || false)

    height: visible ? Tokens.height10 : 0
    visible: footer.paging !== null && footer.paging.items.length > 0
             && (footer.paging.loading || footer.failed || !footer.paging.more)
    verticalAlignment: Text.AlignVCenter
    horizontalAlignment: Text.AlignHCenter
    text: !footer.paging ? "" : footer.paging.loading ? footer.loadingText : footer.failed ? "没取到，点此重试" : footer.doneText
    font.pixelSize: Tokens.textSm
    font.underline: footer.failed && retryHover.hovered
    color: footer.failed ? Kirigami.Theme.textColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    Accessible.role: footer.failed ? Accessible.Button : Accessible.StaticText

    HoverHandler { id: retryHover; enabled: footer.failed; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: footer.failed; onTapped: footer.retryRequested() }
}

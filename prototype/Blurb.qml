// 左窗格里的简介（专辑、歌单、歌手）：只露 3 行，写不下时给「展开全文」，在对话框里读完整的一段。
// 不原地展开：左窗格不滚动，长简介会把下面的播放按钮挤出窗口
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: blurb

    property string text
    property string title   // 对话框标题：这段简介属于谁

    visible: blurb.text.length > 0
    spacing: Tokens.space1

    Label {
        id: preview
        Layout.fillWidth: true
        text: blurb.text
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
    }
    LinkText {
        objectName: "blurbMore"
        visible: preview.truncated
        parts: [{ text: "展开全文", link: true }]
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        onActivated: fullText.open()
    }
    AppDialog {
        id: fullText
        objectName: "blurbDialog"
        parent: Overlay.overlay
        title: blurb.title
        message: blurb.text
    }
}

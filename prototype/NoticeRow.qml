// 一条消息（评论回复、@我、系统通知）：头像 | 谁与何时一行，说了什么一段，
// 回复的是你的哪句话（弱化引一行），提到的东西一行（可点：歌直接放，歌单 / 专辑 / MV 打开）。
// 名字与头像可点，去对方主页；系统通知没有人时头像换成应用图标、不显示名字
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

RowLayout {
    id: row

    property var user: ({})     // { id, name, avatar }
    property real time
    property string text
    property string quote       // 被回复的那句（你自己说的）
    property var resource: null // { kind, id, name, cover, artist, track }

    readonly property bool person: (row.user.id || 0) > 0 || (row.user.name || "").length > 0
    spacing: Tokens.space3

    PersonAvatar {
        Layout.alignment: Qt.AlignTop
        implicitWidth: Tokens.height10
        implicitHeight: Tokens.height10
        visible: row.person
        name: row.user.name || ""
        source: row.user.avatar || ""
        pendant: row.user.avatarPendant || ""
        HoverHandler { enabled: (row.user.id || 0) > 0; cursorShape: Qt.PointingHandCursor }
        TapHandler { enabled: (row.user.id || 0) > 0; onTapped: Actions.openUser(row.user.id) }
    }
    Kirigami.Icon {
        Layout.alignment: Qt.AlignTop
        implicitWidth: Tokens.height10
        implicitHeight: Tokens.height10
        visible: !row.person
        source: "io.github.nephelesound.Yunting"
        fallback: "dialog-information"
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        spacing: Tokens.space1

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space3
            LinkText {
                visible: row.person
                Layout.maximumWidth: implicitWidth
                Layout.fillWidth: true
                parts: [{ text: row.user.name || "", link: (row.user.id || 0) > 0 }]
                font.pixelSize: Tokens.textBase
                color: Kirigami.Theme.textColor
                onActivated: Actions.openUser(row.user.id)
            }
            Label {
                text: Format.commentTime(row.time)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: row.text
            font.pixelSize: Tokens.textBase
            lineHeightMode: Text.FixedHeight
            lineHeight: Tokens.lineBase
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            visible: row.quote.length > 0
            text: "回复你的评论：" + row.quote
            font.pixelSize: Tokens.textSm
            lineHeightMode: Text.FixedHeight
            lineHeight: Tokens.lineSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        ResourceRow {
            Layout.fillWidth: true
            Layout.leftMargin: -Tokens.space1
            Layout.topMargin: Tokens.space1
            visible: row.resource !== null && row.resource.kind !== "other" && (row.resource.name || "").length > 0
            enabled: Actions.canOpen(row.resource)
            aspect: row.resource && (row.resource.kind === "mv" || row.resource.kind === "video") ? 16 / 9 : 1
            round: row.resource !== null && row.resource.kind === "artist"
            cover: row.resource ? row.resource.cover || "" : ""
            title: row.resource ? row.resource.name || "" : ""
            detail: row.resource ? row.resource.artist || "" : ""
            onClicked: Actions.openResource(row.resource)
        }
    }
}

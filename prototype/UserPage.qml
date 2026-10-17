// 用户主页：资料 + 公开歌单。与歌手页共用辅助窗格布局；评论者头像 / 名字点进来。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

ScreenFrame {
    id: page

    property real userId

    readonly property var user: Source.user(page.userId)
    // 自己的主页：账号的去处都在这里（页头的头像与名字直接跳过来），关注的位置换成退出登录
    readonly property bool own: page.userId > 0 && Source.account !== null && Source.account.id === page.userId
    heading: page.user.name || "用户"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.pageMargin
        spacing: Tokens.space6

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Tokens.trackColumn
            Layout.maximumWidth: Tokens.trackColumn
            spacing: 0

            PersonAvatar {
                Layout.preferredWidth: Tokens.trackColumn
                Layout.preferredHeight: Tokens.trackColumn
                name: page.user.name
                source: page.user.avatar
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                text: page.user.name || (Source.live ? "正在取回用户…" : "用户")
                font.pixelSize: Tokens.text2xl
                font.weight: Font.Light
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space2
                visible: text.length > 0
                text: page.user.signature
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Facts {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.space4
                items: [
                    { value: String(page.user.follows || 0), label: "关注" },
                    { value: String(page.user.followeds || 0), label: "粉丝" },
                    { value: String(page.user.playlistCount || 0), label: "歌单" },
                ].concat((page.user.listenSongs || 0) > 0
                    ? [{ value: Format.count(page.user.listenSongs), label: "累计听歌" }] : [])
            }

            // 破坏性操作，确认在对话框里；只用文字（次要操作），不配图标
            Button {
                Layout.topMargin: Tokens.space4
                visible: page.own
                implicitHeight: Tokens.height8
                flat: true
                text: "退出登录…"
                onClicked: Actions.logoutRequested()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.space3

            GroupHeader { text: "公开歌单" }

            GridView {
                id: playlists
                Layout.fillWidth: true
                Layout.fillHeight: true
                readonly property int usable: width - Tokens.pageMargin
                readonly property int columns: Math.max(1, Math.floor((usable + Tokens.space4) / (Tokens.tileLg + Tokens.space4)))
                cellWidth: (usable + Tokens.space4) / columns
                cellHeight: cellWidth - Tokens.space4 + Tokens.space2 + Tokens.height12 + Tokens.space4
                bottomMargin: Tokens.space6
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.user.playlists || []
                ScrollBar.vertical: ScrollBar {}
                WheelScroll { flickable: playlists }

                delegate: MediaTile {
                    required property var modelData
                    width: playlists.cellWidth - Tokens.space4
                    title: modelData.name
                    caption: modelData.count > 0 ? modelData.count + " 首" : "空歌单"
                    cover: modelData.cover
                    onClicked: Actions.openPlaylist(modelData.id)
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Tokens.pageMargin
                    visible: playlists.count === 0
                    icon.name: "view-media-playlist"
                    text: page.user.id > 0 ? "还没有公开歌单" : (Source.live ? "正在取回用户…" : "用户资料还没取到")
                }
            }
        }
    }
}

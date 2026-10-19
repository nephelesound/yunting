// 一条评论：头像 | 作者与时间挨在一起（都是关于「谁、何时」），赞在右；
// 回复别人时先引一行被回复的那条；正文放宽行距，超过 6 行折起（可展开）；有楼中楼时给「N 条回复」。
// 歌曲的评论区与楼中楼的回复共用这一个。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

RowLayout {
    id: row

    property var comment: ({})
    property bool likeable: true    // 楼中楼的赞只显示数量：后端的点赞只更新评论区，不更新回复
    property bool threadOpen        // 这条下面的回复正展开着
    property real threadOf: 0       // 在哪条评论的楼中楼里：回复的就是它时不再引一遍（它就在正上方）
    property bool expanded: false

    signal likeRequested(bool liked)
    signal threadToggled()

    readonly property var quoted: row.comment.beReplied && row.comment.beReplied.length > 0
                                  && row.comment.beReplied[0].id !== row.threadOf ? row.comment.beReplied[0] : null

    spacing: Tokens.space3

    PersonAvatar {
        Layout.alignment: Qt.AlignTop
        implicitWidth: Tokens.height8
        implicitHeight: Tokens.height8
        name: row.comment.user || ""
        source: row.comment.avatar || ""
        pendant: row.comment.avatarPendant || ""
        HoverHandler {
            enabled: (row.comment.userId || 0) > 0
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: (row.comment.userId || 0) > 0
            onTapped: Actions.openUser(row.comment.userId)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Tokens.space1

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space2
            LinkText {
                Layout.fillWidth: true
                Layout.maximumWidth: Math.ceil(implicitWidth)   // 宽度带小数时往下取整会省略掉最后一个字
                parts: [{ text: row.comment.user || "", link: (row.comment.userId || 0) > 0 }]
                font.pixelSize: Tokens.textSm
                color: Kirigami.Theme.textColor
                elide: Text.ElideRight
                onActivated: Actions.openUser(row.comment.userId)
            }
            VipTag { level: row.comment.vipLevel || "" }
            Label {
                text: Format.commentTime(row.comment.time)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Item { Layout.fillWidth: true }
            AbstractButton {
                id: like
                readonly property bool liked: row.comment.liked === true
                enabled: row.likeable && ((row.comment.id || 0) > 0 || !Source.live)
                implicitHeight: Tokens.height7
                leftPadding: Tokens.space1
                rightPadding: Tokens.space1
                hoverEnabled: true
                Accessible.name: like.liked ? "取消点赞评论" : "点赞评论"
                background: Rectangle {
                    radius: Tokens.radius
                    color: Qt.alpha(Kirigami.Theme.highlightColor,
                                    like.down ? Tokens.tintPressed : like.hovered ? Tokens.tintHover : 0)
                }
                contentItem: RowLayout {
                    spacing: Tokens.space1
                    Kirigami.Icon {
                        implicitWidth: Tokens.iconSm
                        implicitHeight: Tokens.iconSm
                        source: like.liked ? Tokens.upvotedIcon : Tokens.upvoteIcon
                        color: like.liked ? Tokens.liked : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        isMask: true
                    }
                    Label {
                        text: Format.count(row.comment.likes || 0)
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                        color: like.liked ? Tokens.liked : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }
                onClicked: row.likeRequested(!like.liked)
            }
        }

        // 回复了谁、回复的哪句：一行弱化的引文（被回复的人可点）
        LinkText {
            Layout.fillWidth: true
            visible: row.quoted !== null
            parts: row.quoted ? [{ text: "回复 @" + row.quoted.user, link: (row.quoted.userId || 0) > 0 },
                                 { text: row.quoted.content }] : []
            separator: "："
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            elide: Text.ElideRight
            onActivated: Actions.openUser(row.quoted.userId)
        }

        Label {
            id: content
            Layout.fillWidth: true
            text: row.comment.content || ""
            font.pixelSize: Tokens.textBase
            lineHeightMode: Text.FixedHeight
            lineHeight: Tokens.lineBase
            wrapMode: Text.Wrap
            maximumLineCount: row.expanded ? 1000 : 6
            elide: Text.ElideRight
        }

        // 折起的正文与楼中楼：两个文字链接排在一行
        // 显隐按条件判断，不能看子项的 visible：父项隐藏时子项的 visible 读出来也是 false，会互相锁死
        Row {
            spacing: Tokens.space4
            visible: content.truncated || row.expanded || (row.comment.replyCount || 0) > 0
            LinkText {
                id: expandLink
                objectName: "expandLink"
                visible: content.truncated || row.expanded
                parts: [{ text: row.expanded ? "收起" : "展开全文", link: true }]
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                onActivated: row.expanded = !row.expanded
            }
            LinkText {
                id: threadLink
                objectName: "threadLink"
                visible: (row.comment.replyCount || 0) > 0
                parts: [{ text: row.threadOpen ? "收起回复" : Format.count(row.comment.replyCount || 0) + " 条回复", link: true }]
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                onActivated: row.threadToggled()
            }
        }
    }
}

// 曲目行：高 48 = 封面边长。静止时无底色（重复元素保持 Level 0），悬停才出现淡色调。
// ranked 时左侧是 32 宽的名次列；正在播放把名次换成强调色播放图标，标题加粗。
// 不排名时，正在播放用悬挂在对齐线外侧的强调色竖条 + 加粗标题标出。
// 标题后依次是：别名（弱化括注）· 红心 · 购买限制（VIP / 付费）· 音质（Hi-Res / 无损）；时长未知时留空。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

AbstractButton {
    id: tile

    required property int index
    required property var modelData

    property bool current
    property bool ranked
    property bool locked // 匿名会话下的 VIP 曲：可见，点击时提示登录

    property bool removable  // 播放队列：悬停时显示「移出」
    readonly property bool liked: Source.isLiked(tile.modelData.id)   // 在红心歌单里：标题后一颗小红心
    property bool showReason // 每日推荐：右侧细节列优先显示推荐理由，没有理由再显示专辑
    readonly property string alias: tile.modelData.alias || ""
    property bool hangingMarker: true  // 左侧没有留白（紧挨别的元素）时关掉悬挂竖条

    signal activated(int index)
    signal removeRequested(int index)

    implicitHeight: Tokens.trackRow
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    onClicked: tile.activated(tile.index)
    Keys.onReturnPressed: tile.activated(tile.index)

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor, tile.down ? Tokens.tintPressed : tile.hovered ? Tokens.tintHover : 0)
        border.width: tile.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }

        Rectangle {
            visible: tile.current && !tile.ranked && tile.hangingMarker
            width: Tokens.space1
            height: Tokens.space6
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.left
            anchors.rightMargin: Tokens.space2
            radius: Tokens.radiusSm
            color: Kirigami.Theme.highlightColor
        }
    }

    contentItem: RowLayout {
        spacing: Tokens.space3

        Item {
            visible: tile.ranked
            Layout.preferredWidth: Tokens.height8
            Layout.fillHeight: true

            Label {
                anchors.fill: parent
                visible: !tile.current
                text: tile.index + 1
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: Tokens.textBase
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Kirigami.Icon {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: tile.current
                width: Tokens.iconSm
                height: Tokens.iconSm
                source: "media-playback-start"
                color: Kirigami.Theme.highlightColor
                isMask: true
            }
        }

        Cover {
            Layout.preferredWidth: Tokens.trackRow
            Layout.preferredHeight: Tokens.trackRow
            source: tile.modelData.cover
            pixelSize: 96
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0 // 宽度只由剩余空间决定，长标题不得挤出右侧各列
            spacing: 0

            // 标题 + VIP 标签：标题先省略，标签始终紧跟其后
            Item {
                id: titleRow
                Layout.fillWidth: true
                implicitHeight: title.implicitHeight

                // 标题（别名作弱化括注跟在后面，先于标题被省略）+ 标记：红心、购买限制、音质。
                // 标题先省略，标记始终完整地紧跟其后
                Label {
                    id: title
                    width: Math.min(implicitWidth, titleRow.width - (marks.width > 0 ? marks.width + Tokens.space2 : 0))
                    text: tile.alias.length > 0
                        ? tile.modelData.name + "<font color='" + Qt.alpha(Kirigami.Theme.textColor, Tokens.muted) + "'>（" + tile.alias + "）</font>"
                        : tile.modelData.name
                    textFormat: tile.alias.length > 0 ? Text.StyledText : Text.PlainText
                    font.pixelSize: Tokens.textBase
                    // 正在播放靠字重 + 强调色标记区分，标题本身不用强调色（Breeze 亮色下强调色文字对比度只有 2.5:1）
                    font.weight: tile.current ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                Row {
                    id: marks
                    x: title.width + Tokens.space2
                    anchors.verticalCenter: title.verticalCenter
                    spacing: Tokens.space1
                    Kirigami.Icon {
                        visible: tile.liked
                        anchors.verticalCenter: parent.verticalCenter
                        width: Tokens.iconSm
                        height: Tokens.iconSm
                        source: "emblem-favorite-symbolic"
                        color: Kirigami.Theme.negativeTextColor
                        isMask: true
                    }
                    Tag {
                        text: Format.restriction(tile.modelData.fee)
                        muted: tile.locked
                    }
                    Tag {
                        text: Format.quality(tile.modelData.quality || "")
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: tile.modelData.artist
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                elide: Text.ElideRight
            }
        }

        Label {
            Layout.preferredWidth: Tokens.albumColumn
            visible: tile.width >= Tokens.wideRow
            // 细节列：每日推荐里「为什么推荐」比专辑名更有用；单曲的专辑名常与曲名相同，重复就留空
            //（列仍占位，保证时长列对齐）
            text: tile.showReason && tile.modelData.reason ? tile.modelData.reason
                : tile.modelData.album === tile.modelData.name ? "" : tile.modelData.album
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            elide: Text.ElideRight
        }

        // 时长列；可移除的行（播放队列）在悬停/聚焦时把时长换成「移出」按钮，列宽不变、行不跳动
        Item {
            Layout.rightMargin: Tokens.space3
            Layout.preferredWidth: Math.max(durationLabel.implicitWidth, Tokens.height8)
            Layout.fillHeight: true
            readonly property bool showRemove: tile.removable && (tile.hovered || tile.visualFocus)

            Label {
                id: durationLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !parent.showRemove
                text: Format.duration(tile.modelData.dur)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: -Tokens.space2
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.showRemove
                icon.name: "list-remove-symbolic"
                text: "移出队列"
                onClicked: tile.removeRequested(tile.index)
            }
        }
    }
}

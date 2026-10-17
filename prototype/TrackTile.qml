// 曲目行：高 48 = 封面边长。静止时无底色（重复元素保持 Level 0），悬停才出现淡色调。
// ranked 时左侧是 32 宽的名次列；正在播放把名次换成强调色播放图标，标题加粗。
// 不排名时，正在播放用悬挂在对齐线外侧的强调色竖条 + 加粗标题标出。
// 列：名次 · 封面 · 标题（别名弱化括注 · 购买限制 · 音质 · 推荐理由）/ 歌手 · 专辑 · 喜欢 · 时长。
// 歌手、专辑是文字链接；喜欢列已喜欢时常驻实心红心，未喜欢时悬停才出现空心，点一下切换。时长未知时留空。
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
    property bool reordering // 歌单编辑顺序：行尾固定显示上移 / 下移
    property bool canMoveDown: true
    readonly property bool liked: Source.isLiked(tile.modelData.id)
    property bool showReason // 每日推荐：标题后多一个推荐理由标签（「超 76% 人收藏」）
    property bool showAlbum: true  // 专辑页里每行的专辑都相同：细节列留空（列仍占位，时长列对齐）
    readonly property string alias: tile.modelData.alias || ""
    property bool hangingMarker: true  // 左侧没有留白（紧挨别的元素）时关掉悬挂竖条
    property var menuContext: ({})     // 交给曲目菜单：{ playlistId } 时多一项「从歌单中移除」

    signal activated(int index)
    signal removeRequested(int index)
    signal moveRequested(int index, int delta)

    implicitHeight: Tokens.trackRow
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    onClicked: tile.activated(tile.index)
    Keys.onReturnPressed: tile.activated(tile.index)
    // 曲目菜单：右键，或键盘的菜单键 / Shift+F10
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            Actions.trackMenu(tile.modelData, tile.menuContext);
            event.accepted = true;
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: Actions.trackMenu(tile.modelData, tile.menuContext)
    }
    Accessible.name: tile.modelData.name + "，" + tile.modelData.artist

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

                // 标题（别名作弱化括注跟在后面，先于标题被省略）+ 标记：购买限制、音质、推荐理由。
                // 标题先省略，标记始终紧跟其后（推荐理由过长时自己省略）
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
                    Tag {
                        id: restrictionTag
                        text: Format.restriction(tile.modelData.fee)
                        muted: tile.locked
                    }
                    Tag {
                        id: qualityTag
                        text: Format.quality(tile.modelData.quality || "")
                    }
                    // 推荐理由说的是「为什么推给你」，不是曲目属性：用中性底，与强调色的属性标签分开。
                    // 它最先让位：只用标题完整显示后剩下的宽度，剩得太少就不显示，不把标题挤成一个字
                    Tag {
                        readonly property real room: titleRow.width - title.implicitWidth - Tokens.space2
                            - (restrictionTag.visible ? restrictionTag.width + Tokens.space1 : 0)
                            - (qualityTag.visible ? qualityTag.width + Tokens.space1 : 0)
                        text: tile.showReason && room >= Tokens.space6 * 2 ? tile.modelData.reason || "" : ""
                        muted: true
                        maximumWidth: Math.min(Tokens.albumColumn, room)
                    }
                }
            }
            LinkText {
                objectName: "artistLinks"
                Layout.fillWidth: true
                parts: Format.artistLinks(tile.modelData)
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        // 专辑列：单曲的专辑名常与曲名相同，重复就留空（列仍占位，保证右侧各列对齐）
        Item {
            Layout.preferredWidth: Tokens.albumColumn
            Layout.fillHeight: true
            visible: tile.width >= Tokens.wideRow
            LinkText {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, parent.width)
                parts: !tile.showAlbum || !tile.modelData.album || tile.modelData.album === tile.modelData.name ? []
                    : [{ text: tile.modelData.album, albumId: tile.modelData.albumId }]
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        // 喜欢列：高频操作，一点即切换（未登录时由 main.qml 引向登录）
        Item {
            Layout.preferredWidth: Tokens.height8
            Layout.fillHeight: true
            IconButton {
                objectName: "likeButton"
                anchors.centerIn: parent
                visible: tile.liked || tile.hovered || tile.visualFocus || hovered
                icon.name: tile.liked ? "emblem-favorite-symbolic" : "love-symbolic"
                icon.color: tile.liked ? Kirigami.Theme.negativeTextColor : Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                text: tile.liked ? "取消喜欢" : "喜欢"
                onClicked: Actions.toggleLike(tile.modelData)
            }
        }

        // 时长列；悬停/聚焦时把时长换成行内动作，列宽不变、行不跳动：
        // 播放队列里是「移出」，其余是「更多」（曲目菜单）
        // 右缘贴页边线（与左边的封面贴页边线对称，和页头、播放条的右缘同一条线）
        Item {
            id: endColumn
            Layout.preferredWidth: tile.reordering ? Tokens.height8 * 2 + Tokens.space1
                                                   : Math.max(durationLabel.implicitWidth, Tokens.height8)
            Layout.fillHeight: true
            readonly property bool showAction: (tile.hovered || tile.visualFocus) && !tile.reordering

            Label {
                id: durationLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: !endColumn.showAction
                text: Format.duration(tile.modelData.dur)
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: -Tokens.space2
                anchors.verticalCenter: parent.verticalCenter
                visible: tile.reordering
                spacing: 0
                IconButton {
                    enabled: tile.index > 0
                    icon.name: "go-up-symbolic"
                    text: "上移"
                    onClicked: tile.moveRequested(tile.index, -1)
                }
                IconButton {
                    enabled: tile.canMoveDown
                    icon.name: "go-down-symbolic"
                    text: "下移"
                    onClicked: tile.moveRequested(tile.index, 1)
                }
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: -Tokens.space2
                anchors.verticalCenter: parent.verticalCenter
                visible: endColumn.showAction
                icon.name: tile.removable ? "list-remove-symbolic" : "overflow-menu"
                text: tile.removable ? "移出队列" : "更多"
                onClicked: tile.removable ? tile.removeRequested(tile.index) : Actions.trackMenu(tile.modelData, tile.menuContext)
            }
        }
    }
}

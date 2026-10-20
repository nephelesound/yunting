// 一起听（不在房间时）：播放条上的按钮弹出的小面板。先是两个动作（开始 / 用链接加入），
// 下面是互相关注的好友在不在线、上次和你一起听是什么时候——这正是「找谁一起听」要看的。在房间里、在线的排前面。
// 点好友就是邀请：还没开房时由 main 先建房再把邀请发出去；房间已开时直接调用房间接口。
// 邀请成功后房间面板会打开，等待对方加入。
// 浮在播放条上的真浮层：有投影（与右键菜单同一层级）
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Popup {
    id: popup

    property bool active      // 有歌在放才能开房间
    property bool busy        // 正在建房间 / 加入

    signal startRequested()
    signal joinRequested()
    signal friendInviteRequested(double userId, string name)

    // 好友多时只露几行，其余在面板里滚动
    readonly property int shownRows: 6

    width: Tokens.trackColumn + 2 * Tokens.space3
    padding: Tokens.space2
    margins: Tokens.space2      // 挂在窄窗口右缘的「更多」上时，整块推回窗口里
    onAboutToShow: Source.refreshFriendsListening()

    background: Kirigami.ShadowedRectangle {
        color: Kirigami.Theme.backgroundColor
        radius: Tokens.radiusLg
        border.width: Tokens.line
        border.color: Qt.alpha(Kirigami.Theme.textColor, Tokens.tonal)
        shadow.size: Tokens.space4
        shadow.yOffset: Tokens.space1
        shadow.color: Qt.alpha("black", Tokens.track)
    }

    // lastTime 是上次和你一起听的时间（与 lastListen 成对），不是好友最近一次听歌
    function status(f) {
        if (f.inRoom)
            return "正在一起听";
        if (f.online)
            return f.description || "在线";
        if (f.lastListen && f.lastTime > 0)
            return "上次一起听 · " + Format.ago(f.lastTime);
        return f.description;
    }

    contentItem: ColumnLayout {
        spacing: 0

        ItemDelegate {
            Layout.fillWidth: true
            enabled: popup.active && !popup.busy
            icon.name: "group-symbolic"
            text: "开始一起听"
            onClicked: {
                popup.close();
                popup.startRequested();
            }
        }
        ItemDelegate {
            Layout.fillWidth: true
            enabled: !popup.busy
            icon.name: "insert-link-symbolic"
            text: "用链接加入…"
            onClicked: {
                popup.close();
                popup.joinRequested();
            }
        }

        Label {
            visible: Source.friendsListening.length > 0
            Layout.fillWidth: true
            Layout.topMargin: Tokens.space3
            Layout.bottomMargin: Tokens.space1
            Layout.leftMargin: Tokens.space2
            text: "好友"
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        }
        ListView {
            id: friends
            objectName: "listenFriends"
            visible: count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count, popup.shownRows) * Tokens.trackRow
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: Source.friendsListening
            ScrollBar.vertical: AppScrollBar {}
            delegate: PersonRow {
                required property var modelData
                width: ListView.view.width
                avatar: modelData.avatar || ""
                pendant: modelData.avatarPendant || ""
                vipLevel: modelData.vipLevel || ""
                name: modelData.name || ""
                detail: popup.status(modelData)
                onClicked: {
                    popup.close();
                    popup.friendInviteRequested(modelData.userId, modelData.name || "");
                }
            }
        }
    }
}

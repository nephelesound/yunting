// 播放队列：从右侧滑出的浮层（Kirigami 原生抽屉，自带遮罩与投影。它真的浮在页面之上，所以投影是对的）。
// 只回答「接下来放什么」：页头（播放队列 · N 首 · 智能 · 清空）→ 正在播放 → 接下来播放。
// 点一行跳到那首；悬停一行可移出队列；拖行首的把手（或聚焦后 Alt+↑/↓）调整顺序。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Kirigami.OverlayDrawer {
    id: panel

    property var queue: []
    property int currentIndex: -1
    property bool vip
    property bool smartEnabled: false
    property var room: null   // 一起听（C++ ListenTogether 或原型替身）；未登录时为 null
    property bool confirmingEnd: false   // 房主点了「结束」，在原地确认；关上抽屉就算取消
    onClosed: panel.confirmingEnd = false
    onOpened: if (panel.room) Source.refreshFriendsListening()

    signal activated(int index)
    signal listenEndRequested()
    signal listenLinkCopied()
    signal listenInviteRequested(real userId, string name)
    signal nowPlayingRequested()
    signal smartToggled(bool enabled)
    signal removeRequested(int index)
    signal clearRequested()
    signal moveRequested(int from, int to)   // 队列里的绝对位置

    readonly property var current: panel.currentIndex >= 0 && panel.currentIndex < panel.queue.length ? panel.queue[panel.currentIndex] : null
    readonly property var upcoming: panel.queue.slice(panel.currentIndex + 1)

    // 拖动时只在这份顺序表里挪（每项记着它在 upcoming 里的原位置 src），松手才提交给 Playback：
    // 若拖动中直接改队列，列表会整个重建、拖动随之中断
    ListModel { id: order }
    property var synced: []   // 上次同步时的 upcoming：order 里的 src 指向它

    // 队列变了：按差异更新 order，不清空重建。移出一首、切到下一首、加一首都只动那一行（有过渡），
    // 其余行原地不动；拖动提交后顺序已经一致，只改 src。认不出的变化（换了整个队列）才整表重建
    function sync() {
        const next = panel.upcoming.map(t => t.id);
        const shown = [];
        for (let i = 0; i < order.count; i++)
            shown.push((panel.synced[order.get(i).src] || {}).id);
        const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
        const without = (a, k) => a.slice(0, k).concat(a.slice(k + 1));
        let k = 0;
        while (k < shown.length && k < next.length && shown[k] === next[k])
            k++;
        if (same(shown, next)) {
            // 顺序没变
        } else if (shown.length === next.length + 1 && same(without(shown, k), next)) {
            // 移出的那一行在淡出期间仍显示原来的歌，不跟着 upcoming 变成下一首
            const leaving = list.itemAtIndex(k);
            if (leaving)
                leaving.frozen = panel.synced[order.get(k).src];
            order.remove(k);
        } else if (next.length === shown.length + 1 && same(without(next, k), shown)) {
            order.insert(k, { src: k });
        } else {
            order.clear();
            for (let i = 0; i < next.length; i++)
                order.append({ src: i });
        }
        for (let i = 0; i < order.count; i++)
            if (order.get(i).src !== i)
                order.setProperty(i, "src", i);
        panel.synced = panel.upcoming;
    }
    onUpcomingChanged: panel.sync()
    Component.onCompleted: panel.sync()
    // keepFocus：键盘挪动后队列更新，焦点要交给挪到新位置的那一行，才能连续按 Alt+↑/↓
    function commitMove(src, to, keepFocus) {
        if (src !== to)
            panel.moveRequested(panel.currentIndex + 1 + src, panel.currentIndex + 1 + to);
        else
            panel.sync();
        if (keepFocus)
            Qt.callLater(() => {
                const row = list.itemAtIndex(to);
                if (row)
                    row.focusTile();
            });
    }

    edge: Qt.RightEdge
    modal: true
    handleVisible: false
    handle.visible: false   // 抽屉自带的拉手按钮；队列只从播放条的按钮打开
    width: Tokens.measureMax
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    function locked(track) {
        return Format.blockedBy(track, panel.vip) !== "";
    }
    // 一起听时房间里另一位放不了的歌（版权、会员）：标出来，放到它时对方那边会卡住
    function roomNote(track) {
        const ids = panel.room && panel.room.inRoom ? panel.room.unplayableIds || [] : [];
        return track && ids.indexOf(track.id) >= 0 ? "对方不能播放" : "";
    }

    component SectionLabel: Label {
        Layout.leftMargin: Tokens.pageMargin
        Layout.preferredHeight: Tokens.height8
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }

    contentItem: ColumnLayout {
        spacing: 0

        // 页头：与页面页头同高，标题 24 细体
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Tokens.space4
            Layout.leftMargin: Tokens.pageMargin
            Layout.rightMargin: Tokens.pageMargin - Tokens.space2
            Layout.preferredHeight: Tokens.height12
            spacing: Tokens.space3

            Label {
                text: "播放队列"
                font.pixelSize: Tokens.text2xl
                font.weight: Font.Light
            }
            Label {
                Layout.alignment: Qt.AlignBaseline
                visible: panel.queue.length > 0
                text: panel.queue.length + " 首"
                font.pixelSize: Tokens.textSm
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            // 智能续播：一直开着或关着的设置，用系统的开关（Switch）；不用勾选的按钮底，
            // 勾选底由用户的控件样式画，换台机器样子就不一样。只在本机算口味
            Switch {
                checked: panel.smartEnabled
                text: "智能续播"
                font.pixelSize: Tokens.textSm
                onToggled: panel.smartToggled(checked)
                ToolTip.text: panel.smartEnabled
                    ? "智能续播已开启：按本机播放/跳过/红心记录补充队列"
                    : "开启智能续播：按本机口味补充队列，不上传数据"
                ToolTip.visible: hovered
                ToolTip.delay: 600
            }
            Item { Layout.fillWidth: true }
            Button {
                implicitHeight: Tokens.height8
                flat: true
                enabled: panel.upcoming.length > 0
                icon.name: "edit-clear-history-symbolic"
                icon.width: Tokens.iconSm
                icon.height: Tokens.iconSm
                text: "清空"
                font.pixelSize: Tokens.textSm
                onClicked: panel.clearRequested()
            }
        }

        // 一起听：谁在房间里；还没人来时怎么邀请（扫码或发链接）；能做什么；怎么离开
        ColumnLayout {
            id: listenSection
            visible: panel.room !== null && panel.room.inRoom
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.pageMargin
            Layout.rightMargin: Tokens.pageMargin
            Layout.topMargin: Tokens.space2
            spacing: Tokens.space3
            readonly property var users: panel.room ? panel.room.users || [] : []
            readonly property bool waiting: users.length < 2

            Connections {
                target: panel.room
                ignoreUnknownSignals: true
                function onLeftRoom() { panel.confirmingEnd = false; }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.space3
                Repeater {
                    model: listenSection.users
                    delegate: RowLayout {
                        id: member
                        required property var modelData
                        required property int index
                        spacing: Tokens.space2
                        PersonAvatar {
                            implicitWidth: Tokens.height8
                            implicitHeight: Tokens.height8
                            name: member.modelData.name || ""
                            source: member.modelData.avatar || ""
                            pendant: member.modelData.avatarPendant || ""
                        }
                        Label {
                            Layout.maximumWidth: Tokens.albumColumn - Tokens.height8
                            text: member.modelData.name || ""
                            font.pixelSize: Tokens.textBase
                            elide: Text.ElideRight
                        }
                        VipTag { level: member.modelData.vipLevel || "" }
                    }
                }
                // 第二个位置空着：等好友来
                Label {
                    visible: listenSection.waiting
                    text: "等好友加入…"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
                Item { Layout.fillWidth: true }
                Button {
                    objectName: "listenEnd"
                    implicitHeight: Tokens.height8
                    flat: true
                    // 确认出现时让位给下面那一格的「结束一起听」，同一处不放两个结束
                    opacity: panel.confirmingEnd ? 0 : 1
                    Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                    enabled: !panel.confirmingEnd && (!panel.room || !panel.room.busy)
                    // 房主走了房间就散了，好友在时先在原地问一句；还没人来、或好友自己退出，直接走
                    text: panel.room && panel.room.host ? "结束" : "退出"
                    font.pixelSize: Tokens.textSm
                    onClicked: {
                        if (panel.room.host && !listenSection.waiting)
                            panel.confirmingEnd = true;
                        else
                            panel.listenEndRequested();
                    }
                }
            }
            // 你们俩一起听过多少：好友来了、取到了才有这一行
            Label {
                readonly property var stats: panel.room ? panel.room.statistics || ({}) : ({})
                visible: !listenSection.waiting && text.length > 0
                Layout.fillWidth: true
                text: stats.listenCount > 0
                    ? "一起听过 " + stats.listenCount + " 次" + (stats.totalSeconds > 0 ? "，共 " + Format.spoken(stats.totalSeconds) : "")
                    : ""
                font.pixelSize: Tokens.textSm
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }

            // 邀请：网易云音乐 App 扫码即可加入；也可以把链接发给好友
            RowLayout {
                visible: listenSection.waiting
                Layout.fillWidth: true
                spacing: Tokens.space4
                QrCode {
                    Layout.alignment: Qt.AlignTop
                    maxSide: Tokens.tileLg
                    content: panel.room ? panel.room.shareUrl : ""
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Tokens.space2
                    Label {
                        Layout.fillWidth: true
                        text: "用网易云音乐 App 扫码，或把链接发给好友"
                        font.pixelSize: Tokens.textSm
                        wrapMode: Text.Wrap
                    }
                    Button {
                        implicitHeight: Tokens.height8
                        icon.name: "edit-copy-symbolic"
                        icon.width: Tokens.iconSm
                        icon.height: Tokens.iconSm
                        text: "复制邀请链接"
                        font.pixelSize: Tokens.textSm
                        onClicked: {
                            Clipboard.copy(panel.room.shareUrl);
                            panel.listenLinkCopied();
                        }
                    }
                }
            }

            // 互关好友一键点名邀请：二维码/链接仍保留，给不在好友列表里的人
            ColumnLayout {
                id: inviteFriends
                readonly property var candidates: (Source.friendsListening || []).filter(
                    f => !listenSection.users.some(u => u.userId === f.userId))
                visible: listenSection.waiting && candidates.length > 0
                Layout.fillWidth: true
                spacing: Tokens.space1
                Label {
                    Layout.fillWidth: true
                    text: "邀请好友"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
                Repeater {
                    model: inviteFriends.candidates.slice(0, 4)
                    delegate: PersonRow {
                        required property var modelData
                        Layout.fillWidth: true
                        width: parent.width
                        avatar: modelData.avatar || ""
                        pendant: modelData.avatarPendant || ""
                        name: modelData.name || ""
                        detail: modelData.online ? (modelData.description || "在线")
                            : modelData.lastTime > 0 ? "上次一起听 · " + Format.ago(modelData.lastTime) : ""
                        onClicked: panel.listenInviteRequested(modelData.userId, modelData.name || "")
                    }
                }
            }

            // 说明与「结束」的确认共用这一格，原地淡入淡出；格高取两者较高的，下面的队列不跳
            Item {
                objectName: "listenHintSlot"
                Layout.fillWidth: true
                implicitHeight: Math.max(roomHint.implicitHeight, endConfirm.implicitHeight)

                Label {
                    id: roomHint
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: panel.confirmingEnd ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                    text: panel.room && panel.room.host ? "你放的歌会同步给好友，两人都能切歌和暂停"
                        : "歌单由房主决定，两人都能切歌和暂停"
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    id: endConfirm
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Tokens.space2
                    opacity: panel.confirmingEnd ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut } }
                    Label {
                        Layout.fillWidth: true
                        text: "好友那边也会停止播放"
                        font.pixelSize: Tokens.textSm
                        wrapMode: Text.Wrap
                    }
                    Button {
                        objectName: "listenEndConfirm"
                        implicitHeight: Tokens.height8
                        text: "结束一起听"
                        font.pixelSize: Tokens.textSm
                        enabled: panel.confirmingEnd && (!panel.room || !panel.room.busy)
                        onClicked: {
                            panel.confirmingEnd = false;
                            panel.listenEndRequested();
                        }
                    }
                    Button {
                        objectName: "listenEndCancel"
                        implicitHeight: Tokens.height8
                        flat: true
                        text: "取消"
                        font.pixelSize: Tokens.textSm
                        enabled: panel.confirmingEnd
                        onClicked: panel.confirmingEnd = false
                    }
                }
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                visible: text.length > 0
                type: Kirigami.MessageType.Error
                text: panel.room ? panel.room.error || "" : ""
            }
        }

        // 正在播放
        SectionLabel {
            Layout.topMargin: Tokens.space4
            visible: panel.current !== null
            text: "正在播放"
        }
        TrackTile {
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.pageMargin
            Layout.rightMargin: Tokens.pageMargin
            visible: panel.current !== null
            index: panel.currentIndex
            modelData: panel.current || { name: "", artist: "", album: "", dur: 0, fee: 0, cover: "" }
            current: true
            showReason: !!modelData.smartReason
            note: panel.roomNote(panel.current)
            // 点正在播放的这一首：不重头放，去正在播放页（与点播放条封面同一个去处）
            onActivated: panel.nowPlayingRequested()
        }

        // 接下来播放
        SectionLabel {
            Layout.topMargin: Tokens.space4
            visible: panel.upcoming.length > 0
            text: "接下来播放"
        }
        ListView {
            id: list
            objectName: "queueList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftMargin: Tokens.pageMargin
            rightMargin: Tokens.pageMargin
            bottomMargin: Tokens.space6
            spacing: Tokens.space2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: order
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: list }
            // 过渡：拖动换位快（跟手）；移出 / 加入的那一行淡出 / 淡入，其余行平移补位，不跳。
            // 补位动画里把 opacity 也拉回 1：被打断的淡入不会把行留在半透明
            moveDisplaced: Transition {
                NumberAnimation { property: "y"; duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
            }
            remove: Transition {
                NumberAnimation { property: "opacity"; to: 0; duration: Tokens.motionFast; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
            }
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
            }
            displaced: Transition {
                NumberAnimation { property: "y"; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
                NumberAnimation { property: "opacity"; to: 1; duration: Tokens.motionBase }
            }

            delegate: Item {
                id: row
                required property int index
                required property int src
                property var frozen: null   // 正在淡出的行：定格在移出前的那首（见 sync）
                readonly property var track: row.frozen || panel.upcoming[row.src] || { name: "", artist: "", album: "", dur: 0, fee: 0, cover: "" }
                width: list.width - list.leftMargin - list.rightMargin
                implicitHeight: tile.implicitHeight
                function focusTile() { tile.forceActiveFocus(); }

                // 把手悬挂在页边线外侧（与「正在播放」竖条同一处），曲目行本身仍从页边线开始
                Kirigami.ListItemDragHandle {
                    anchors.right: tile.left
                    anchors.rightMargin: Tokens.space1
                    anchors.verticalCenter: tile.verticalCenter
                    implicitWidth: Tokens.iconSm
                    implicitHeight: Tokens.iconSm
                    listItem: row
                    listView: list
                    onMoveRequested: (oldIndex, newIndex) => order.move(oldIndex, newIndex, 1)
                    // 把手很小：悬停说明它能做什么，以及键盘的等价操作
                    HoverHandler { id: handleHover }
                    ToolTip.visible: handleHover.hovered
                    ToolTip.delay: 600
                    ToolTip.text: "拖动调整顺序（也可聚焦后按 Alt+↑/↓）"
                    onDropped: panel.commitMove(row.src, row.index)
                }
                TrackTile {
                    id: tile
                    width: parent.width
                    index: row.index
                    modelData: row.track
                    showReason: !!row.track.smartReason
                    removable: true
                    hangingMarker: false
                    locked: panel.locked(row.track)
                    note: panel.roomNote(row.track)
                    // 列表里的序号是相对「接下来」的，换算回队列里的位置
                    onActivated: i => panel.activated(panel.currentIndex + 1 + i)
                    onRemoveRequested: i => panel.removeRequested(panel.currentIndex + 1 + i)
                    // 键盘：Alt+↑ / Alt+↓ 上下挪一位
                    Keys.onUpPressed: event => {
                        if ((event.modifiers & Qt.AltModifier) && row.index > 0)
                            panel.commitMove(row.src, row.index - 1, true);
                        else
                            event.accepted = false;
                    }
                    Keys.onDownPressed: event => {
                        if ((event.modifiers & Qt.AltModifier) && row.index < order.count - 1)
                            panel.commitMove(row.src, row.index + 1, true);
                        else
                            event.accepted = false;
                    }
                }
            }

            Kirigami.PlaceholderMessage {
                anchors.centerIn: parent
                width: parent.width - 2 * Tokens.pageMargin
                visible: panel.upcoming.length === 0
                icon.name: "view-media-playlist"
                text: panel.current ? "这是队列里最后一首" : "队列是空的"
                explanation: "在任意列表里点一首歌，它所在的列表会成为新的队列"
            }
        }
    }
}

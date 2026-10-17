// 播放队列：从右侧滑出的浮层（Kirigami 原生抽屉，自带遮罩与投影。它真的浮在页面之上，所以投影是对的）。
// 只回答「接下来放什么」：页头（播放队列 · N 首 · 清空）→ 正在播放 → 接下来播放。
// 点一行跳到那首；悬停一行可移出队列；拖行首的把手（或聚焦后 Alt+↑/↓）调整顺序。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.OverlayDrawer {
    id: panel

    property var queue: []
    property int currentIndex: -1
    property bool vip

    signal activated(int index)
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
        return track.fee === 4 || (!panel.vip && track.fee === 1);
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
                    removable: true
                    hangingMarker: false
                    locked: panel.locked(row.track)
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

// 曲目菜单：右键曲目行，或点行尾的「更多」。一个实例由 main.qml 持有，Actions.trackMenu 打开。
// 分三组：放（下一首播放 / 加入队列）· 收（红心 / 加到歌单 / 从歌单移除）· 去（歌手 / 专辑 / 复制链接）。
// 菜单是真正浮起的浮层，阴影由系统样式给出。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "Format.js" as Format

Menu {
    id: trackMenu

    required property var player    // Playback
    property bool loggedIn
    property var track: null
    property var context: ({})

    signal loginRequested()
    signal newPlaylistRequested(var track)
    signal addRequested(var playlist, var track)
    signal removeRequested(real playlistId, var track)
    signal copied()
    signal shareRequested(var track)

    function show(track, context) {
        trackMenu.track = track;
        trackMenu.context = context;
        trackMenu.popup();
    }

    readonly property bool liked: trackMenu.track !== null && Source.isLiked(trackMenu.track.id)

    // 菜单宽度随最长一项走：超长的歌手名、专辑名截短，不让菜单横跨半个窗口
    function shorten(text) { return text.length > 24 ? text.slice(0, 23) + "…" : text; }
    function indexOfItem(item) {
        for (let i = 0; i < trackMenu.count; ++i)
            if (trackMenu.itemAt(i) === item)
                return i;
        return trackMenu.count;
    }

    MenuItem {
        icon.name: "media-playlist-play"
        text: "下一首播放"
        onTriggered: trackMenu.player.playNext(trackMenu.track)
    }
    MenuItem {
        icon.name: "media-playlist-append"
        text: "加入播放队列"
        onTriggered: trackMenu.player.enqueue(trackMenu.track)
    }

    MenuSeparator {}

    MenuItem {
        icon.name: trackMenu.liked ? "emblem-favorite-symbolic" : "love"
        text: trackMenu.liked ? "取消喜欢" : "喜欢"
        onTriggered: Actions.toggleLike(trackMenu.track)
    }
    Menu {
        id: addTo
        title: "添加到歌单"
        icon.name: "list-add"

        // 未登录时与「喜欢」一样引向登录，而不是灰掉
        MenuItem {
            visible: !trackMenu.loggedIn
            height: visible ? implicitHeight : 0
            icon.name: "view-barcode-qr"
            text: "登录后可添加到歌单…"
            onTriggered: trackMenu.loginRequested()
        }
        Instantiator {
            model: trackMenu.loggedIn ? Source.addableLists : []
            delegate: MenuItem {
                required property var modelData
                text: trackMenu.shorten(modelData.name)
                onTriggered: trackMenu.addRequested(modelData, trackMenu.track)
            }
            onObjectAdded: (index, object) => addTo.insertItem(index, object)
            onObjectRemoved: (index, object) => addTo.removeItem(object)
        }
        MenuSeparator { visible: trackMenu.loggedIn && Source.addableLists.length > 0 }
        MenuItem {
            visible: trackMenu.loggedIn
            height: visible ? implicitHeight : 0
            icon.name: "document-new"
            text: "新建歌单…"
            onTriggered: trackMenu.newPlaylistRequested(trackMenu.track)
        }
    }
    MenuItem {
        visible: (trackMenu.context.playlistId || 0) > 0
        height: visible ? implicitHeight : 0
        icon.name: "list-remove"
        text: "从这个歌单中移除"
        onTriggered: trackMenu.removeRequested(trackMenu.context.playlistId, trackMenu.track)
    }

    MenuSeparator {}

    // 每位歌手一项，按署名顺序排在专辑前面（没有 id 的歌手打不开，不列）
    Instantiator {
        model: Format.artistLinks(trackMenu.track).filter(a => a.artistId > 0)
        delegate: MenuItem {
            required property var modelData
            icon.name: "view-media-artist"
            text: "歌手：" + trackMenu.shorten(modelData.text)
            onTriggered: Actions.openArtist(modelData.artistId)
        }
        onObjectAdded: (index, object) => trackMenu.insertItem(trackMenu.indexOfItem(albumItem), object)
        onObjectRemoved: (index, object) => trackMenu.removeItem(object)
    }
    MenuItem {
        id: albumItem
        enabled: trackMenu.track !== null && (trackMenu.track.albumId || 0) > 0
        icon.name: "view-media-album-cover"
        text: trackMenu.track && trackMenu.track.album ? "专辑：" + trackMenu.shorten(trackMenu.track.album) : "专辑"
        onTriggered: Actions.openAlbum(trackMenu.track.albumId)
    }
    MenuItem {
        icon.name: "document-share"
        text: "分享…"
        onTriggered: trackMenu.shareRequested(trackMenu.track)
    }
    MenuItem {
        icon.name: "edit-copy"
        text: "复制歌曲链接"
        onTriggered: {
            Clipboard.copy(Source.shareUrl("song", trackMenu.track.id));
            trackMenu.copied();
        }
    }
}

// 请求总线：深处的组件（曲目行、搜索磁贴、专辑页的歌手名）只说「想做什么」，由 main.qml 统一执行。
// 这样导航与曲目菜单不必经过每一层页面逐级转发。
pragma Singleton

import QtQuick

QtObject {
    // context：{ playlistId }。曲目在自己创建的歌单里时，菜单多一项「从歌单中移除」
    signal trackMenuRequested(var track, var context)
    signal artistRequested(real id)
    signal albumRequested(real id)
    signal mvRequested(real id)
    signal userRequested(real id)
    signal playlistRequested(real id)
    signal logoutRequested()
    signal playlistMenuRequested(var playlist)   // 「我的」里的歌单磁贴：{ id, name, owned }
    signal newPlaylistRequested()
    signal likeToggleRequested(var track)        // 曲目行、播放条、曲目菜单的红心都走这一处（未登录时引向登录）
    signal playRequested(var tracks, int index)  // 不是曲目列表页的地方要放歌：听歌排行、私信里分享的歌
    signal playNowRequested(var track)           // 插到「下一首」并立刻放，队列不动：相似歌曲、外部链接

    function play(tracks, index) { if (index >= 0 && index < tracks.length) playRequested(tracks, index); }

    function trackMenu(track, context) { trackMenuRequested(track, context || {}); }
    function toggleLike(track) { if (track) likeToggleRequested(track); }
    function openArtist(id) { if (id > 0) artistRequested(id); }
    function openAlbum(id) { if (id > 0) albumRequested(id); }
    function openMv(id) { if (id > 0) mvRequested(id); }
    function openUser(id) { if (id > 0) userRequested(id); }
    function openPlaylist(id) { if (id > 0) playlistRequested(id); }
    signal radioRequested(real id)
    function openRadio(id) { if (id > 0) radioRequested(id); }
    // 动态、通知里提到的东西（{ kind, id, name, cover, artist, track }）：歌直接放，其余打开对应的页；
    // 视频还没有页可开。只有 id 的歌补成最小的曲目（播放地址与时长由播放器按 id 取）
    function canOpen(resource) {
        return Boolean(resource) && (resource.track ? true : resource.id > 0
            && ["song", "playlist", "album", "mv", "artist"].indexOf(resource.kind) >= 0);
    }
    function openResource(resource) {
        if (!canOpen(resource))
            return;
        if (resource.track)
            play([resource.track], 0);
        else if (resource.kind === "song")
            play([{ id: resource.id, name: resource.name || "", artist: resource.artist || "", album: "", albumId: 0,
                    artistId: 0, artists: [{ id: 0, name: resource.artist || "" }], dur: 0, fee: 0,
                    cover: resource.cover || "", alias: "", quality: "", reason: "", mv: 0, playable: true }], 0);
        else if (resource.kind === "playlist")
            openPlaylist(resource.id);
        else if (resource.kind === "album")
            openAlbum(resource.id);
        else if (resource.kind === "mv")
            openMv(resource.id);
        else if (resource.kind === "artist")
            openArtist(resource.id);
    }

    signal inboxRequested()
    function openInbox() { inboxRequested(); }
    signal aboutRequested()
    function openAbout() { aboutRequested(); }
}

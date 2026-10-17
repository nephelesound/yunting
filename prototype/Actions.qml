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

    function play(tracks, index) { if (index >= 0 && index < tracks.length) playRequested(tracks, index); }

    function trackMenu(track, context) { trackMenuRequested(track, context || {}); }
    function toggleLike(track) { if (track) likeToggleRequested(track); }
    function openArtist(id) { if (id > 0) artistRequested(id); }
    function openAlbum(id) { if (id > 0) albumRequested(id); }
    function openMv(id) { if (id > 0) mvRequested(id); }
    function openUser(id) { if (id > 0) userRequested(id); }
    function openPlaylist(id) { if (id > 0) playlistRequested(id); }
    signal inboxRequested()
    function openInbox() { inboxRequested(); }
    signal aboutRequested()
    function openAbout() { aboutRequested(); }
}

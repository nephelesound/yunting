// 请求总线：深处的组件（曲目行、搜索磁贴、专辑页的歌手名）只说「想做什么」，由 main.qml 统一执行。
// 这样导航与曲目菜单不必经过每一层页面逐级转发。
pragma Singleton

import QtQuick

QtObject {
    // context：{ playlistId }——曲目在自己创建的歌单里时，菜单多一项「从歌单中移除」
    signal trackMenuRequested(var track, var context)
    signal artistRequested(real id)
    signal albumRequested(real id)
    signal playlistRequested(real id)
    signal logoutRequested()

    function trackMenu(track, context) { trackMenuRequested(track, context || {}); }
    function openArtist(id) { if (id > 0) artistRequested(id); }
    function openAlbum(id) { if (id > 0) albumRequested(id); }
    function openPlaylist(id) { if (id > 0) playlistRequested(id); }
}

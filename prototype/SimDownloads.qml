// 下载的原型实现：点「下载」立即下好。截图 harness 用 filled 让热歌榜前几首算作已下载。
// 接口与 C++ DownloadManager 相同：downloads · directory · downloadTrack(track, level) · cancel(id) · remove(id) ·
// progress · finished · failed
import QtQuick

QtObject {
    id: manager

    property bool filled: false
    readonly property string directory: "/home/jo/.local/share/yunting/downloads"
    property var added: []
    readonly property var downloads: (manager.filled ? Source.hotChart.tracks.slice(0, 6) : []).map(t => ({
            id: t.id, name: t.name, artist: t.artist, album: t.album, cover: t.cover,
            size: 9.6e6 + (t.id % 7) * 3.1e6, level: "lossless", type: "flac" }))
        .concat(manager.added)

    signal progress(real songId, real received, real total)
    signal finished(real songId, string path)
    signal failed(real songId, string message)

    function downloadTrack(track, level) {
        manager.added = manager.added.concat([{ id: track.id, name: track.name, artist: track.artist, album: track.album,
                                                cover: track.cover, size: 8e6, level: level, type: "flac" }]);
        manager.finished(track.id, manager.directory + "/" + track.id + ".flac");
    }
    function cancel(songId) {}
    function remove(songId) { manager.added = manager.added.filter(d => d.id !== songId); }
}

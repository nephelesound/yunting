// 关掉再打开，回到原来的样子：队列、当前曲与位置、音量、播放模式、窗口大小、所在的顶层页。
// 用 Qt 标准的 QtCore.Settings 持久化（配置文件），不经 C++。
// 只在应用里启用（main.qml 用 Loader 按 backend 是否存在决定），原型与截图不读也不写。
// 启动时不自动播放：恢复到暂停在上次的位置，按空格继续。
import QtQuick
import QtCore

Item {
    id: persisted

    required property var window
    required property var player      // Playback

    readonly property int maxQueue: 500   // 队列存成 JSON；太长的队列只留前 500 首，配置文件不至于膨胀
    readonly property var pivots: ["home", "mine", "search"]

    Settings {
        id: saved
        category: "session"
        property string queue: "[]"
        property int index: -1
        property string origin: ""
        property real position: 0
        property real volume: 0.7
        property bool muted: false
        property int mode: 0
        property string page: "home"
        property int width: 0
        property int height: 0
        property bool maximized: false
    }

    property bool restored: false

    Component.onCompleted: {
        const engine = persisted.player.engine;
        let queue = [];
        try {
            queue = JSON.parse(saved.queue);
        } catch (e) {
            queue = [];
        }
        if (queue.length > 0 && saved.index >= 0) {
            persisted.player.restore(queue, saved.index);
            persisted.player.origin = saved.origin;
            engine.playing = false;
            engine.seek(saved.position);
        }
        engine.volume = saved.volume;
        engine.muted = saved.muted;
        persisted.player.mode = saved.mode;
        if (persisted.pivots.includes(saved.page))
            persisted.window.switchPivot(saved.page);
        if (saved.width > 0 && saved.height > 0) {
            persisted.window.width = saved.width;
            persisted.window.height = saved.height;
        }
        if (saved.maximized)
            persisted.window.showMaximized();
        persisted.restored = true;
    }

    // 恢复完成后才开始回写，避免启动途中的默认值把存档覆盖掉
    Binding { when: persisted.restored; target: saved; property: "queue"; value: JSON.stringify(persisted.player.queue.slice(0, persisted.maxQueue)) }
    Binding { when: persisted.restored; target: saved; property: "index"; value: persisted.player.index }
    Binding { when: persisted.restored; target: saved; property: "origin"; value: persisted.player.origin }
    Binding { when: persisted.restored; target: saved; property: "position"; value: Math.floor(persisted.player.engine.position) }
    Binding { when: persisted.restored; target: saved; property: "volume"; value: persisted.player.engine.volume }
    Binding { when: persisted.restored; target: saved; property: "muted"; value: persisted.player.engine.muted }
    Binding { when: persisted.restored; target: saved; property: "mode"; value: persisted.player.mode }
    Binding { when: persisted.restored && persisted.pivots.includes(persisted.window.page); target: saved; property: "page"; value: persisted.window.page }
    Binding { when: persisted.restored; target: saved; property: "maximized"; value: persisted.window.visibility === Window.Maximized }
    // 最大化时记住的是还原后的大小
    Binding { when: persisted.restored && persisted.window.visibility === Window.Windowed; target: saved; property: "width"; value: persisted.window.width }
    Binding { when: persisted.restored && persisted.window.visibility === Window.Windowed; target: saved; property: "height"; value: persisted.window.height }
}

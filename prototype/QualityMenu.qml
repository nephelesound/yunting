// 播放音质的单选菜单：播放条的音质按钮与窄窗口下「更多」里的子菜单共用。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "Format.js" as Format

Menu {
    id: qualityMenu

    property string quality
    property var audio
    signal qualityRequested(string quality)
    signal equalizerRequested()

    // 原型里的 SimEngine 没有输出设备 / EQ：鸭子类型判断，缺了就不显示
    readonly property bool hasAudio: audio !== null && audio !== undefined
        && typeof audio.audioOutputs === "function"
    readonly property var outputs: hasAudio ? audio.audioOutputs() : []
    readonly property bool eqAvailable: audio !== null && audio !== undefined && audio.eqAvailable === true
    readonly property string device: audio !== null && audio !== undefined && audio.outputDevice !== undefined
        ? audio.outputDevice : ""
    readonly property string scrobbleMode: audio !== null && audio !== undefined && audio.scrobbleMode !== undefined
        ? audio.scrobbleMode : ""

    title: "音质：" + Format.qualityLevel(qualityMenu.quality)

    Instantiator {
        model: ["auto", "standard", "higher", "exhigh", "lossless", "hires"]
        delegate: MenuItem {
            required property string modelData
            text: Format.qualityLevel(modelData) + (modelData === "auto" ? "（按曲目最高可用）" : "")
            checkable: true
            checked: qualityMenu.quality === modelData
            onTriggered: qualityMenu.qualityRequested(modelData)
        }
        onObjectAdded: (index, object) => qualityMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => qualityMenu.removeItem(object)
    }

    MenuSeparator {}

    // 输出设备：空串是系统默认。设备列表在打开菜单时取一次即可
    Menu {
        id: outputMenu
        title: "输出设备"
        enabled: qualityMenu.hasAudio
        Instantiator {
            model: qualityMenu.outputs
            delegate: MenuItem {
                required property var modelData
                text: modelData.description || modelData.id
                checkable: true
                checked: qualityMenu.device === modelData.id
                onTriggered: if (qualityMenu.hasAudio) qualityMenu.audio.setOutputDevice(modelData.id)
            }
            onObjectAdded: (index, object) => outputMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => outputMenu.removeItem(object)
        }
        MenuItem {
            text: "系统默认"
            checkable: true
            checked: qualityMenu.device === ""
            onTriggered: if (qualityMenu.hasAudio) qualityMenu.audio.setOutputDevice("")
        }
    }

    // 听歌记录怎样报给网易云（计入听歌排行与累计听歌）：网页版的接口，或官方电脑客户端的格式
    Menu {
        title: "听歌记录"
        enabled: qualityMenu.scrobbleMode !== ""
        MenuItem {
            text: "按网页版的方式上报"
            checkable: true
            checked: qualityMenu.scrobbleMode === "legacy"
            onTriggered: qualityMenu.audio.scrobbleMode = "legacy"
        }
        MenuItem {
            text: "按官方电脑客户端的方式上报"
            checkable: true
            checked: qualityMenu.scrobbleMode === "ncbl"
            onTriggered: qualityMenu.audio.scrobbleMode = "ncbl"
        }
    }

    MenuItem {
        visible: qualityMenu.eqAvailable
        text: "均衡器…"
        icon.name: "audio-equalizer"
        onTriggered: qualityMenu.equalizerRequested()
    }
}

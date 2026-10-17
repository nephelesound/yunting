// 展示标题行（36px 细体），三种形态，起点永远在页边线上：
//   顶层页：各枢轴并排，当前实色、其余淡出，点淡出的切换；
//   子页：「上级  本页」面包屑，上级淡出、点它返回（代替返回箭头，标题不因返回键而右移）；
//   否则：只有页名。
// 宽度由所在的布局给定；放不下时最后一项（本页名，常是很长的歌名）省略，绝不越过右侧的账号区。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Row {
    id: title

    property string text
    property var pivots: []          // [{ key, text }]
    property string current
    property string parentText       // 非空即为子页

    signal pivotActivated(string key)
    signal parentActivated()

    readonly property var items: title.parentText.length > 0
        ? [{ key: "..", text: title.parentText }, { key: "", text: title.text }]
        : title.pivots.length > 0 ? title.pivots : [{ key: "", text: title.text }]

    // 不省略时需要的宽度：按字体直接量文字（不读可见标签的宽度，避免与省略形成绑定环）。
    // 不用一排不可见的标签去量：不可见的定位器不重新排版，异步到达的页名（歌手名）会按旧宽度被省略
    //（advanceWidth 是方法调用，不建立依赖：先读一次 metrics.font，字体定下来后才会重算）
    readonly property real naturalWidth: {
        void metrics.font;
        return title.items.reduce((w, it) => w + Math.ceil(metrics.advanceWidth(it.text)), 0)
            + title.spacing * (title.items.length - 1);
    }
    FontMetrics {
        id: metrics
        font.pixelSize: Tokens.text4xl
        font.weight: Font.Light
    }

    spacing: Tokens.space6
    clip: true

    Repeater {
        model: title.items
        delegate: Label {
            id: item
            required property var modelData
            required property int index
            readonly property bool active: modelData.key === title.current || modelData.key === ""

            width: Math.max(0, Math.min(implicitWidth, title.width - item.x))
            // 各项按同一条基线排：含回退字体（日文汉字等）的页名行高更大，按顶对齐会比上级低一截
            anchors.baseline: title.top
            anchors.baselineOffset: metrics.ascent
            text: modelData.text
            font.pixelSize: Tokens.text4xl
            font.weight: Font.Light
            elide: Text.ElideRight
            // 键盘：非当前项进 Tab 顺序，回车 / 空格切换；聚焦时与悬停同样提亮，并加下划线
            activeFocusOnTab: !item.active
            font.underline: item.activeFocus
            opacity: active ? 1 : hover.hovered || item.activeFocus ? Tokens.navHover : Tokens.navInactive
            Behavior on opacity { NumberAnimation { duration: Tokens.motionFast } }
            Accessible.role: item.modelData.key === ".." ? Accessible.Link : Accessible.PageTab
            Accessible.name: item.modelData.text

            function activate() {
                if (item.modelData.key === "..")
                    title.parentActivated();
                else
                    title.pivotActivated(item.modelData.key);
            }
            Keys.onReturnPressed: item.activate()
            Keys.onSpacePressed: item.activate()
            HoverHandler { id: hover; enabled: !item.active; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                enabled: !item.active
                onTapped: item.activate()
            }
        }
    }
}

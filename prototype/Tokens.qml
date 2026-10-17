// 设计令牌：界面里出现的每个尺寸都必须是这里的某一档，组件里不写其它数字。
// 档位与 dogenet 设计规范一致（4/8/12/16/24 间距、28/32/40/48 高度、2/4/6/8 圆角、三级字号 + 展示字号）。
pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

QtObject {
    // 间距：只允许这五档
    readonly property int space1: 4    // 紧贴：标签内边距、图标与文字
    readonly property int space2: 8    // 同组元素
    readonly property int space3: 12   // 行内部、标题与其内容
    readonly property int space4: 16   // 组件之间
    readonly property int space6: 24   // 区块之间、页边距

    // 控件高度
    readonly property int height7: 28  // 紧凑
    readonly property int height8: 32  // 标准按钮、图标按钮
    readonly property int height10: 40 // 输入框
    readonly property int height12: 48 // 曲目行、播放条封面

    // 圆角：嵌套时 内层 = 外层 − 内边距
    readonly property int radiusSm: 2  // 标签
    readonly property int radius: 4    // 磁贴、按钮、封面
    readonly property int radiusMd: 6
    readonly property int radiusLg: 8  // 浮层

    // 不透明度：只用 5% 档。同一语义全应用只有一个值
    readonly property real muted: 0.65        // 次要文字 = 正文色 × 0.65（三套配色下对比度都 ≥ 4.5:1；disabledTextColor 只给真正禁用的东西）
    readonly property real tintHover: 0.1     // 可点行/卡悬停的强调色淡色调（静止时为 0）
    readonly property real tintPressed: 0.15  // 按下
    readonly property real tonal: 0.1         // 静止的淡色调面：VIP 标签、无封面的入口磁贴
    readonly property real tonalHover: 0.15
    readonly property real placeholder: 0.05  // 封面加载前的占位底
    readonly property real track: 0.15        // 进度线未播放段
    readonly property real faded: 0.25        // 压暗：过期的二维码、封面占位图标
    readonly property real fadedHover: 0.5    // 禁用的强调色按钮
    // 排版导航（枢轴、面包屑上级）的非当前项：Metro 式「当前实、其余虚」，但 0.5 才让 36/24 的细体
    // 在 Breeze 亮色下有约 3:1 的对比度，读得出是可点的导航；悬停 / 键盘聚焦提到 0.75
    readonly property real navInactive: 0.5
    readonly property real navHover: 0.75
    // 正在播放页封面氛围底上的系统底色遮罩：只透出 15% 的封面色调，文字对比度保持
    readonly property real ambienceScrim: 0.85
    // 悬停在可点的封面上：系统底色淡底，衬出说明去向的图标
    readonly property real coverScrim: 0.6
    // 桌面歌词悬停时的深色底：压在任意壁纸上，白字仍可读，又不挡住桌面
    readonly property real desktopBacking: 0.45
    // 桌面歌词悬停时的深色底板：白字在任意壁纸与窗口上都看得清，又透出一点底下的东西
    readonly property real desktopPanel: 0.6
    // 桌面歌词：隔着一段距离看的字，三档（当前句 / 第二行）；填色可选系统强调色或这几种在任意壁纸上都醒目的颜色
    readonly property var desktopLyricSizes: [[text2xl, textBase], [text4xl, text2xl], [48, text4xl]]
    readonly property var desktopLyricColors: ["#4fc3f7", "#ff8a80", "#69f0ae", "#ffd740"]

    // 动效：时长一律取 Kirigami.Units，桌面平台插件会按 Plasma「动画速度」(AnimationDurationFactor) 缩放，
    // 用户关掉动画时它们就是 0，减弱动效不用另写分支。
    readonly property int motionFast: Kirigami.Units.shortDuration      // 100：按下、悬停
    readonly property int motionBase: Kirigami.Units.longDuration       // 200：页面进入、封面淡入、焦点变化
    readonly property int motionSlow: Kirigami.Units.veryLongDuration   // 400：只给被动观看的环境运动（歌词滚动）
    // 曲线：进出用强 ease-out，屏上移动用强 ease-in-out；不用 ease-in
    readonly property var easeOut: [0.23, 1, 0.32, 1, 1, 1]
    readonly property var easeInOut: [0.77, 0, 0.175, 1, 1, 1]

    // 字号（像素）：展示 / 分组标题 / 正文 / 说明 / 标签
    readonly property int text4xl: 36
    readonly property int text2xl: 24
    readonly property int textBase: 16
    readonly property int textSm: 14
    readonly property int textXs: 12

    // 图标：跟随 Breeze 的尺寸档（16/22/32），其它尺寸会被缩回最近一档
    readonly property int iconSm: 16
    readonly property int iconMd: 22
    readonly property int iconLg: 32

    // 线宽：只有 1 与 2。播放条顶边那条进度线就是 2，同时充当内容区与播放条的分界
    readonly property int line: 1
    readonly property int lineStrong: 2

    // 版面
    readonly property int pageMargin: space6
    readonly property int hudHeight: height12 + 2 * space4   // 80
    readonly property int trackRow: height12                  // 48
    readonly property int trackColumn: 280                    // 35 × 8
    readonly property int measureMax: 400                     // 正在播放封面的最大边长（50 × 8）
    readonly property int wideRow: 480                        // 曲目行宽于此时显示专辑列（60 × 8）
    readonly property int albumColumn: 160                    // 专辑列宽（20 × 8）

    // 封面磁贴边长：3 块磁贴 + 2 道间距 恰好等于 n 条曲目行（48 + 8）的高度，
    // 所以「排行榜」与「热歌」两组在全景里上下边缘对齐。n = 9 → 160，n = 6 → 104。
    readonly property int tileSm: 104   // 2 行曲目的高度（2 × 56 − 8）
    readonly property int tileLg: 160
    function tileFor(availableHeight) {
        return availableHeight >= 3 * tileLg + 2 * space2 ? tileLg : tileSm;
    }
    function rowsFor(tile) {
        return (3 * tile + 2 * space2 + space2) / (trackRow + space2);
    }
}

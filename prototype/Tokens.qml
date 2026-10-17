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
    // 压在视频画面上的控制条深色底：白字图标在任何画面上都看得清，又不挡住画面
    readonly property real videoScrim: 0.45
    // 桌面歌词的字：纯白（亮壁纸上可换纯黑）配不透明度，不靠阴影、不靠底板。
    // 还没唱到的字与第二行是这个不透明度，唱到的字点亮成全不透明
    readonly property real desktopInk: 0.55
    // 悬停时工具条的底：与字同一色系的不透明深灰，白色图标
    readonly property color desktopToolbar: "#505050"
    // 桌面歌词：隔着一段距离看的字，三档（当前句 / 第二行）。
    // 点亮色默认是字色本身（全不透明），也可选系统强调色或这几种中间调（亮壁纸、暗壁纸上都读得出）
    // 分享图：3:4 竖图（45 × 8 宽），导出放大 3 倍（1080 × 1440）。底是封面色场，压暗到白字站得住
    readonly property int shareCardWidth: 360
    readonly property real shareScrim: 0.45
    readonly property var desktopLyricSizes: [[text2xl, textBase], [text4xl, text2xl], [48, text4xl]]
    readonly property var desktopLyricColors: ["#1e88e5", "#e53935", "#43a047", "#ef6c00"]

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
    // Breeze 缺的或会误导的图标（来源与许可见 icons/LICENSES.txt）
    // 已喜欢：实心红心（Breeze 只有空心的 love-symbolic，未喜欢用它）。红色写在 SVG 里：样式化的按钮不给文件图标上色。
    // 「喜欢 / 赞」的红是固定的，不跟配色方案的 negative（那是「出错」的意思，有的方案里是品红）
    readonly property url likedIcon: Qt.resolvedUrl("icons/heart-filled.svg")
    readonly property color liked: "#da4453"
    // 评论的赞：网易云用拇指，不用心（心是「喜欢歌曲」）
    readonly property url upvoteIcon: Qt.resolvedUrl("icons/thumb-like-16-regular.svg")
    readonly property url upvotedIcon: Qt.resolvedUrl("icons/thumb-like-16-filled.svg")
    // 退出登录：Breeze 的 system-log-out 在 16 px 是一个「<」，读起来像「返回」
    readonly property url logoutIcon: Qt.resolvedUrl("icons/arrow-exit-16-regular.svg")
    // 播放队列：Breeze 的 view-media-playlist（几行加音符）与旁边的歌词按钮几乎一样；队列就是一张清单
    readonly property url queueIcon: Qt.resolvedUrl("icons/text-bullet-list-16-regular.svg")
    // 迷你播放器：Breeze 的 window-minimize-pip 是个方框带箭头，看不出是「画中画」
    readonly property url miniIcon: Qt.resolvedUrl("icons/picture-in-picture-16-regular.svg")

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

    // 封面磁贴边长
    readonly property int tileSm: 104   // 2 行曲目的高度（2 × 56 − 8）：榜单卡片的封面与右侧两行对齐
    readonly property int tileLg: 160
}

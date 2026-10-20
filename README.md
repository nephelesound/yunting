<div align="center">

<img src="data/io.github.nephelesound.Yunting.svg" width="96" alt="云听">

# 云听 Yunting

**为 KDE Plasma 打造的网易云音乐客户端**

原生 Qt 6 / Kirigami 界面，不套网页壳，随系统主题、字体和图标变化

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
[![Release](https://img.shields.io/github/v/release/nephelesound/yunting)](https://github.com/nephelesound/yunting/releases)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-informational)
![Qt 6](https://img.shields.io/badge/Qt-6-41cd52)
![KDE Plasma](https://img.shields.io/badge/KDE-Plasma%206-1d99f3)

<img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/home.webp" width="860" alt="首页：每日推荐、心动模式、私人漫游、私人雷达和推荐歌单">

</div>

云听是一款原生的网易云音乐桌面客户端。界面全部由 Plasma 自带的控件绘制，配色、字体、图标和动画速度都随系统设置变化；它不套网页壳，也不需要在后台运行 Node 服务，启动快，占用少。

## 为什么选择云听

- **原生应用**：不基于 Electron，不内嵌浏览器，在 NVIDIA 显卡上也不会白屏。
- **融入 Plasma**：媒体键、锁屏界面的播放控件、系统托盘、通知和电源管理一应俱全；在浏览器或 Telegram 里点开网易云链接，会直接跳到云听。
- **推荐优先**：一打开就是每日推荐、心动模式、私人漫游和私人雷达；排行榜、新歌新碟和播客都归在「发现」页。
- **账号功能齐全**：评论（包括楼中楼回复）、一起听、私信与通知、用户主页的听歌排行和云盘都能用；你的听歌记录也会同步回网易云，和官方客户端一样计入听歌排行。
- **界面克制**：用排版组织信息，只在需要的地方加装饰；能点的东西，一看就知道能点。

<table>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying.webp" alt="正在播放：封面和滚动的歌词"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying-dark.webp" alt="同一页面的暗色配色"></td>
</tr>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/discover.webp" alt="发现页：官方榜、更多榜单和热歌榜"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/share.webp" alt="分享：链接、二维码、发送到 Telegram 和分享图"></td>
</tr>
</table>

## 功能

**推荐与发现**
- 每日推荐（附推荐理由）、心动模式、私人漫游、私人雷达、相似歌曲和推荐歌单
- 「发现」页：官方榜和更多榜单、新歌、新碟、精品歌单、MV、播客
- 搜索单曲、歌手、专辑、歌单、MV 和用户，支持搜索建议和热搜

**正在播放**
- 歌词随播放平滑滚动；支持逐字歌词的歌曲会逐字高亮，译文紧跟在原文下方
- 点击任意一句歌词，就从那里开始播放；右键可以复制这一句或整首歌词
- 评论：精彩评论、全部评论和楼中楼回复，都可以点赞
- 百科：你和这首歌的回忆坐标、曲风等基本信息、副歌位置、相关 MV 和获奖记录
- 相似：点击相似歌曲就接着播放，点击相似歌单就打开
- 歌曲有动态封面时，显示动态封面

**我的音乐**
- 我喜欢的音乐、最近播放、云盘，以及收藏的歌单、歌手、专辑和 MV
- 新建歌单、添加歌曲、调整顺序、重命名和删除
- 把歌曲下载到本地，没有网络也能播放

**与好友一起**
- 一起听：用二维码或链接邀请好友；打开「一起听」面板就能看到哪些好友在线
- 用户主页：动态、听歌排行（最近一周和所有时间）、关注和粉丝，并显示头像挂件和 VIP / SVIP 标识
- 消息：私信、评论、@我和通知（目前只能查看，不能回复）

**播放**
- 音质可选，从标准到 Hi-Res；10 段均衡器，另有低音和空间感调节；可以选择输出设备
- 播放队列可以拖动排序，重启后队列和播放进度都会恢复
- 智能续播：在本机记录你听完、跳过和标红心的歌曲，队列快播完时按你的口味补充歌曲；可以关闭，数据不会上传
- 提前获取下一首的播放地址，切歌几乎不用等待
- 每一行歌曲都有红心按钮；连续点赞、关注或收藏时只提交最后一次操作，避免请求过于频繁而触发风控

**桌面集成**
- MPRIS：支持媒体键、Plasma 媒体控件和锁屏界面的播放控件
- 关闭窗口后，可以在托盘里继续播放
- 播放音乐时阻止系统自动睡眠，但不影响屏幕熄灭；观看 MV 时屏幕保持常亮
- 桌面歌词：在 Wayland 下置顶显示，可以拖动，字号和颜色都可以调整
- 迷你播放器和全屏封面
- 打开网易云链接：云听注册为 `orpheus://` 协议的处理程序，网页上的「在客户端打开」会直接唤起云听；复制歌曲、歌单或专辑的链接（包括 `163cn.tv` 短链接）后切回云听，它会提示你打开
- 分享：复制链接或歌曲信息、用手机扫码、发送到 Telegram 或邮件，也可以生成一张带封面和歌词的分享图

## 与其他客户端对比

在 Linux 上听网易云音乐，常见的选择有以下几个。官方 Linux 版（1.2.1）自 2019 年起就没有更新过，因此不参与对比。

|  | 云听 | [SPlayer-Next](https://github.com/SPlayer-Dev/SPlayer-Next) | [AlgerMusicPlayer](https://github.com/algerkong/AlgerMusicPlayer) | [NeteaseCloudMusicGtk4](https://github.com/gmg137/netease-cloud-music-gtk) |
|---|:-:|:-:|:-:|:-:|
| 界面技术 | Qt 6 / Kirigami | Electron | Electron | GTK 4 / libadwaita |
| 支持平台 | Linux | Windows、macOS、Linux | Windows、macOS、Linux、网页 | Linux |
| 跟随系统配色和字体 | ✅ | ◐ 仅跟随明暗模式 | ◐ 仅跟随明暗模式 | ✅ |
| 每日推荐、私人漫游、心动模式 | ✅ | ✅ | ✅ | ◐ 缺少私人漫游 |
| 逐字歌词 | ✅ | ✅ | ✅ | ❌ |
| 桌面歌词 | ✅ | ✅ | ✅ | ◐ 需安装 GNOME 扩展 |
| 歌曲评论 | ✅ | ✅ | ◐ 仅支持播客评论 | ❌ |
| MV | ✅ | ❌ | ✅ | ❌ |
| 播客 | ✅ | ❌ | ✅ | ❌ |
| 下载 | ✅ | ✅ | ✅ | ❌ |
| 云盘 | ◐ 可浏览和播放 | ✅ 支持上传 | ❌ | ✅ |
| 一起听 | ✅ | ❌ | ❌ | ❌ |
| 私信与通知 | ◐ 仅能查看 | ❌ | ❌ | ❌ |
| 听歌记录同步到网易云 | ✅ | ✅ | ❌ | ❌ |
| 均衡器 | ✅ | ✅ | ✅ | ❌ |
| MPRIS 媒体控制 | ✅ | ✅ | ✅ | ✅ |
| 打开网易云链接 | ✅ | ✅ | ❌ | ❌ |
| 本地音乐库 | ❌ | ✅ | ✅ | ❌ |
| 灰色歌曲换源播放 | ❌ | ❌ | ✅ | ❌ |
| Last.fm / Discord | ❌ | ✅ | ❌ | ❌ |
| 界面语言 | 仅中文 | 多语言 | 多语言 | 多语言 |
| 许可证 | GPL-3.0+ | AGPL-3.0 | MIT | GPL-3.0 |

◐ 表示部分支持，具体限制写在单元格里。其他客户端的情况是我们在 2026 年 10 月对照各自的源码核实的，之后可能有变化；如有出入，欢迎提 Issue 指正。

简单来说：如果你用的是 Plasma，主要在网易云听歌，又希望客户端像系统自带的应用一样自然，云听正适合你。如果你还需要管理本地音乐、连接 Navidrome 或 Jellyfin，或者要在 Windows、macOS 上使用，SPlayer-Next 会更合适；如果想播放网易云上的灰色歌曲，可以试试 AlgerMusicPlayer。

## 安装

### 所有发行版（Flatpak）

从 [Releases](https://github.com/nephelesound/yunting/releases) 下载 `yunting-x86_64.flatpak`（ARM 设备请下载 `yunting-aarch64.flatpak`），然后运行：

```bash
flatpak install --user yunting-x86_64.flatpak
```

所需的 KDE 运行时会自动从 Flathub 安装。更新时，下载新版本的安装包再安装一次即可；卸载请运行 `flatpak uninstall io.github.nephelesound.Yunting`。

### Arch Linux

云听目前不在 AUR 上（AUR 暂停了新账号注册），所以用 `yay` 或 `paru` 搜不到。请用仓库里的 PKGBUILD 从 GitHub 构建，安装最新的发布版本：

```bash
sudo pacman -S --needed base-devel git
git clone https://github.com/nephelesound/yunting.git
cd yunting/packaging/aur
makepkg -si
```

`makepkg` 会从官方仓库安装依赖，下载 PKGBUILD 中指定版本的源码，构建并通过测试后交给 pacman 安装。

- 更新：进入 `yunting/packaging/aur`，运行 `git pull && makepkg -si`
- 卸载：`sudo pacman -R yunting`
- 如果以前装过 `yunting-git`（从主干构建的旧包）：照上面的步骤安装即可，pacman 会提示用 `yunting` 替换它

### 从源码构建

依赖：Qt 6（Base、Declarative）、MpvQt、libmpv、Kirigami、Kirigami Addons、KCoreAddons、KDBusAddons、KWindowSystem、KI18n、KColorScheme、KConfig、KStatusNotifierItem、Prison、QtKeychain、OpenSSL、Fontconfig。可选依赖：LayerShellQt（在 Wayland 下置顶桌面歌词）、zstd。

```bash
git clone https://github.com/nephelesound/yunting.git
cd yunting
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

安装完成后，可以在应用菜单里找到「云听」，也可以在终端运行 `yunting`。

## 开发

```bash
make app        # 构建并运行
make test       # 运行后端单元测试
make lint       # 运行 qmllint
make shots      # 离屏截取各个页面（亮色、暗色、窄窗口）
```

日志和其他 KDE 应用一样使用 Qt 的日志系统：从桌面启动时写入 systemd journal（用 `journalctl --user -t yunting` 查看），加上 `--verbose` 参数可以输出调试日志。日志不会记录 cookie 和查询参数。

排查请求量：所有对网易云接口的请求都经由网络出口 `NcmClient` 发出，那里设有一个流量计（下载音频文件走单独的连接，不计入其中）。

- 运行 `yunting --verbose`，终端会逐条打印每个请求的方法和路径。
- 同一个读请求在 1 秒内第 4 次发出时会被拦截并告警（`yunting.traffic`）。正常的轮询和重试远达不到这个频率，一旦出现，就说明程序有 bug。1 秒内超过 40 个请求同样会告警，并列出请求最频繁的路径。
- 在「关于」→「复制诊断信息」里，可以看到最近一分钟的请求总数、每秒峰值、被拦截的次数和请求最频繁的路径。

欢迎提交 Issue 和 Pull Request。

## 许可证

Copyright © 2026 sudoskys。云听是自由软件，以 [GNU 通用公共许可证](LICENSE)第 3 版或（由你选择）任何更高版本发布，不提供任何担保。如需 GPL 以外的授权（例如闭源的商业版本），请联系版权所有者。

应用图标来自 Microsoft Fluent Emoji 的 `musical-note`（MIT 许可），详见 [data/ICON-LICENSE](data/ICON-LICENSE)；界面中其他图标的来源和许可见 [prototype/icons/LICENSES.txt](prototype/icons/LICENSES.txt)。生成 Material You 配色用的是 Google 的 [Material Color Utilities](https://github.com/material-foundation/material-color-utilities)（Apache-2.0），所用的部分和许可证在 [src/third_party/material-color-utilities](src/third_party/material-color-utilities)。

## 免责声明

「网易云音乐」的名称和商标，以及通过其接口获取的音乐、歌词、图片、评论等内容，均归各自的权利人所有。云听只访问用户本人账号有权访问的内容，不提供、不存储、也不分发任何受版权保护的内容。本程序按「原样」提供，作者不对使用本程序造成的任何后果负责。请遵守网易云音乐的用户协议和当地法律，风险自负。

<div align="center">
<sub>由 <a href="https://github.com/nephelesound">Nephele Sound</a> 维护</sub>
</div>

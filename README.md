<div align="center">

<img src="data/io.github.nephelesound.Yunting.svg" width="96" alt="云听">

# 云听 Yunting

**为 KDE Plasma 打造的网易云音乐客户端**

原生 Qt 6 / Kirigami 界面 · 没有 WebView · 跟随系统主题、字体与图标

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
[![Release](https://img.shields.io/github/v/release/nephelesound/yunting)](https://github.com/nephelesound/yunting/releases)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-informational)
![Qt 6](https://img.shields.io/badge/Qt-6-41cd52)
![KDE Plasma](https://img.shields.io/badge/KDE-Plasma%206-1d99f3)

<img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/home.webp" width="860" alt="首页：每日推荐、心动模式、私人漫游、私人雷达与推荐歌单">

</div>

云听用 Qt Quick 和 Kirigami 写成，界面是 Plasma 自己的控件：配色、字体、图标、动画速度都跟着系统设置走，媒体键、锁屏控件、托盘、通知也都接好了。它不内嵌浏览器，也不在本地跑 Node 服务，开得快，占用小。

## 为什么是云听

- **原生**：没有 Electron，没有内嵌浏览器，没有本地 Node 服务；NVIDIA 上也不会白屏。
- **KDE 应用该有的它都有**：跟随 Plasma 的配色与字体；MPRIS 媒体键与锁屏控件、托盘、系统通知、电源管理；浏览器和 Telegram 里的网易云链接直接在云听里打开。
- **个人推荐在前**：打开就是每日推荐、心动模式、私人漫游、私人雷达；排行榜、新歌新碟和播客都放在「发现」里。
- **网易云账号能用的大多都有**：评论与楼中楼回复、一起听、私信与通知、用户主页的听歌排行、云盘；听歌记录会同步回网易云，和官方客户端一样计入听歌排行。
- **界面安静**：排版先行，装饰只用在有用处的地方。能点的东西一眼看得出来。

<table>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying.webp" alt="正在播放：封面与逐行滚动的歌词"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying-dark.webp" alt="同一页在暗色配色下"></td>
</tr>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/discover.webp" alt="发现：官方榜、更多榜单与热歌榜"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/share.webp" alt="分享：链接、二维码、发送到 Telegram 与分享图"></td>
</tr>
</table>

## 功能

**推荐与发现**
- 每日推荐（带推荐理由）、心动模式、私人漫游、私人雷达、相似歌曲、推荐歌单
- 发现：官方榜与更多榜单、新歌、新碟、精品歌单、MV、播客
- 搜索单曲、歌手、专辑、歌单、MV 与用户；有搜索建议与热搜

**正在播放**
- 歌词随播放平滑滚动；有逐字时间轴的歌逐字填色；翻译与原文成组显示
- 点一句从那里播放，右键复制这句或全部歌词
- 评论：精彩评论、全部评论与楼中楼回复，可点赞
- 百科：你和这首歌的回忆坐标、曲风与基本信息、副歌位置、相关 MV 与获奖
- 相似：相似歌曲点一下就接着放，相似歌单点开即看
- 有动态封面的歌显示动态封面

**我的音乐**
- 我喜欢的音乐、最近播放、云盘、收藏的歌单、歌手、专辑与 MV
- 创建歌单，加歌，排序、改名、删除
- 下载到本地，随时离线播放

**和朋友一起**
- 一起听：扫码或发链接邀请；点开「一起听」能看到哪些好友在线
- 用户主页：动态、听歌排行（最近一周 / 全部）、关注与粉丝；头像挂件与 VIP / SVIP 标识
- 消息：私信、评论、@我、通知（只读）

**播放**
- 音质可选，从标准到 Hi-Res；10 段均衡器，另有低音与空间感；可选输出设备
- 队列可拖动排序，重启后队列与播放位置都会恢复
- 智能续播：在本机记录完整播放、跳过与红心，队列快放完时按口味补歌；可以关掉，数据不上传
- 预取下一首的播放地址，切歌更快
- 每行都有红心。连续点赞、关注、收藏时只发最后一次，免得连点触发风控

**与桌面的集成**
- MPRIS：媒体键、Plasma 媒体控件与锁屏控件
- 关掉窗口后在托盘里继续播放
- 放歌时系统不自动睡眠，屏幕照常熄灭；看 MV 时屏幕常亮
- 桌面歌词：Wayland 下置顶、可拖动，字号与填色可调
- 迷你播放器、全屏封面
- 打开网易云链接：注册为 `orpheus://` 的处理程序，浏览器里的「在客户端打开」会唤起云听；复制的歌曲、歌单、专辑链接（包括 `163cn.tv` 短链）切回云听时会提示打开
- 分享：复制链接或歌曲信息、手机扫码、发到 Telegram 或邮件，或生成一张带封面和歌词的分享图

## 和其他客户端比

在 Linux 上听网易云，常见的选择有下面几个。官方 Linux 版（1.2.1）自 2019 年起没有更新，不列入比较。

|  | 云听 | [SPlayer-Next](https://github.com/SPlayer-Dev/SPlayer-Next) | [AlgerMusicPlayer](https://github.com/algerkong/AlgerMusicPlayer) | [NeteaseCloudMusicGtk4](https://github.com/gmg137/netease-cloud-music-gtk) |
|---|:-:|:-:|:-:|:-:|
| 界面 | Qt 6 / Kirigami | Electron | Electron | GTK 4 / libadwaita |
| 平台 | Linux | Windows · macOS · Linux | Windows · macOS · Linux · 网页 | Linux |
| 跟随系统配色与字体 | ✅ | ◐ 只跟亮 / 暗 | ◐ 只跟亮 / 暗 | ✅ |
| 每日推荐 · 私人漫游 · 心动模式 | ✅ | ✅ | ✅ | ◐ 没有私人漫游 |
| 逐字歌词 | ✅ | ✅ | ✅ | ❌ |
| 桌面歌词 | ✅ | ✅ | ✅ | ◐ 需要 GNOME 扩展 |
| 歌曲评论 | ✅ | ✅ | ◐ 只有播客评论 | ❌ |
| MV | ✅ | ❌ | ✅ | ❌ |
| 播客 | ✅ | ❌ | ✅ | ❌ |
| 下载 | ✅ | ✅ | ✅ | ❌ |
| 云盘 | ◐ 浏览与播放 | ✅ 含上传 | ❌ | ✅ |
| 一起听 | ✅ | ❌ | ❌ | ❌ |
| 私信与通知 | ◐ 只读 | ❌ | ❌ | ❌ |
| 听歌记录同步回网易云 | ✅ | ✅ | ❌ | ❌ |
| 均衡器 | ✅ | ✅ | ✅ | ❌ |
| MPRIS 媒体控件 | ✅ | ✅ | ✅ | ✅ |
| 打开网易云链接 | ✅ | ✅ | ❌ | ❌ |
| 本地音乐库 | ❌ | ✅ | ✅ | ❌ |
| 灰色歌曲换源播放 | ❌ | ❌ | ✅ | ❌ |
| Last.fm / Discord | ❌ | ✅ | ❌ | ❌ |
| 界面语言 | 中文 | 多语言 | 多语言 | 多语言 |
| 许可证 | GPL-3.0+ | AGPL-3.0 | MIT | GPL-3.0 |

◐ 表示部分支持，说明写在格子里。其他客户端的情况是 2026 年 10 月对照各自源码核对的，之后可能有变化，发现不对请提 Issue。

所以：如果你用 Plasma，主要听网易云，想要一个像系统自带应用那样的客户端，云听是为你写的。如果你还要管理本地音乐、连接 Navidrome / Jellyfin，或者要在 Windows 和 macOS 上用，SPlayer-Next 更合适；想播放网易云上的灰色歌曲，可以用 AlgerMusicPlayer。

## 安装

### 任意发行版（Flatpak）

从 [Releases](https://github.com/nephelesound/yunting/releases) 下载 `yunting-x86_64.flatpak`（ARM 机器用 `yunting-aarch64.flatpak`），然后：

```bash
flatpak install --user yunting-x86_64.flatpak
```

需要的 KDE 运行时会自动从 Flathub 装上。更新：下载新版本的包再装一次。卸载：`flatpak uninstall io.github.nephelesound.Yunting`。

### Arch Linux

云听不在 AUR 上（AUR 暂停了新账号注册），`yay` / `paru` 搜不到。用仓库里的 PKGBUILD 从 GitHub 构建并安装最新发布的版本 `yunting`：

```bash
sudo pacman -S --needed base-devel git
git clone https://github.com/nephelesound/yunting.git
cd yunting/packaging/aur
makepkg -si
```

`makepkg` 从官方仓库装好依赖，取 PKGBUILD 里写的那个发布版本的源码，构建、跑测试，再交给 pacman 安装。

- 更新：回到 `yunting/packaging/aur`，运行 `git pull && makepkg -si`
- 卸载：`sudo pacman -R yunting`
- 以前装过 `yunting-git`（从主干构建的旧包）：照上面安装即可，pacman 会提示用 `yunting` 替换它

### 从源码构建

依赖：Qt 6（Base、Declarative）、MpvQt、libmpv、Kirigami、Kirigami Addons、KCoreAddons、KDBusAddons、KWindowSystem、KI18n、KColorScheme、KConfig、KStatusNotifierItem、Prison、QtKeychain、OpenSSL；可选 LayerShellQt（Wayland 下的桌面歌词置顶）、zstd。

```bash
git clone https://github.com/nephelesound/yunting.git
cd yunting
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

安装后在应用菜单里找「云听」，或在终端运行 `yunting`。

## 开发

```bash
make app        # 构建并运行
make test       # 后端单元测试
make lint       # qmllint
make shots      # 各屏离屏截图（亮色、暗色、窄窗）
```

日志与其他 KDE 应用一样走 Qt 日志：从桌面启动时写进 systemd journal（`journalctl --user -t yunting`），`--verbose` 打开调试级日志。日志不记录 cookie 与查询参数。

排查请求量：对网易云的接口请求都从网络出口（`NcmClient`）发出，那里有一个流量计（下载音频文件走另外的连接，不计在内）。

- `yunting --verbose` 在终端里逐条打印每个请求的方法与路径。
- 同一个读请求 1 秒内第 4 次会被拦下并告警（`yunting.traffic`）：正常的轮询与重试都远低于此，出现就是程序的 bug；1 秒内超过 40 个请求也会告警，并列出最常请求的路径。
- 「关于」→「复制诊断信息」里有最近一分钟的请求总数、峰值每秒、被拦下的次数与最常请求的路径。

欢迎提 Issue 与 Pull Request。

## 许可

Copyright © 2026 sudoskys. 云听是自由软件，按 [GNU 通用公共许可证](LICENSE) 第 3 版或（由你选择）任何更新的版本发布，不附带任何担保。需要 GPL 以外的授权（例如闭源的商业版本），请联系版权所有者。

应用图标是 Microsoft Fluent Emoji 的 `musical-note`（MIT），许可见 [data/ICON-LICENSE](data/ICON-LICENSE)；界面里补充的图标来源与许可见 [prototype/icons/LICENSES.txt](prototype/icons/LICENSES.txt)。

## 免责声明

「网易云音乐」名称与商标，以及通过其接口获取的音乐、歌词、图片、评论等内容，归各自权利人所有。云听只访问使用者本人账号有权访问的内容，不提供、不存储、不分发任何受版权保护的内容。本程序按「原样」提供，作者不对使用本程序产生的任何后果负责；请遵守网易云音乐用户协议与所在地法律，使用风险自负。

<div align="center">
<sub>由 <a href="https://github.com/nephelesound">Nephele Sound</a> 维护</sub>
</div>

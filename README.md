<div align="center">

<img src="data/io.github.nephelesound.Yunting.svg" width="96" alt="云听">

# 云听 Yunting

**为 KDE Plasma 打造的网易云音乐客户端**

原生 Qt 6 / Kirigami 界面 · 没有 WebView · 跟随系统主题、字体与图标

[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-informational)
![Qt 6](https://img.shields.io/badge/Qt-6-41cd52)
![KDE Plasma](https://img.shields.io/badge/KDE-Plasma%206-1d99f3)
[![AUR: yunting-git](https://img.shields.io/badge/AUR-yunting--git-1793d1)](https://aur.archlinux.org/packages/yunting-git)

<img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/home.webp" width="860" alt="首页：个人推荐">

</div>

## 为什么是云听

- **原生**：Qt Quick + Kirigami 渲染，没有 Electron、没有内嵌浏览器、没有本地 Node 服务。启动快，占用小，在 NVIDIA 上也不白屏。
- **像 KDE 的一员**：跟随 Plasma 的配色、字体、图标与动画速度；托盘、MPRIS 媒体键与锁屏控件、KDE Connect 分享、系统电源管理都接好了。
- **个人推荐在前**：打开就是每日推荐、心动模式、私人漫游、私人雷达；排行榜和搜索各在一处，不抢位置。
- **安静的界面**：排版先行，装饰只在有用的地方出现；看得出能点的就能点。

## 功能

<table>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying.webp" alt="正在播放与歌词"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/daily.webp" alt="每日推荐"></td>
</tr>
<tr>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/nowplaying-dark.webp" alt="暗色主题"></td>
<td width="50%"><img src="https://raw.githubusercontent.com/nephelesound/.github/main/assets/yunting/share.webp" alt="分享"></td>
</tr>
</table>

**听歌**
- 扫码登录，凭据存在系统钥匙串
- 每日推荐（带推荐理由）、心动模式、私人漫游、私人雷达、相似歌曲、我喜欢的音乐、推荐歌单、排行榜
- 搜索单曲、歌手、专辑、歌单与 MV
- 歌手页、专辑页、用户主页；MV 播放
- 音质可选（标准到 Hi-Res），10 段均衡器、低音与空间感，输出设备选择

**歌词**
- 跟随播放平滑滚动，翻译与原文成组显示
- 点一句跳到那里播放；右键复制这句或全部歌词
- 桌面歌词：Wayland 下置顶、可拖动，字号与填色可调，当前句随播放逐字填色

**队列与收藏**
- 播放队列可拖动排序，队列与播放位置在重启后恢复
- 每一行都有红心，一点即收藏；加到歌单、歌单排序与管理
- 评论：精彩评论与全部评论，可点赞

**与桌面的集成**
- MPRIS：媒体键、Plasma 媒体控件与锁屏控件
- 托盘：关窗口后在托盘继续播放
- 放歌时系统不自动睡眠（屏幕照常熄灭），看 MV 时屏幕常亮
- 分享：复制链接或歌曲信息、手机扫码，或经 KDE 原生分享发到手机、邮件、Telegram
- 迷你播放器、全屏封面

## 安装

### Arch Linux（AUR）

```bash
paru -S yunting-git    # 或 yay -S yunting-git
```

### 从源码构建

依赖：Qt 6（Base、Declarative、Multimedia）、Kirigami、KStatusNotifierItem、Purpose、Prison、QtKeychain、OpenSSL；可选 LayerShellQt（Wayland 下的桌面歌词置顶）、libqrencode（分享二维码）、zstd。

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

欢迎提 Issue 与 Pull Request。

## 许可

Copyright © 2026 sudoskys. 云听是自由软件，按 [GNU 通用公共许可证](LICENSE) 第 3 版或（由你选择）任何更新的版本发布，不附带任何担保。需要 GPL 以外的授权（例如闭源的商业版本），请联系版权所有者。

应用图标是 Microsoft Fluent Emoji 的 `musical-note`（MIT），许可见 [data/ICON-LICENSE](data/ICON-LICENSE)。

## 免责声明

「网易云音乐」名称与商标，以及通过其接口获取的音乐、歌词、图片、评论等内容，归各自权利人所有。云听只访问使用者本人账号有权访问的内容，不提供、不存储、不分发任何受版权保护的内容。本程序按「原样」提供，作者不对使用本程序产生的任何后果负责；请遵守网易云音乐用户协议与所在地法律，使用风险自负。

<div align="center">
<sub>由 <a href="https://github.com/nephelesound">Nephele Sound</a> 维护</sub>
</div>

# 云听进度清单

状态：✅ 已完成并验证 · 🟡 部分完成 · ❌ 未做

## 核心听歌闭环

- [x] 原生 QML + C++，无 WebView / CEF
- [x] 扫码登录、cookie 钥匙串 / 600 文件后备
- [x] 会话过期、退出登录
- [x] 首页：每日推荐、心动模式、私人漫游、私人雷达、相似歌曲、我喜欢的音乐、推荐歌单
- [x] 排行榜：官方榜列表 + 热歌榜 200 首
- [x] 歌单：完整曲目，>1000 首用 trackIds + 批量详情补齐并保序
- [x] 搜索：单曲 100、艺人 / 歌单 / 专辑 / MV 数据，单曲补封面
- [x] 播放：320k / 无损自动、播放暂停、上下首、进度、音量、静音、三种播放模式
- [x] 队列：点播、移出、清空、持久化
- [x] 歌词：LRC + 翻译 + 当前句滚动
- [x] 评论：热评 10 条读取；全部评论分页；评论点赞（weapi 乐观更新 / 失败回滚）
- [x] 只读信箱后端：私信会话、历史、分页、未读数、附件（歌曲/专辑/歌单/MV）；不落盘、不发送
- [x] 账户/用户主页：VIP 类型、等级、听歌数
- [x] 评论者头像 / 名字进入用户主页（资料 + 公开歌单）
- [x] 红心：乐观更新 / 回滚、全量红心列表、计数与列表同步
- [x] 最近播放：服务端听歌打卡 + 本地立即更新 + 300 条读取
- [x] MPRIS、托盘、媒体键
- [x] 图片 / 元数据缓存、播放地址缓存、断网重试、离线读取旧数据
- [x] 设置持久化：队列、位置、音量、播放模式、窗口大小、页面

## 后端数据与 API

- [x] weapi：AES-CBC 两次 + RSA，写请求不重试
- [x] 鉴权上下文：Web cookie 只走 Web `/api` + `/weapi` + `os=pc`，不混用 `/eapi` 或客户端专有标识
- [x] 匿名只读 smoke：`make smoke-readonly`（CLI + 歌手/专辑/评论/MV/用户/电台）
- [x] 原生 API CLI：`--api list` 清单 + `--api get` 只读调试（可选 cookie）
- [x] 风控标识：`os=pc/appver=3.1.17/osver=10`
- [x] 音质选择：auto/standard/higher/exhigh/lossless/hires；hires 实测 24-bit/48 kHz FLAC
- [x] 性能打点：`yunting.perf` 记录 HTTP 延迟、缓存命中、取址/首次出声耗时
- [x] NCBL 客户端日志：ChaCha20 + RSA + zstd + PLV/PLD，`--scrobble-mode ncbl` 显式开启；默认 legacy
- [x] NCBL 实测：PLV / PLD 均返回 `successfiles`，服务器接受
- [x] 歌单内曲目排序：`reorderPlaylistTracks`（Web weapi）
- [x] LayerShellQt overlay 助手：`backend.layerShell`（Wayland，X11/无依赖自动 no-op）
- [x] 全量歌单 / 红心
- [x] 搜索详情补封面 / 音质 / MV
- [x] 曲目导航元数据：artistId / albumId；歌手 followed、专辑 subscribed / company
- [x] 搜索 MV：12 条（type=1004）
- [x] 搜索用户 / 电台：Cloudsearch 按需取（各 20 / 20 条上限）
- [x] MV 详情 + 播放地址：`mv:<id>`（Web `/weapi`，只读）
- [x] 歌手数据：`artist:<id>`（热歌 50 + 专辑列表）
- [x] 专辑数据：`album:<id>`（完整曲目）
- [x] 歌手全部歌曲分页：`artistSongs:<id>:<offset>`（50 首/页）
- [x] 评论分页：`comments:<songId>:<offset>`（20 条/页）
- [x] 歌单写操作：创建 / 收藏 / 加歌 / 移歌 / 改名 / 删除 / 排序
- [~] 歌单收藏写链路：代码完成；live 验证被 405「操作过于频繁」挡住，账号未变化
- [x] 退出登录清理缓存与在途回包
- [x] 云盘读取分页

## API 覆盖面

参考库有 449 个 module，但里面包含大量重复端点、账号安全、音乐人后台、广告/抽奖等，不等于 449 个都该接。当前完成的是核心听歌闭环（登录、推荐、榜单、歌单、搜索、播放、歌词、评论读/点赞、红心、最近播放、MV、用户主页、信箱只读、听歌统计、NCBL）；**不是“官方客户端全功能”**。

| 领域 | 状态 | 缺口 / 说明 |
|---|---|---|
| 音乐百科 | 🟡 SDK 已接 | typed SDK + `wiki:<id>` Store 缓存已落地；`src/sdk/` 有 structs、校验、service、raw/normalized sample 与 JSON Schema。前端：正在播放页「百科」栏（副歌可点跳转）；真实账号未验 |
| 通知列表 | 🟡 已接 | Store `notify:notices|comments|forwards`；前端：消息页「评论 / @我 / 通知」；live 仍需登录验证 |
| 评论读取 | 🟡 已接 | 歌曲热评/分页/楼中楼；歌单评论热评/分页/楼中楼（`A_PL_0_`）已接。歌单评论点赞未接 |
| 评论写 | ❌ 暂缓 | 发评论/回复依赖 xeapi，与当前 Web cookie 上下文冲突；点赞、楼中楼读取已接 |
| 评论抱一抱 / 举报 / 删除 | ❌ 未接 | `comment/hug`、`comment/report`、`comment/delete` 等 |
| 内容发现 | 🟡 部分 | 前端「发现」页：排行榜、新歌、新碟、精品歌单（按分类）、推荐 MV、播客；banner、首页 block、独家内容不接（多为广告与编辑位） |
| 曲目扩展 | 🟡 部分 | 已有逐字歌词、MV、百科创作者/副歌、动态封面；单曲下载 + 本地文件离线播放已接。歌词标记未接 |
| 音乐库 | 🟡 部分 | 歌单写（含描述，歌单页「编辑简介…」）、歌手/专辑收藏、用户关注/取关；收藏的歌手/专辑/MV 在「我的」第二排与「全部收藏」页；歌单隐私/封面/导入未接 |
| 云盘 | 🟡 只读 | 已有分页读取；上传、导入、删除未接 |
| 媒体扩展 | 🟡 部分 | 播客/电台已接（发现 › 播客、搜索 › 电台、电台页，节目按主歌播放）；视频时间线/详情/播放地址只有 SDK，没有视频页（收藏里的视频暂不列出） |
| 消息与社交 | 🟡 部分 | 私信、用户动态（用户页「动态」）、关注/取关（用户页）已接；发送/回复、一起听、听歌识曲、话题未接 |
| 账户 | 🟡 部分 | 扫码登录、退出、用户资料/等级/统计、cookie 刷新（会话过期自动尝试一次）已接；手机/邮箱登录、资料编辑、头像上传、VIP 信息/任务未接 |
| 搜索 | 🟡 部分 | 单曲/歌手/歌单/专辑/MV/电台结果、搜索联想（输入框下的建议）已接；默认搜索、热搜详情未接 |

不计入近期目标：广告、抽奖、云贝、音乐人后台、粉丝中心、设备管理、注册/验证码、`eapi`/`xeapi` 混用、播放器皮肤/自动更新/硬件独占（无对应稳定 API 或与当前鉴权边界冲突）。

状态以本表与 `docs/API-FRONTEND.md` 为准；`docs/API-FRONTEND.md` 的 P0 是历史建议，底部“已实现的新增数据槽”已落地。

## 官方客户端分析

- [x] macOS dmg：`NeteaseMusic 3.1.8.3330`，CEF + 原生壳
- [x] APK：`9.6.08.261015102648`，hybrid + RN 模块
- [x] 确认官方功能面：下载、本地音乐、云盘上传、MV、桌面歌词、迷你播放器、全屏封面、音效 / EQ / 3D / 杜比、一起听、IM、听歌识曲、播客 / 长音频、皮肤、自动更新
- [x] 确认 APK 资源里有 EQ 预设、混响、空间音频、`rn_rn-podcast-*`、`rn_rn-artist`、`rn_rn-audio-quality-layer`、评论社区、K 歌 / MV 等模块

## 后端下一步（按用户可见价值排序）

- [x] Store 按领域拆分：core / account / like / playlist / follow / on-demand
- [x] 专辑 / 歌手关注状态与订阅写接口（写操作，需控频）
- [x] 音乐百科 SDK：typed models + service + raw/normalized samples（前端 UI 不在本分工）
- [x] 通知列表 SDK：系统通知、评论、转发（登录后才可 live 验证）
- [x] 内容发现 SDK：精品歌单/标签、新歌、新碟（typed models + service）
- [x] 播客/电台 SDK：推荐节目、节目详情、电台详情、分页
- [x] 视频 SDK / MV 收藏列表 SDK

## 前端接入（不属本分工）

- [x] 歌手页 / 专辑页 UI
- [x] 搜索里的歌手 / 专辑 / 歌单 / MV 跳转
- [x] 排行榜卡片进入对应榜单
- [x] 队列拖动排序 / 下一首播放 / 加入队列
- [x] 歌单内排序 UI（上移 / 下移，一次提交完整顺序）
- [x] 歌手关注 / 专辑收藏
- [x] MV 播放页（720p/480p 实际地址 + 播放控制）

## 暂缓

- [x] 下载 / 离线音频：下载 URL、文件落盘、AudioEngine 本地文件优先播放；`--download-selftest` live 验证 25 MB FLAC，离线 WAV smoke 通过
- [ ] 本地音乐库（ID3 / CUE / iPod）
- [x] MV / 视频播放 UI（搜索 MV、曲目菜单、独立播放页）
- [ ] 评论写：发评论/回复走 xeapi（客户端认证），与 Web cookie 上下文冲突；点赞已走 weapi
- [ ] 评论写 / 翻页 UI
- [ ] 云盘上传 / 导入 / 删除
- [ ] 播客 / 电台 / 有声
- [ ] 社交 / IM 发送与回复 / 听歌识曲；一起听后端已接，UI 由前端接
- [x] 会话过期自动尝试 cookie 刷新；失败保留过期状态，等待重新扫码
- [x] DSP 链路：默认走 QAudioBufferOutput → 10 段 biquad + 低音 + 空间感 → QAudioSink；可 `-DYUNTING_EXPERIMENTAL_EQ=OFF` 关闭
- [x] 输出设备选择：`audioOutputs()` / `setOutputDevice(id)`；EQ 与默认路径都生效
- [x] DSP 输出失败自动回退 QAudioOutput
- [x] 5 分钟 soak：14 次切歌 / seek / EQ，0 失败，RSS 稳定在约 177MB，88200Hz 24-bit 正常
- [x] EQ 链路实测：24-bit/48k FLAC 解码为 float 48k，首次出声 249ms（默认路径约 220ms）
- [x] EQ UI：10 段滑杆、低音、空间感、7 组预设；默认启用 DSP 链路
- [ ] 硬件独占（Qt/PipeWire 不提供；不假装 bit-perfect）
- [x] 迷你播放器 / 桌面歌词
- [x] 全屏封面（F11 / 播放条「更多」）
- [ ] 皮肤
- [ ] 全局快捷键 / Touch Bar / 自动更新

## SDK

- [x] typed `Result<T>` / `Error` / `JsonReader`，字段错误带 JSON path
- [x] `SongWiki` structs + `fromApi` 校验/归一化
- [x] `fetchSongWiki` 三段请求编排，可选接口失败不拖垮主接口
- [x] Store `wiki:<id>` 兼容缓存；不落盘（可能含个人回忆坐标）
- [x] samples：request / raw / normalized / schema
- [x] transport seam 测试：HTTP 403、业务码 301、连接失败、personal cookie
- [x] Discovery SDK：new songs / new albums / high-quality playlists / tags / banners
- [x] Radio SDK：recommended programs / program detail / station / program page
- [x] Feed SDK：personalized MVs / private content / homepage blocks
- [x] Video SDK：timeline / detail / signed URLs
- [x] Library SDK：artist / album / MV subscription pages
- [x] Notification SDK：notices / comments / forwards
- [x] Dynamic cover SDK：URL/尺寸/格式，字段别名容错
- [x] Search suggest SDK：匿名 web suggestion
- [x] 歌单描述写：Store::setPlaylistDescription + 本地详情/缓存更新
- [x] Shared SDK transport seam + unified CMake test helper
- [x] 动态封面 SDK：匿名 301，登录后取址；已用 gtk4 cookie live 验证 `videoPlayUrl`/`needTransition`
- [x] 社交读 SDK：用户动态流 typed EventPage（匿名公开流已验证）
- [x] 社交写 SDK：用户关注/取关（weapi，不重试）
- [ ] 社交写：动态发布/转发按下一批

## 一起听后端（已接）

已接入真实明文 REST：`room/create`、`invitation/accept`、`status/get`、`room/check`、`end/v2`、`heartbeat`、`play/command/report`、`sync/list/command/report`、`sync/playlist/get`。

`ListenTogether` 维护房间状态、成员、房间歌单与播放指令：1 秒轮询 `sync/playlist/get` 按 `serverSeq` 应用远端 PLAY/PAUSE/GOTO/seek，5 秒心跳与状态刷新；本地 AudioEngine 的 track/playing/seek 变化反向上报，远端应用期间抑制回声。`syncQueue` 由房主把当前队列 REPLACE 到房间。

前端接缝是 `backend.listenTogether`（Q_PROPERTY/方法/信号见 `src/listentogether.h`）；UI 不在本次范围内，由前端接。

## 未偿债务 / 迁移

- [x] 已迁 typed SDK：comments（热评/分页/楼中楼）、普通+逐字歌词、用户社交（关注/粉丝/等级/计数）、通知计数、MV 详情、用户/电台搜索、首页榜单/热搜/红心 id。
- [x] 曲目/歌单卡片 typed 边界：`sdk::Track` / `sdk::PlaylistCard`，可播性集中在 `playability()`；新歌与电台节目复用同一个 `Track` 适配器。
- [ ] 仍未迁：playlist / search 主体 / artist / album / account 的每日推荐与个人列表 / inbox。它们的嵌套 track 与 playlist card 已改走 typed 边界，下一步可直接把这些端点的请求/响应迁到 typed service，不再被重复的可播性与字段回退挡住。
- [ ] video（视频时间线、详情、播放地址）的 Store/前端接线：还没有视频页；feed 的首页 block、独家内容不接。
- [ ] 歌单隐私/封面/导入；云盘写入；下载地址。
- [x] 一起听后端：真实 REST 房间生命周期、轮询指令、心跳、队列同步；`--listen-together-selftest` 已 live 建/同步/结束。
- [ ] 动态封面签名 URL 的刷新策略由前端负责（有时效）。

## 验证

- [x] `make test` 36/36
- [x] `--download-selftest 108485`：真实下载 25,291,762 字节 FLAC 后清理；本地 WAV 离线播放 smoke 通过
- [x] `--store-key playlistComments:3778678`：真实歌单热评 319,443 条；`playlistComments:3778678:0` 分页落地
- [x] `--listen-together-selftest`：真实建房、同步两首歌单、结束；随后 status inRoom=false
- [x] `--refresh-session`：真实 `login/token/refresh` 返回 ok，并重拉首屏
- [x] `make contract` 登录 + 匿名
- [x] `make lint`
- [x] `make smoke-readonly` 匿名会话，不读钥匙串
- [x] `--like` 红心往返
- [x] `--playlist-selftest` 建 / 加 / 移 / 改名 / 删，账号无残留
- [x] `--detail artist:6452` / `--detail album:32311`
- [x] `--comments 108485:20` 评论分页
- [x] `normalize` 单测：曲目导航字段 / MV / 云盘 / LRC
- [x] `form` 单测：表单 `+` / 空格 / 分隔符编码
- [x] `eqprocessor` 单测：中性直通、1 kHz -12 dB 衰减
- [x] DSP 构建 + Hi-Res / spatial 预设播放 smoke
- [x] `store` / `audioengine_seek` 测试
- [x] `--smoke-audio --audio-quality hires`（24-bit/48 kHz FLAC）
- [x] `--artist-songs 6452:100` 歌手歌曲分页
- [x] `--api list` / `--api get api/v1/artist/6452` / 带 cookie 的 `account`
- [x] `--mv 522365` MV 详情 + 480p 播放地址
- [x] `--search-users 林俊杰` / `--search-radios 林俊杰`
- [x] `--smoke-audio` 播放 + 打卡
- [x] 实时界面截图
- [x] 风控：读接口缓存、写接口单发不重试、测试数据清理
- [x] 写操作 CLI 统一加 `--allow-write` 闸门，误执行直接拒绝

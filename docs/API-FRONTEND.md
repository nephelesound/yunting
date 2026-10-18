# 前端 API 规格（Store / Source）

这份只列**前端实际能消费的字段**与**还没接但值得接的接口**。原始端点见
`docs/reference/ncm-api-enhanced/module/`；数据形状以 `data` / `personal` 为准。

## Typed SDK

新接口不再直接把 `QJsonObject` / `QVariantMap` 拆进 Store 或页面。SDK 边界在 `src/sdk/`：

- `sdk/song_wiki.h`：typed 模型，每个字段注明 raw JSON path；
- `sdk/song_wiki.cpp`：唯一的 raw JSON 读取与校验点；
- `sdk/song_wiki_service.*`：请求编排，返回 `Result<SongWiki>`；
- `sdk/sdk_result.h`：`ErrorKind` / `Error` / `Result<T>`；
- `samples/`：request、raw response、normalized response、JSON Schema。

`wiki:<songId>` 已走这条 SDK；Store 只把 `SongWiki` 转成现有 QML 兼容 map。新增接口按 `src/sdk/README.md` 的 checklist 做。

## 已有
| 能力 | Store key / 触发 | 主要字段 |
|---|---|---|
| 账户 | `account` | `name` `vip` `vipType` `level` `listenSongs` `avatar` `background` |
| 用户主页 | `user:<id>` | `id` `name` `avatar` `signature` `followed` `follows` `followeds` `playlistCount` `vipType` `level` `listenSongs` `playlists[]` |
| 热门评论 | `comments:<songId>` | `userId` `id` `user` `avatar` `content` `likes` `liked` `time` |
| 评论分页 | `comments:<songId>:<offset>` → `data.commentPage` | 同上 + `parentId` |
| 评论点赞 | `setCommentLiked(songId, commentId, liked)` | 乐观更新 `liked` / `likes` |
| 只读信箱 | `inbox` / `inbox:<offset>` | 会话：`userId` `name` `avatar` `vipType` `lastText` `kind` `lastTime` `lastMsgId` `unread` `messageCount` |
| 私信历史 | `inboxThread:<uid>[:<before>]` | 消息：`id` `fromUserId` `fromName` `fromAvatar` `toUserId` `toName` `mine` `time` `message{ text,kind,song,album,playlist,mv }` |
| NCBL 上报 | `AudioEngine.scrobbleMode` | `legacy` / `ncbl` |

## 高价值接口与接入状态

> 状态（2026-10-18）：P0 已落地；P1/P2/P3 多数仍待接。完整覆盖面见 `PROGRESS.md` 的「API 覆盖面」。下表保留原始规划，不代表全部已接。

| 优先级 | 能力 | 端点 | 前端能得到什么 | 建议 Store key |
|---|---|---|---|---|
| P0 | 听歌总时长 | `/api/content/activity/listen/data/total` | `totalDuration`（秒） | `listen:total` |
| P0 | 本周/本月时长 | `/api/content/activity/listen/data/realtime/report?type=week|month` | 周/月收听统计 | `listen:week` / `listen:month` |
| P0 | 歌曲播放排行 | `/api/content/activity/listen/data/song/play/rank?type=month` | 歌曲 + `playCount` | `listen:rank` |
| P0 | 通知计数 | `/api/pl/count` | `msg` `comment` `event` `follow` `notice` `forward` `newProgramCount` | `notifications` |
| P1 | 通知列表 | `/weapi/msg/notices`、`/msg/comments`、`/msg/forwards` | 系统通知 / 评论 / 转发 | `notify:<kind>[:<time>]` |
| P1 | 关注 / 粉丝 | `/api/user/getfollows/<uid>`、`/api/user/getfolloweds/<uid>` | 用户列表，带头像/签名/互关 | `userFollows:<uid>` / `userFolloweds:<uid>` |
| P1 | 用户等级 | `/api/user/level` | 当前等级、听歌数、升级进度 | `userLevel` |
| P1 | 评论回复 | `/weapi/resource/comment/floor/get`（POST） | 楼中楼，含 `parentCommentId` | `commentReplies:<songId>:<commentId>` |
| P1 | 评论抱一抱 | `/weapi/comment/hug/list`、`/comment/hug` | 抱一抱用户列表 | `commentHugs:<songId>:<commentId>` |
| P1 | 音乐百科 | `/api/link/page/parent/relation/construct/info`（明文 POST） | 简介、基本信息、MV、乐评、乐谱、回忆坐标等 block | `wiki:<songId>` |
| P1 | 逐字歌词 | `/api/song/lyric/v1` | `yrc` / `klyric` / 翻译，可做卡拉 OK 和桌面歌词逐字填色 | `lyricsNew:<songId>` |
| P2 | 精品歌单 | `/weapi/top/playlist/highquality`、`/playlist/highquality/tags` | 分类/标签/高赞歌单 | `discover:highquality:<cat>[:<before>]` / `discover:highqualityTags` |
| P2 | 新歌 / 新专辑 | `/weapi/personalized/newsong`、`/album/new`、`/album/newest` | 推荐新歌、新碟 | `discover:newSongs` / `discover:newAlbums` |
| P2 | 歌手/MV/专辑收藏列表 | `/weapi/artist/sublist`、`/album/sublist`、`/mv/sublist` | “我的关注/收藏”页 | `library:artists` / `library:albums` / `library:mvs` |
| P2 | 歌曲下载地址 | `/weapi/song/enhance/download/url/v1` | `url` `size` `br` `type` `level` | `download:<songId>:<level>` |
| P2 | 歌手粉丝 / 关注数 | `/weapi/artist/fans`、`/artist/follow/count` | 歌手社交信息 | `artistSocial:<id>` |
| P3 | 播客 / 电台 | `/weapi/dj/detail`、`/dj/program/detail`、`/dj/sublist` | 电台与节目 | `radio:<id>[:<offset>]` |
| P3 | 云盘写入 | `/weapi/cloud/upload/token`、`/cloud/import` | 上传/导入云盘 | 写操作，单独开关 |

## 字段补全建议
- 歌单卡片：补 `description`、`playCount`、`creator{id,name}`、`updateTime`、`subscribed`。
- 用户：补 `vipRights`（细分黑胶/VIP）、`createTime`、`followed`、`mutual`。
- 评论：补 `replyCount`、`beReplied` / `parentCommentId`，前端才能画楼中楼。
- 曲目：已有 `mv` / `albumId` / `artistId` / `artists` / `quality`；下载/授权状态需单独接。
- 通知：`/pl/count` 的 `msg` 与 `comment/event/follow/notice/forward` 应拆开展示，不要只显示一个总数。

## 不建议现在做
- 一起听：REST 只有房间状态/心跳，真正同步依赖实时通道，第三方可靠性低。
- 完整客户端登录 / 设备注册：只对 NCBL 日志有用，已单独隔离。

## 已实现的新增数据槽（前端可直接绑定）
| 数据槽 | 请求 key | 形状 |
|---|---|---|
| `data.listenTotal` | `listen:total` | `{ seconds }` |
| `data.listenRealtime` | `listen:week` / `listen:month` | `{ type,startTime,endTime,userNickName,listenTimeDistribution,weekFriends,weekToday }` |
| `data.listenRank` | `listenRank:month` / `listenRank:week` | `{ type,startTime,endTime,songCount,items:[{id,name,artist,album,cover,playCount,artists[]}] }` |
| `data.notifications` | `notifications` | `{ msg,comment,event,follow,notice,forward,newProgramCount,total }` |
| `data.userFollows` | `userFollows:<uid>[:<offset>]` | `{ userId,offset,more,items:[{id,name,avatar,signature,gender,followed,mutual,vipType}] }` |
| `data.userFolloweds` | `userFolloweds:<uid>[:<offset>]` | 同 `userFollows` |
| `data.userLevel` | `userLevel` | `{ level,nowPlayCount,nextPlayCount,nowLoginCount,nextLoginCount,progress,info }` |
| `data.userSubcount` | `userSubcount` | `{ artistCount,createdPlaylistCount,subPlaylistCount,mvCount,djRadioCount }` |
| `data.commentReplies` | `commentReplies:<songId>:<commentId>[:<before>]` | `{ songId,commentId,before,more,items:[评论字段] }` |
| `data.lyrics` | `lyrics:<songId>` | 句子表 `[{t,text,tr}]`；同槽的 `data.lyricsTrackId` 标明是哪首，`data.lyricsPureMusic` 为 true 表示纯音乐（空歌词是正确结果，界面说「纯音乐，请欣赏」） |
| `data.lyricsNew` | `lyricsNew:<songId>` | `{ songId,lrc,tlyric,yrc,klyric,romalrc,lines:[{t,text,tr}] }` |
| `data.searchSuggest` | `searchSuggest:<query>` | `{ query,order[],songs:[{id,name,artist,album}],artists:[{id,name,cover}],albums:[{id,name,artist,artistId}],playlists:[{id,name,cover,count}] }`；最新请求的查询优先，不落盘 |
| `data.newSongs` | `discover:newSongs` | `[Track]`（`cover` 为原图 URL） |
| `data.newAlbums` | `discover:newAlbums` | `[{ id,name,cover,artist,artistId,publishTime,size }]` |
| `data.highqualityTags` | `discover:highqualityTags` | `[{ name,hot }]` |
| `data.highquality` | `discover:highquality:<cat>[:<before>]` | `{ cat,before,nextBefore,more,items:[{id,name,cover,count,plays,description,creator}] }`；`cat=全部` 表示不筛分类，`before` 是本次请求游标（首页 0），`nextBefore` 传下一页 |
| `data.recommendedMvs` | `discover:mvs` | `[{ id,name,cover,artist,artistId,duration,playCount,reason }]` |
| `data.recommendedPrograms` | `discover:programs` | `[Program]` |
| `data.radio` | `radio:<id>[:<offset>]` | `{ id,name,cover,description,dj:{id,name,avatar},programCount,subCount,playCount,offset,more,programs:[Program] }`；`offset=0` 时取详情，翻页复用同 id 详情 |

`Track` 与 `ncmTrack` 同形：`id,name,artist,album,albumId,artistId,artists:[{id,name}],dur,fee,cover,alias,quality,mv,playable,blockedBy,reason`。
`Program` 是节目版 Track：`id` 为主歌 id（真正播放的那首）、`name` 为节目名、`artist` 为主播、`album` 为电台、
`albumId` 与 `artistId` 为 0、`artists:[{id:0,name:主播}]`、`dur` 为主歌秒数；另带
`programId,radioId,serial,createTime,description`。没有可播放主歌 id 的节目不会返回。

调用方式：页面里 `Source.need("<key>")`；数据槽到达后 `dataChanged` 会驱动绑定。
只读信箱、社交列表、歌词、评论回复**不需要写操作**；只有账户级统计需要登录。

实测状态：
- 已用真实账号验证：`listen:total`、`listen:week`、`listenRank:month`、`notifications`、`userLevel`、`userSubcount`、`userFollows`、`userFolloweds`、`lyricsNew`
- `commentReplies` 已用真实楼中楼验证（20 条，分页 `before` 正确）


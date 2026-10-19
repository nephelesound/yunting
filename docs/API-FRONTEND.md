# 前端 API 规格（Store / Source）

这份只列**前端实际能消费的字段**与**还没接但值得接的接口**。原始端点见
`docs/reference/ncm-api-enhanced/module/`；数据形状以 `data` / `personal` 为准。

## Typed SDK

新接口不再直接把 `QJsonObject` / `QVariantMap` 拆进 Store 或页面。SDK 边界在 `src/sdk/`：

- `sdk/song_wiki.h`：typed 模型，每个字段注明 raw JSON path；
- `sdk/song_wiki.cpp`：唯一的 raw JSON 读取与校验点；
- `sdk/song_wiki_service.*`：请求编排，返回 `Result<SongWiki>`；
- `sdk/listen_data.h/.cpp`：听歌足迹（总时长、周/月实时报告、播放排行）的 typed 模型与纯解析；
- `sdk/sdk_result.h`：`ErrorKind` / `Error` / `Result<T>`；
- `samples/`：request、raw response、normalized response、JSON Schema。

`wiki:<songId>` 已走这条 SDK；Store 只把 `SongWiki` 转成现有 QML 兼容 map。新增接口按 `src/sdk/README.md` 的 checklist 做。

## 已有
| 能力 | Store key / 触发 | 主要字段 |
|---|---|---|
| 账户 | `account` | `name` `vip` `vipType` `vipLevel` `level` `listenSongs` `avatar` `avatarPendant` `identityIcon` `background` |
| 用户主页 | `user:<id>` | `id` `name` `avatar` `avatarPendant` `identityIcon` `signature` `followed` `follows` `followeds` `playlistCount` `vipType` `vipLevel` `level` `listenSongs` `playlists[]` |
| 热门评论 | `comments:<songId>` | `userId` `id` `user` `avatar` `avatarPendant` `identityIcon` `vipLevel` `content` `likes` `liked` `time` |
| 评论分页 | `comments:<songId>:<offset>` → `data.commentPage` | 同上 + `parentId` |
| 评论点赞 | `setCommentLiked(kind, ownerId, commentId, liked)` | `kind` 为 `song` / `playlist`（`ownerId` 是歌曲或歌单 id）；乐观更新该线程热评与分页槽的 `liked` / `likes`，失败回滚 |
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
| P1 | 通知列表 | `/weapi/msg/notices`、`/msg/comments`、`/msg/forwards` | 系统通知 / 评论 / 转发（已实现） | `notify:notices` / `notify:comments` / `notify:forwards` |
| P1 | 关注 / 粉丝 | `/api/user/getfollows/<uid>`、`/api/user/getfolloweds/<uid>` | 用户列表，带头像/签名/互关 | `userFollows:<uid>` / `userFolloweds:<uid>` |
| P1 | 用户动态 | `/api/event/get/<uid>` | 动态流（已实现，别人的主页也能看） | `userEvents:<uid>[:<cursor>]` |
| P1 | 用户等级 | `/api/user/level` | 当前等级、听歌数、升级进度 | `userLevel` |
| P1 | 评论回复 | `/weapi/resource/comment/floor/get`（POST） | 楼中楼，含 `parentCommentId` | `commentReplies:<songId>:<commentId>` |
| P1 | 评论抱一抱 | `/weapi/comment/hug/list`、`/comment/hug` | 抱一抱用户列表 | `commentHugs:<songId>:<commentId>` |
| P1 | 音乐百科 | `/api/link/page/parent/relation/construct/info`（明文 POST） | 简介、基本信息、MV、乐评、乐谱、回忆坐标等 block | `wiki:<songId>` |
| P1 | 逐字歌词 | `/api/song/lyric/v1` | `yrc` / `klyric` / 翻译，可做卡拉 OK 和桌面歌词逐字填色 | `lyricsNew:<songId>` |
| P2 | 精品歌单 | `/weapi/top/playlist/highquality`、`/playlist/highquality/tags` | 分类/标签/高赞歌单 | `discover:highquality:<cat>[:<before>]` / `discover:highqualityTags` |
| P2 | 新歌 / 新专辑 | `/weapi/personalized/newsong`、`/album/new`、`/album/newest` | 推荐新歌、新碟 | `discover:newSongs` / `discover:newAlbums` |
| P2 | 歌手/MV/专辑收藏列表 | `/weapi/artist/sublist`、`/album/sublist`、`/cloudvideo/allvideo/sublist` | “我的关注/收藏”页（已实现） | `library:artists` / `library:albums` / `library:mvs` |
| P2 | 歌曲下载地址 | `/weapi/song/enhance/download/url/v1` | `url` `size` `br` `type` `level` | `download:<songId>:<level>` |
| P2 | 歌手粉丝 / 关注数 | `/weapi/artist/fans`、`/artist/follow/count` | 歌手社交信息 | `artistSocial:<id>` |
| P3 | 播客 / 电台 | `/weapi/dj/detail`、`/dj/program/detail`、`/dj/sublist` | 电台与节目 | `radio:<id>[:<offset>]` |
| P3 | 云盘写入 | `/weapi/cloud/upload/token`、`/cloud/import` | 上传/导入云盘 | 写操作，单独开关 |

## 字段补全建议
- 歌单卡片：补 `description`、`playCount`、`creator{id,name}`、`updateTime`、`subscribed`。
- 用户：已补 `vipLevel`：`svip` 为黑胶 SVIP，`vip` 为黑胶 VIP，空串为无会员；`createTime`、`followed`、`mutual` 也已接。
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
| `data.userFollows` | `userFollows:<uid>[:<offset>]` | `{ userId,offset,more,items:[{id,name,avatar,signature,gender,followed,mutual,vipType,vipLevel}] }` |
| `data.userFolloweds` | `userFolloweds:<uid>[:<offset>]` | 同 `userFollows` |
| `data.userLevel` | `userLevel` | `{ level,nowPlayCount,nextPlayCount,nowLoginCount,nextLoginCount,progress,info }` |
| `data.userSubcount` | `userSubcount` | `{ artistCount,createdPlaylistCount,subPlaylistCount,mvCount,djRadioCount }` |
| `data.commentReplies` | `commentReplies:<songId>:<commentId>[:<before>]` | `{ songId,commentId,before,more,items:[评论字段] }` |
| `data.artist` | `artist:<id>` | 按 id 存：`{ "<artistId>": { id,name,cover,alias,description,followed,musicSize,albumSize,tracks[],albums[] } }`，只留最近 8 个 id |
| `data.album` | `album:<id>` | 按 id 存：`{ "<albumId>": { id,name,artist,artistId,cover,description,company,subscribed,publishTime,tracks[] } }`，只留最近 8 个 id |
| `data.user` | `user:<id>` | 按 id 存：`{ "<uid>": { id,name,avatar,signature,followed,follows,followeds,playlistCount,vipType,vipLevel,level,listenSongs,playlists[] } }`，只留最近 8 个 id；隐私拒绝时该项带 `restricted` / `message` |
| `data.mv` | `mv:<id>` | 按 id 存：`{ "<mvId>": { id,name,cover,artist,artistId,duration,playCount,subCount,shareCount,commentCount,publishTime,description,subscribed,url,br,size,expires,urlPending } }`，只留最近 8 个 id；每次打开都重新取签名地址 |
| `data.wiki` | `wiki:<songId>` | 按 id 存：`{ "<songId>": { songId,loaded,songName,description,memory,chorus,basic[],creators[],mvs[],reviews[],sheets[],entertainments[],awards[] } }`，只留最近 8 个 id |
| `data.similarSongs` | `similarSongs:<songId>` | `{ songId,tracks:[Track] }`：一首歌的相似歌曲（正在播放页「相似」栏）；槽里只放最近要的那首 |
| `data.similarPlaylists` | `similarPlaylists:<songId>` | `{ songId,items:[{id,name,cover,count,…}] }`：同上，相似歌单 |
| `data.defaultSearch` | `defaultSearch` | `{ showKeyword,realKeyword,searchType,action,alg,imageUrl }`：搜索框空着时显示 `showKeyword`，回车搜 `realKeyword` |
| `data.song` | `song:<id>` | 按 id 存：`{ "<songId>": Track }`，只留最近 8 个 id；只知道 id 时（打开链接、下载）用它取曲目 |
| `data.lyrics` | `lyrics:<songId>` | 按歌存：`{ "<songId>": { lines:[{t,text,tr}], pureMusic } }`，只留最近 8 首；`pureMusic` 为 true 表示纯音乐（空歌词是正确结果，界面说「纯音乐，请欣赏」）。几处同时要不同歌的歌词各取各的 |
| `data.lyricsNew` | `lyricsNew:<songId>` | `{ songId,lrc,tlyric,yrc,klyric,romalrc,lines:[{t,text,tr}] }` |
| `data.searchSuggest` | `searchSuggest:<query>` | `{ query,order[],songs:[{id,name,artist,album}],artists:[{id,name,cover}],albums:[{id,name,artist,artistId}],playlists:[{id,name,cover,count}] }`；最新请求的查询优先，不落盘 |
| `data.newSongs` | `discover:newSongs` | `[Track]` |
| `data.newAlbums` | `discover:newAlbums` | `[{ id,name,cover,artist,artistId,publishTime,size }]` |
| `data.highqualityTags` | `discover:highqualityTags` | `[{ name,hot }]` |
| `data.highquality` | `discover:highquality:<cat>[:<next>]` | 分页：`{ cat,more,next,items:[{id,name,cover,count,plays,description,creator}] }`；`cat=全部` 表示不筛分类 |
| `data.recommendedMvs` | `discover:mvs` | `[{ id,name,cover,artist,artistId,duration,playCount,reason }]` |
| `data.recommendedPrograms` | `discover:programs` | `[Program]` |
| `data.radio` | `radio:<id>[:<next>]` | 分页：`{ id,name,cover,description,dj:{id,name,avatar},programCount,subCount,playCount,more,next,programs:[Program] }`；翻页复用同 id 的详情 |
| `data.userEvents` | `userEvents:<uid>[:<next>]` | 分页：`{ userId,more,next,items:[{id,action,text,time,user,resource,likes,comments,shares}] }`；隐私拒绝时 `items:[]` 另带 `restricted` / `message` |
| `personal.savedArtists` | `library:artists[:<next>]` | 分页：`{ more,next,count,items:[{id,name,cover,alias,albums,mvs}] }` |
| `personal.savedAlbums` | `library:albums[:<next>]` | 分页：`{ more,next,count,items:[{id,name,cover,artist,artistId,size}] }` |
| `personal.savedMvs` | `library:mvs[:<next>]` | 分页：`{ more,next,count,items:[{kind,id,vid,name,cover,artist,duration,playCount}] }`；`kind=mv` 时 `id` 为数字、`vid` 为空；`kind=video` 时 `id` 为 0、`vid` 为字符串 |
| `data.notices` | `notify:notices[:<next>]` | 分页：`{ more,next,items:[{id,time,user,text,resource}] }` |
| `data.commentNotices` | `notify:comments[:<next>]` | 分页：`{ more,next,total,items:[{id,time,user,content,repliedContent,resource}] }` |
| `data.forwards` | `notify:forwards[:<next>]` | 分页：`{ more,next,newCount,items:[{id,time,user,message,resource}] }` |
| `data.dynamicCover` | `dynamicCover:<songId>` | `{ songId,url,width,height }`；没有动态封面或未登录时 `url:""`、尺寸 0，不报错；签名 URL 不写磁盘缓存 |

「分页」的槽：第一页的 key 不带游标；之后每页的 key 在末尾加 `:<next>`，取回的条目**接在同一个槽的 items 后面**
（与关注 / 粉丝、私信一样），`more` 为 false 时没有下一页。`next` 是游标或 offset，由 Store 给，界面原样带回。
这些槽在没取之前是 `null`；`[]` 表示取到了但是空的。

`Track` 由 `sdk::trackToVariantMap()` 生成：`id,name,artist,album,albumId,artistId,artists:[{id,name}],dur,fee,cover,alias,quality,mv,playable,blockedBy,reason`；发现页与节目传 `coverPx=0`，`cover` 保留原图 URL，其余槽位是 120 方图。
`Program` 是节目版 Track：`id` 为主歌 id（真正播放的那首）、`name` 为节目名、`artist` 为主播、`album` 为电台、
`albumId` 与 `artistId` 为 0、`artists:[{id:0,name:主播}]`、`dur` 为主歌秒数；另带
`programId,radioId,serial,createTime,description`。没有可播放主歌 id 的节目不会返回。

`user` = `{ id,name,avatar }`（avatar 是原始 URL，尺寸由界面决定）。`resource` =
`{ kind,id,name,cover,artist }`，`kind` 为 `song|playlist|album|mv|video|artist|program|other`；
`kind=video` 时 `id` 为 0，另带字符串 `vid`。动态、收藏、通知与动态封面都需要登录：
未登录时对应的按需 key 不发请求，也不发失败信号。

调用方式：页面里 `Source.need("<key>")`；数据槽到达后 `dataChanged` 会驱动绑定。
只读信箱、社交列表、歌词、评论回复**不需要写操作**；只有账户级统计需要登录。

实测状态：
- 已用真实账号验证：`listen:total`、`listen:week`、`listenRank:month`、`notifications`、`userLevel`、`userSubcount`、`userFollows`、`userFolloweds`、`lyricsNew`
- `commentReplies` 已用真实楼中楼验证（20 条，分页 `before` 正确）


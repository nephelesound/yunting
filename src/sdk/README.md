# ncm-player SDK

面向客户端/前端的 **typed API 边界**。原则和 Pydantic SDK 相同：请求参数、响应字段、错误分类和字段校验都集中在一处；调用方不直接拆 `QJsonObject`，也不在页面里到处访问 `QVariantMap`。

C++ 没有 Pydantic 的运行时模型反射，所以这里用：

| Pydantic | 本 SDK |
|---|---|
| `BaseModel` | 普通 `struct`，字段有确定默认值 |
| 字段类型校验 | `JsonReader` 统一检查 JSON 类型并带 path 报错 |
| `ValidationError` | `sdk::Error { kind, endpoint, path, code, message }` |
| `model_validate` | `SongWiki::fromApi(...) -> Result<SongWiki>` |
| 请求模型 + `request()` | `SongWikiRequest` + `fetchSongWiki(...)` |
| Optional 字段 | C++ 默认值；可选接口失败不拖垮主接口 |

如果以后需要声明式绑定（少写手写解析），可以再引入 `nlohmann/json` 的 `from_json` 或 `glaze`；当前选择 Qt JSON + typed DTO，是为了不引入第二套 JSON DOM，也不把 Qt 类型藏进第三方宏。

## 层次

```
NcmClient                 传输（cookie、weapi/明文、超时）
  └─ sdk/song_wiki_service  三个端点的编排、错误分类、回调生命周期
       └─ sdk/song_wiki      typed 模型 + 校验/归一化
            └─ Store       缓存 + 现有 QML 兼容 map（legacy adapter）
```

- `song_wiki_service.cpp` 是三段响应的唯一合并点。
- `song_wiki.cpp` 是 raw JSON 的唯一读取点。
- `Store::requestWiki()` 只拿 `Result<SongWiki>`，再调用 `toVariantMap()` 交给旧前端。
- 新前端接入应优先使用 `SongWiki` 或 `toJson()`；不要把 `QJsonObject` 再拆到页面里。

## 文件

- `sdk_result.h`：`Result<T>`、`ErrorKind`、`Error`。
- `sdk_json.h/.cpp`：带 path 的 typed `JsonReader`。
- `song_wiki.h/.cpp`：DTO、解析、归一化、legacy map 适配。
- `song_wiki_service.h/.cpp`：三个请求的组合与回调。
- `discovery.h/.cpp`：推荐新歌、新碟、精品歌单、精品标签、首页 banner 的 typed 模型与请求。
- `radio.h/.cpp`：推荐节目、电台详情、节目详情、电台节目分页的 typed 模型与请求。
- `social.h/.cpp`：用户动态流的 typed 模型、只读请求，以及用户关注/取关写请求（需要登录 cookie，不重试）。
- `feed.h/.cpp`：个性化 MV、独家内容、首页 block 的 typed 模型与请求。
- `video.h/.cpp`：视频时间线、详情、签名播放地址。
- `notification.h/.cpp`：系统通知、评论通知、@我/转发通知（登录 weapi）。
- `library.h/.cpp`：歌手/专辑/MV 收藏列表分页（登录 weapi）。
- `service.h/.cpp`：唯一的 HTTP code/错误/context/JSON 信封 seam，所有 service 共用。
- `samples/`：
  - `song_wiki.request.json`：参数与三个 form 的形态。
  - `song_wiki.raw.json`：三个上游响应的代表样例。
  - `song_wiki.normalized.json`：前端可直接消费的稳定形状。
  - `song_wiki.schema.json`：normalized 响应的 JSON Schema，可用于前端类型生成/校验。
  - `discovery.raw.json` / `discovery.normalized.json`：发现页四类数据。
  - `radio.raw.json` / `radio.normalized.json`：播客/电台数据；节目播放用 `mainSongId` 走现有歌曲取址。
  - `social.raw.json` / `social.normalized.json`：用户动态流。
  - `feed.raw.json` / `feed.normalized.json`：首页/独家/个性化 MV。
  - `video.raw.json` / `video.normalized.json`：视频时间线、详情、播放地址。
  - `notification.raw.json` / `notification.normalized.json`：三类通知。
  - `library.raw.json` / `library.normalized.json`：三类收藏列表。

## 用法

```cpp
#include "sdk/song_wiki_service.h"

ncm::sdk::fetchSongWiki(client, {songId}, this,
    [](ncm::sdk::Result<ncm::sdk::SongWiki> result) {
        if (!result) {
            // result.error.kind / .endpoint / .path / .code / .message
            return;
        }
        const ncm::sdk::SongWiki &wiki = *result.value;
        // wiki.songName / wiki.basic / wiki.creators / ...
    });
```

回调在 Qt 主线程触发一次。传 `this` 作为 context 时，对象销毁后的迟到回包会被丢弃。

发现页和电台使用同一模式：

```cpp
ncm::sdk::fetchNewSongs(client, 10, this, [](auto result) { /* QList<NewSong> */ });
ncm::sdk::fetchNewAlbums(client, 30, 0, "ALL", this, [](auto result) { /* QList<NewAlbum> */ });
ncm::sdk::fetchHighQualityPlaylists(client, "华语", 50, 0, this,
    [](auto result) { /* HighQualityPlaylistPage */ });
ncm::sdk::fetchBanners(client, this, [](auto result) { /* QList<Banner> */ });
ncm::sdk::fetchRecommendedPrograms(client, this, [](auto result) { /* QList<RadioProgram> */ });
ncm::sdk::fetchRadioPrograms(client, radioId, 30, 0, false, this,
    [](auto result) { /* RadioProgramPage */ });
ncm::sdk::fetchUserEvents(client, userId, -1, 20, false, this,
    [](auto result) { /* EventPage */ });
ncm::sdk::fetchRecommendedVideos(client, 0, this, [](auto result) { /* VideoTimelinePage */ });
ncm::sdk::fetchSubscribedArtists(client, 25, 0, this,
    [](auto result) { /* SubscribedArtistPage */ });
```

程序只给出 `mainSongId`；播放地址由现有歌曲取址服务负责，SDK 不重复音频协议。

## 字段约定

- 字符串缺失 -> 空串，不返回 null。
- 数组缺失 -> 空表；显式类型错误（该是 array 却是 string）-> `Parse` 错误。
- `duration` 一律秒；上游毫秒字段在 SDK 内转换。
- `cover`/`avatar` 是原始 URL，不由 SDK 拼 `?param=`；尺寸策略属于 UI。
- `jumpUrl` 保留原始 `orpheus://...`，能解析的 MV id 另外给出。
- `empty=true` 表示接口成功但没有百科 block，不是失败。
- 可选接口（创作者、副歌）失败不会让百科失败；主接口失败才是 `Result` 失败。

## 新增接口的规范

1. 在 `sdk/` 建 `xxx.h/.cpp`：请求 struct、响应 struct、每个字段注明 raw JSON path。
2. 所有解析只写在该 `.cpp`，用 `JsonReader`；禁止在 Store/QML 读取 raw JSON。
3. 网络编排放在 `xxx_service.cpp`，返回 `Result<T>`；错误必须有 `endpoint`，解析错误必须有 `path`。
4. 加一个 typed parser 测试；再决定是否加 Store 缓存层。
5. 在 `samples/` 放 request、raw、normalized 三份 JSON。
6. 旧前端若仍吃 `QVariantMap`，只在边界写 `toVariantMap()`，不让 map 进入 SDK。

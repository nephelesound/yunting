.pragma library

// 匿名替身快照（2026-10-17），由 `scripts/snapshot.py --anonymous` 生成后入库：只有公开数据，没有账号。
// 没有个人快照时 make 把它复制成 Personal.js，测试与截图不必联网、不必登录

var account = null

var daily = null

var heart = null

var radar = null

var liked = null

var likedIds = []

var mine = []

var recent = null

var cloud = null

var subscribed = []

var playlistDetails = {}

var playlists = [
 {
  "id": 2237551001,
  "name": "粤语传世经典，怀旧是人的本能",
  "cover": "https://p2.music.126.net/PsONIoIzCJ-9gPCAeq9ahw==/19115009649498152.jpg?param=300y300",
  "count": 211,
  "description": "",
  "playCount": 150918464.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 454158611,
  "name": "一曲戏腔极尽风流，一抹笑靥极尽欢颜。",
  "cover": "https://p2.music.126.net/X3lwU-K8ahwkswzKmm_xMA==/3435973846756513.jpg?param=300y300",
  "count": 81,
  "description": "",
  "playCount": 30176608.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 442538213,
  "name": "『纯电音』让你一秒心动的电音精选",
  "cover": "https://p2.music.126.net/wRiwy65LSxv9X43B-zlBMA==/109951163550232716.jpg?param=300y300",
  "count": 481,
  "description": "",
  "playCount": 36388120.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 8055396278,
  "name": "华语经典 | 那些属于我们青春的一首歌曲",
  "cover": "https://p2.music.126.net/n8RctTMYZ4CintnATKQlOA==/109951173019190450.jpg?param=300y300",
  "count": 239,
  "description": "",
  "playCount": 33174148.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 437270067,
  "name": "「粤语」粤语深情，甜瘾温喉",
  "cover": "https://p2.music.126.net/QSQnz1AHB4JCwfp8V3cVPA==/1408474408906157.jpg?param=300y300",
  "count": 93,
  "description": "",
  "playCount": 21434790.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 919939187,
  "name": "想开家茶馆，放首民谣给从前",
  "cover": "https://p2.music.126.net/OTZ7izmTCziHI19UNogcLQ==/109951163050828782.jpg?param=300y300",
  "count": 29,
  "description": "",
  "playCount": 21229044.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 130788961,
  "name": "【怀疑耳机坏了系列】耳机你对耳朵做了什么",
  "cover": "https://p2.music.126.net/np88MU-d3uL8iP1P0R4Tsw==/109951166685344262.jpg?param=300y300",
  "count": 47,
  "description": "",
  "playCount": 33416600.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 8904187604,
  "name": "超火rap|一定要听的顶级说唱",
  "cover": "https://p2.music.126.net/ObgkcuElyoZwxppjuPOYOw==/109951169170452068.jpg?param=300y300",
  "count": 109,
  "description": "",
  "playCount": 44949628.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 2088799380,
  "name": "日系温柔女声| 沐浴在歌声中的暖阳下",
  "cover": "https://p2.music.126.net/Y2nqa5UA22IQzoOX6cWIpg==/109951164395932097.jpg?param=300y300",
  "count": 123,
  "description": "",
  "playCount": 22652096.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 750115024,
  "name": "这节奏，听着就想跳舞！",
  "cover": "https://p2.music.126.net/qWs57cQp_jORhw5PjoFvJg==/109951162939753075.jpg?param=300y300",
  "count": 53,
  "description": "",
  "playCount": 39733632.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 2340621859,
  "name": "国电新势力 | 本土电子音域的多元碰撞",
  "cover": "https://p2.music.126.net/D12IvT26PxlX_k4VLwmYDw==/109951172460182483.jpg?param=300y300",
  "count": 40,
  "description": "",
  "playCount": 25279464.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 },
 {
  "id": 2015397679,
  "name": "适合努力学习英语时听的歌曲",
  "cover": "https://p2.music.126.net/Ak2mqnmim2sFjxKAYnRb-A==/109951163085080176.jpg?param=300y300",
  "count": 80,
  "description": "",
  "playCount": 31752930.0,
  "creatorId": 0,
  "creatorName": "",
  "updateTime": 0,
  "subscribed": false,
  "reason": "",
  "tracks": []
 }
]

var fm = {
 "tracks": [
  {
   "id": 3441284304,
   "name": "20 Min*liluzivert采样*BGM x jerseyclub* beat",
   "artist": "prodjaded",
   "artistId": 121336893,
   "artists": [
    {
     "id": 121336893,
     "name": "prodjaded"
    }
   ],
   "albumId": 400628008,
   "album": "20 Min*liluzivert采样*BGM x jerseyclub* beat",
   "dur": 148,
   "fee": 0,
   "cover": "http://p2.music.126.net/UL92716NSTilPZQ9-ve_eQ==/109951174006509431.jpg?param=120y120",
   "alias": "",
   "quality": "",
   "mv": 0,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  }
 ]
}

var similar = {
 "seed": {
  "id": 287398,
  "name": "我不难过",
  "artist": "孙燕姿",
  "artistId": 9272,
  "artists": [
   {
    "id": 9272,
    "name": "孙燕姿"
   }
  ],
  "albumId": 28539,
  "album": "未完成",
  "dur": 320,
  "fee": 1,
  "cover": "http://p2.music.126.net/Pzxy6py26NgNmhJjYMf2RQ==/109951169724216323.jpg?param=120y120",
  "alias": "",
  "quality": "lossless",
  "mv": 5293234,
  "playable": false,
  "blockedBy": "vip",
  "reason": ""
 },
 "tracks": [
  {
   "id": 2640844059,
   "name": "慢慢离开你",
   "artist": "金润吉",
   "artistId": 905426,
   "artists": [
    {
     "id": 905426,
     "name": "金润吉"
    }
   ],
   "albumId": 252091741,
   "album": "慢慢离开你",
   "dur": 231,
   "fee": 8,
   "cover": "https://p2.music.126.net/mmlWL5zMvScknPj8nRj9kA==/109951170088065162.jpg?param=120y120",
   "alias": "",
   "quality": "hires",
   "mv": 0,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  },
  {
   "id": 3404238777,
   "name": "周旋",
   "artist": "王以太 / 艾热 AIR",
   "artistId": 12236125,
   "artists": [
    {
     "id": 12236125,
     "name": "王以太"
    },
    {
     "id": 1203045,
     "name": "艾热 AIR"
    }
   ],
   "albumId": 386145067,
   "album": "太热爱",
   "dur": 290,
   "fee": 8,
   "cover": "https://p2.music.126.net/rDv1qod6MDsYcEHKtxCdAA==/109951173532089932.jpg?param=120y120",
   "alias": "",
   "quality": "lossless",
   "mv": 0,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  },
  {
   "id": 3342319503,
   "name": "明知故犯",
   "artist": "Max李玄",
   "artistId": 57539469,
   "artists": [
    {
     "id": 57539469,
     "name": "Max李玄"
    }
   ],
   "albumId": 357748854,
   "album": "失温",
   "dur": 166,
   "fee": 8,
   "cover": "https://p1.music.126.net/H2DFRdwP2y5SuCAn-Tuikw==/109951172568091306.jpg?param=120y120",
   "alias": "You know that I feel bad",
   "quality": "lossless",
   "mv": 0,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  },
  {
   "id": 2108845007,
   "name": "讴歌",
   "artist": "Vansdaddy",
   "artistId": 35009085,
   "artists": [
    {
     "id": 35009085,
     "name": "Vansdaddy"
    }
   ],
   "albumId": 181194681,
   "album": "讴歌",
   "dur": 135,
   "fee": 8,
   "cover": "https://p1.music.126.net/xfPdDoM-Kk14BFdJT7Qxgw==/109951169164991342.jpg?param=120y120",
   "alias": "Acura Integurl remix",
   "quality": "lossless",
   "mv": 0,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  },
  {
   "id": 285546,
   "name": "讨厌",
   "artist": "芮恩",
   "artistId": 9225,
   "artists": [
    {
     "id": 9225,
     "name": "芮恩"
    }
   ],
   "albumId": 28365,
   "album": "芮恩 Rui∑n",
   "dur": 257,
   "fee": 0,
   "cover": "https://p1.music.126.net/rXr3zuC6nqYrrxfmF6X-oA==/109951169531259501.jpg?param=120y120",
   "alias": "",
   "quality": "lossless",
   "mv": 5276032,
   "playable": true,
   "blockedBy": "",
   "reason": ""
  }
 ]
}

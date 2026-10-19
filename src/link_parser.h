#pragma once
// 打开链接的纯解析：orpheus:// 与 music.163.com 分享链接 → { kind, id }。歌曲链接的意思是放这首歌。
// 不联网、不依赖 GUI；不是本应用能直接打开的目标时 kind = None，调用方只管忽略。
#include <QString>
#include <QtGlobal>

enum class LinkKind { None, Song, Playlist, Album, Artist, User, Mv };

struct OpenLink {
    LinkKind kind = LinkKind::None;
    qint64 id = 0;

    bool valid() const { return kind != LinkKind::None && id > 0; }
};

// 解析一个字符串；识别不了、id 不为正、或不是网易云链接都返回 kind = None。
OpenLink parseOpenLink(const QString &text);

// QML 信号里的类型名（song/playlist/album/artist/user/mv）；None 为空串。
QString linkKindName(LinkKind kind);

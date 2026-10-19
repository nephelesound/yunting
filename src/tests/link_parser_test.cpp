// 链接解析的行为测试：每个真实格式、坏 base64/JSON、未知类型与非网易域名。
#include "../link_parser.h"

#include <QTest>

class LinkParserTest : public QObject {
    Q_OBJECT

private slots:
    void orpheusSongPayloadPlays()
    {
        const OpenLink link = parseOpenLink(
            QStringLiteral("orpheus://eyJ0eXBlIjoic29uZyIsImlkIjoiMTIzIiwiY21kIjoicGxheSJ9"));
        QVERIFY(link.valid());
        QCOMPARE(link.kind, LinkKind::Song);
        QCOMPARE(link.id, 123);
    }

    void orpheusAlbumAndPlaylistDoNotPlay()
    {
        const OpenLink album = parseOpenLink(
            QStringLiteral("orpheus://eyJ0eXBlIjoiYWxidW0iLCJpZCI6IjQ1NiIsImNtZCI6IiJ9"));
        QCOMPARE(album.kind, LinkKind::Album);
        QCOMPARE(album.id, 456);

        const OpenLink playlist = parseOpenLink(
            QStringLiteral("orpheus://eyJ0eXBlIjoicGxheWxpc3QiLCJpZCI6Ijc4OSJ9"));
        QCOMPARE(playlist.kind, LinkKind::Playlist);
        QCOMPARE(playlist.id, 789);
    }

    // 真实链接可能带末尾 /、省略 padding 或百分号编码；JSON 里的 id 也可能是数字。
    void orpheusPayloadAcceptsTrailingSlashPaddingAndNumericId()
    {
        const OpenLink padded = parseOpenLink(
            QStringLiteral("orpheus://eyJ0eXBlIjoiYXJ0aXN0IiwiaWQiOiIyMjg1NDcifQ%3D%3D/"));
        QCOMPARE(padded.kind, LinkKind::Artist);
        QCOMPARE(padded.id, 228547);

        const OpenLink numeric = parseOpenLink(
            QStringLiteral("orpheus://eyJ0eXBlIjoic29uZyIsImlkIjozMjEsImNtZCI6InBsYXkifQ"));
        QCOMPARE(numeric.kind, LinkKind::Song);
        QCOMPARE(numeric.id, 321);
    }

    // 歌词创作者信息里的 orpheus://nm/<kind>/<page>?id=… 形式。
    void orpheusArtistPathLink()
    {
        const OpenLink link = parseOpenLink(
            QStringLiteral("orpheus://nm/artist/home?id=228547&type=artist"));
        QVERIFY(link.valid());
        QCOMPARE(link.kind, LinkKind::Artist);
        QCOMPARE(link.id, 228547);
    }

    void orpheusMalformedPayloadsAreIgnored()
    {
        QCOMPARE(parseOpenLink(QStringLiteral("orpheus://")).kind, LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("orpheus://!!!not-base64!!!")).kind, LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("orpheus://bm90IGpzb24")).kind, LinkKind::None);
        // 未知类型与 id 不为正都不打开
        QCOMPARE(parseOpenLink(
                     QStringLiteral("orpheus://eyJ0eXBlIjoicmFkaW8iLCJpZCI6IjEifQ")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(
                     QStringLiteral("orpheus://eyJ0eXBlIjoic29uZyIsImlkIjoiMCJ9")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("orpheus://nm/artist/home?id=abc")).kind,
                 LinkKind::None);
    }

    void shareSongLinkPlays()
    {
        const OpenLink link = parseOpenLink(
            QStringLiteral("https://music.163.com/song?id=123&userid=42&uct2=abc"));
        QVERIFY(link.valid());
        QCOMPARE(link.kind, LinkKind::Song);
        QCOMPARE(link.id, 123);
    }

    void shareFragmentPlaylistLink()
    {
        const OpenLink link = parseOpenLink(
            QStringLiteral("https://music.163.com/#/playlist?id=456&userid=42"));
        QCOMPARE(link.kind, LinkKind::Playlist);
        QCOMPARE(link.id, 456);
    }

    void shareMobileAlbumLink()
    {
        const OpenLink link = parseOpenLink(
            QStringLiteral("https://y.music.163.com/m/album?id=789&userid=42"));
        QCOMPARE(link.kind, LinkKind::Album);
        QCOMPARE(link.id, 789);
    }

    void shareArtistUserAndMvLinks()
    {
        const OpenLink artist = parseOpenLink(
            QStringLiteral("https://music.163.com/artist?id=228547"));
        QCOMPARE(artist.kind, LinkKind::Artist);
        QCOMPARE(artist.id, 228547);

        const OpenLink user = parseOpenLink(
            QStringLiteral("https://music.163.com/#/user/home?id=99&uct2=abc"));
        QCOMPARE(user.kind, LinkKind::User);
        QCOMPARE(user.id, 99);

        const OpenLink mv = parseOpenLink(
            QStringLiteral("https://y.music.163.com/m/mv?id=55"));
        QCOMPARE(mv.kind, LinkKind::Mv);
        QCOMPARE(mv.id, 55);
    }

    void nonNeteaseAndUnknownLinksAreIgnored()
    {
        QCOMPARE(parseOpenLink(QStringLiteral("https://example.com/song?id=1")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("https://music.163.com.evil.test/song?id=1")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("https://music.163.com/radio?id=1")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("https://music.163.com/song?id=0")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("https://music.163.com/song?id=-1")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("https://music.163.com/song")).kind,
                 LinkKind::None);
        QCOMPARE(parseOpenLink(QStringLiteral("not a link")).kind, LinkKind::None);
    }

    void kindNamesMatchTheQmlContract()
    {
        QCOMPARE(linkKindName(LinkKind::Song), QStringLiteral("song"));
        QCOMPARE(linkKindName(LinkKind::Playlist), QStringLiteral("playlist"));
        QCOMPARE(linkKindName(LinkKind::Album), QStringLiteral("album"));
        QCOMPARE(linkKindName(LinkKind::Artist), QStringLiteral("artist"));
        QCOMPARE(linkKindName(LinkKind::User), QStringLiteral("user"));
        QCOMPARE(linkKindName(LinkKind::Mv), QStringLiteral("mv"));
        QVERIFY(linkKindName(LinkKind::None).isEmpty());
    }

    // 剪贴板与命令行拿到的是一句话：找出第一个网易云链接，短链单独交出来
    void findsLinksInsideShareText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<qint64>("id");
        QTest::addColumn<QString>("shortUrl");
        QTest::newRow("phone share")
            << QStringLiteral("分享王菲的单曲《红豆》: http://163cn.tv/xLb2c3 (来自@网易云音乐)")
            << QString() << qint64(0) << QStringLiteral("http://163cn.tv/xLb2c3");
        QTest::newRow("web link in a sentence")
            << QStringLiteral("听听这首：https://music.163.com/song?id=186001&userid=1。")
            << QStringLiteral("song") << qint64(186001) << QString();
        QTest::newRow("trailing ascii period")
            << QStringLiteral("see https://y.music.163.com/m/playlist?id=24381616.")
            << QStringLiteral("playlist") << qint64(24381616) << QString();
        QTest::newRow("other site first")
            << QStringLiteral("https://example.com/song?id=1 https://music.163.com/#/album?id=32311")
            << QStringLiteral("album") << qint64(32311) << QString();
        QTest::newRow("no netease link")
            << QStringLiteral("周末一起去听音乐会吧 https://example.com/x")
            << QString() << qint64(0) << QString();
        // 网页「在客户端打开」实际拉起时的参数（多一个 channel 字段）
        QTest::newRow("web open-in-client")
            << QStringLiteral("orpheus://eyJ0eXBlIjoicGxheWxpc3QiLCJpZCI6IjI4NjgwMzE5MzYiLCJjbWQiOiJwbGF5IiwiY2hhbm5lbCI6IndlYnNldCJ9")
            << QStringLiteral("playlist") << qint64(2868031936) << QString();
        QTest::newRow("standard base64 with slash and padding")
            << QStringLiteral("orpheus://eyJ0eXBlIjoic29uZyIsImlkIjoiMTAiLCJjbWQiOiJwbGF5IiwibmFtZSI6Ing/In0=")
            << QStringLiteral("song") << qint64(10) << QString();
        QTest::newRow("in-page anchor keeps the query")
            << QStringLiteral("https://music.163.com/song?id=186001#comments")
            << QStringLiteral("song") << qint64(186001) << QString();
        QTest::newRow("markdown brackets")
            << QStringLiteral("[听这首](https://music.163.com/song?id=186001) 或 [https://music.163.com/album?id=32311]")
            << QStringLiteral("song") << qint64(186001) << QString();
    }
    void findsLinksInsideShareText()
    {
        QFETCH(QString, text);
        QFETCH(QString, kind);
        QFETCH(qint64, id);
        QFETCH(QString, shortUrl);
        const FoundLink found = findLink(text);
        QCOMPARE(linkKindName(found.link.kind), kind);
        QCOMPARE(found.link.id, id);
        QCOMPARE(found.shortUrl.toString(), shortUrl);
        QCOMPARE(found.found(), !kind.isEmpty() || !shortUrl.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LinkParserTest)
#include "link_parser_test.moc"

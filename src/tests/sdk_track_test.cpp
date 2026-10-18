// Typed Track / PlaylistCard boundary: the golden fixture was recorded from the
// pre-migration normalize.cpp (ncmTrack/ncmTracks/ncmPlaylistCard), so these
// tests freeze the QML map keys, order and value types across the migration.
#include "sdk/track.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkTrackTest : public QObject {
    Q_OBJECT

    static QJsonObject fixture()
    {
        const QString path = QFINDTESTDATA("track_golden.json");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    // QJsonValue::toVariant turns integral JSON numbers into qlonglong; the fixture
    // was serialized from QVariant maps whose numbers were all double, so restore
    // that type before the exact-type comparison.
    static QVariant canonical(const QVariant &value)
    {
        if (value.metaType() == QMetaType::fromType<qlonglong>()
            || value.metaType() == QMetaType::fromType<int>()) {
            return QVariant(value.toDouble());
        }
        if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
            QVariantMap out;
            const QVariantMap source = value.toMap();
            for (auto it = source.cbegin(); it != source.cend(); ++it)
                out.insert(it.key(), canonical(it.value()));
            return out;
        }
        if (value.metaType() == QMetaType::fromType<QVariantList>()) {
            QVariantList out;
            const QVariantList source = value.toList();
            out.reserve(source.size());
            for (const QVariant &item : source)
                out.append(canonical(item));
            return out;
        }
        return value;
    }

    static bool sameTypes(const QVariant &actual, const QVariant &expected)
    {
        if (actual.metaType() != expected.metaType())
            return false;
        if (expected.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap a = actual.toMap();
            const QVariantMap e = expected.toMap();
            if (a.keys() != e.keys())
                return false;
            for (auto it = e.cbegin(); it != e.cend(); ++it)
                if (!sameTypes(a.value(it.key()), it.value()))
                    return false;
        } else if (expected.metaType() == QMetaType::fromType<QVariantList>()) {
            const QVariantList a = actual.toList();
            const QVariantList e = expected.toList();
            if (a.size() != e.size())
                return false;
            for (qsizetype i = 0; i < e.size(); ++i)
                if (!sameTypes(a.at(i), e.at(i)))
                    return false;
        }
        return true;
    }

private slots:
    void goldenTrackMaps()
    {
        const QJsonObject root = fixture();
        QVERIFY(!root.isEmpty());
        for (const QJsonValue &value : root.value("tracks").toArray()) {
            const QJsonObject test = value.toObject();
            const auto parsed = parseTrack(test.value("song").toObject(),
                                           test.value("privilege").toObject());
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            const QVariantMap expected =
                canonical(test.value("expected").toObject().toVariantMap()).toMap();
            const QVariantMap actual =
                trackToVariantMap(*parsed.value, test.value("reason").toString());
            QVERIFY2(actual == expected, qPrintable(test.value("name").toString()));
            QVERIFY2(sameTypes(actual, expected), qPrintable(test.value("name").toString()));
        }
    }

    void goldenTrackLists()
    {
        const QJsonObject root = fixture();
        QVERIFY(!root.isEmpty());
        for (const QJsonValue &value : root.value("trackLists").toArray()) {
            const QJsonObject test = value.toObject();
            const auto parsed = parseTracks(test.value("songs").toArray(),
                                            test.value("privileges").toArray());
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            const QVariantList expected =
                canonical(test.value("expected").toArray().toVariantList()).toList();
            const QVariantList actual = tracksToVariantList(*parsed.value);
            QVERIFY2(actual == expected, qPrintable(test.value("name").toString()));
            QVERIFY2(sameTypes(actual, expected), qPrintable(test.value("name").toString()));
        }
    }

    void goldenPlaylistCards()
    {
        const QJsonObject root = fixture();
        QVERIFY(!root.isEmpty());
        for (const QJsonValue &value : root.value("cards").toArray()) {
            const QJsonObject test = value.toObject();
            const QByteArray coverKey = test.value("coverKey").toString().toLatin1();
            const auto parsed = parsePlaylistCard(test.value("playlist").toObject(),
                                                  coverKey.constData());
            QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
            const QVariantMap expected =
                canonical(test.value("expected").toObject().toVariantMap()).toMap();
            const QVariantMap actual = playlistCardToVariantMap(*parsed.value);
            QVERIFY2(actual == expected, qPrintable(test.value("name").toString()));
            QVERIFY2(sameTypes(actual, expected), qPrintable(test.value("name").toString()));
        }
    }

    // 与旧 ncmTrack 的导航字段一致；typed 模型里 cover 保持原图 URL
    void trackKeepsNavigationFields()
    {
        const QJsonObject song{
            {"id", 1},
            {"name", "晴天"},
            {"ar", QJsonArray{QJsonObject{{"id", 6452}, {"name", "周杰伦"}}}},
            {"al", QJsonObject{{"id", 32311}, {"name", "叶惠美"},
                               {"picUrl", "http://p1.music.126.net/x.jpg"}}},
            {"dt", 269000},
            {"alia", QJsonArray{"晴天 (Live)"}},
            {"hr", QJsonObject{{"br", 999000}}},
        };
        const auto parsed = parseTrack(song);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const Track &track = *parsed.value;
        QCOMPARE(track.artists.size(), 1);
        QCOMPARE(track.artists.first().id, qint64(6452));
        QCOMPARE(track.albumId, qint64(32311));
        QCOMPARE(track.duration, qint64(269));
        QCOMPARE(track.alias, QStringLiteral("晴天 (Live)"));
        QCOMPARE(track.quality, QStringLiteral("hires"));
        QCOMPARE(track.cover, QStringLiteral("http://p1.music.126.net/x.jpg"));

        const QVariantMap t = trackToVariantMap(track);
        QCOMPARE(t.value("artistId").toDouble(), 6452);
        QCOMPARE(t.value("albumId").toDouble(), 32311);
        QCOMPARE(t.value("dur").toDouble(), 269);
        QCOMPARE(t.value("alias").toString(), QStringLiteral("晴天 (Live)"));
        QCOMPARE(t.value("quality").toString(), QStringLiteral("hires"));
        QCOMPARE(t.value("cover").toString(),
                 QStringLiteral("http://p1.music.126.net/x.jpg?param=120y120"));
    }

    // 播放权限来自 privilege（账号相关）：pl>0 可播，st<0 不可播；没有 privilege 才退回 fee。
    void privilegeDecidesPlayability()
    {
        const auto withPrivilege = [](double fee, QJsonObject privilege) {
            QJsonObject song{{"id", 1}, {"name", "某首歌"}, {"fee", fee}};
            song.insert("privilege", privilege);
            const auto parsed = parseTrack(song);
            return parsed ? trackToVariantMap(*parsed.value) : QVariantMap{};
        };

        // 非会员账号：会员曲的 pl 为 0（接口的 cs 是布尔 false，不能当成云盘）
        const QVariantMap vipOnly = withPrivilege(1, {{"id", 1}, {"fee", 1}, {"st", 0}, {"pl", 0}, {"cs", false}});
        QCOMPARE(vipOnly.value("playable").toBool(), false);
        QCOMPARE(vipOnly.value("blockedBy").toString(), QStringLiteral("vip"));

        // 云盘曲：cs 为 true 时即便 pl 为 0 也能播
        const QVariantMap cloudPrivilege = withPrivilege(0, {{"id", 1}, {"fee", 0}, {"st", 0}, {"pl", 0}, {"cs", true}});
        QCOMPARE(cloudPrivilege.value("playable").toBool(), true);
        QCOMPARE(cloudPrivilege.value("blockedBy").toString(), QString());

        // 同一首歌，会员账号的 pl 大于 0
        const QVariantMap vipAccount = withPrivilege(1, {{"id", 1}, {"fee", 1}, {"st", 0}, {"pl", 320000}});
        QCOMPARE(vipAccount.value("playable").toBool(), true);
        QCOMPARE(vipAccount.value("blockedBy").toString(), QString());

        // 付费专辑未购买
        const QVariantMap paid = withPrivilege(4, {{"id", 1}, {"fee", 4}, {"payed", 0}, {"st", 0}, {"pl", 0}});
        QCOMPARE(paid.value("playable").toBool(), false);
        QCOMPARE(paid.value("blockedBy").toString(), QStringLiteral("paid"));

        // 已下架/灰色：st 为负
        QJsonObject gone{{"id", 1}, {"name", "已下架"}, {"fee", 0}};
        gone.insert("privilege", QJsonObject{{"id", 1}, {"fee", 0}, {"st", -200}, {"pl", 0}});
        const auto goneParsed = parseTrack(gone);
        QVERIFY(goneParsed.ok());
        const QVariantMap unavailable = trackToVariantMap(*goneParsed.value);
        QCOMPARE(unavailable.value("playable").toBool(), false);
        QCOMPARE(unavailable.value("blockedBy").toString(), QStringLiteral("unavailable"));
    }

    // 平行数组 privileges 按 id 对上 songs；没对上的歌退回自己的 privilege/fee
    void tracksJoinParallelPrivilegesById()
    {
        const QJsonArray songs{
            QJsonObject{{"id", 11}, {"name", "会员曲"}, {"fee", 1}},
            QJsonObject{{"id", 22}, {"name", "付费曲"}, {"fee", 4}},
            QJsonObject{{"id", 33}, {"name", "无权限信息"}, {"fee", 0}},
        };
        const QJsonArray privileges{
            QJsonObject{{"id", 11}, {"fee", 1}, {"st", 0}, {"pl", 320000}},
            QJsonObject{{"id", 22}, {"fee", 4}, {"payed", 0}, {"st", 0}, {"pl", 0}},
            QJsonObject{{"id", 99}, {"fee", 1}, {"st", 0}, {"pl", 320000}},
        };
        const auto parsed = parseTracks(songs, privileges);
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        const QVariantList tracks = tracksToVariantList(*parsed.value);
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks.at(0).toMap().value("playable").toBool(), true);
        QCOMPARE(tracks.at(1).toMap().value("playable").toBool(), false);
        QCOMPARE(tracks.at(1).toMap().value("blockedBy").toString(), QStringLiteral("paid"));
        QCOMPARE(tracks.at(2).toMap().value("playable").toBool(), true);
    }

    // 没有 privilege 时退回 fee
    void missingPrivilegeFallsBackToFee()
    {
        const auto mapFor = [](double fee) {
            const auto parsed = parseTrack(QJsonObject{{"id", 2}, {"name", "某首歌"},
                                                       {"fee", fee}});
            return parsed ? trackToVariantMap(*parsed.value) : QVariantMap{};
        };

        const QVariantMap paid = mapFor(4);
        QCOMPARE(paid.value("playable").toBool(), false);
        QCOMPARE(paid.value("blockedBy").toString(), QStringLiteral("paid"));

        const QVariantMap vip = mapFor(1);
        QCOMPARE(vip.value("playable").toBool(), false);
        QCOMPARE(vip.value("blockedBy").toString(), QStringLiteral("vip"));

        const QVariantMap free = mapFor(0);
        QCOMPARE(free.value("playable").toBool(), true);
        QCOMPARE(free.value("blockedBy").toString(), QString());
    }

    // 发现页/节目文档里 cover 是原图；队列曲目才补 120 方图
    void coverPolicyKeepsDiscoveryUrlsRaw()
    {
        const auto parsed = parseTrack(QJsonObject{
            {"id", 1}, {"name", "新歌"},
            {"artists", QJsonArray{QJsonObject{{"id", 3}, {"name", "歌手"}}}},
            {"album", QJsonObject{{"id", 4}, {"name", "专辑"},
                                  {"picUrl", "http://cover/new.jpg"}}},
            {"duration", 210500}, {"fee", 0}});
        QVERIFY(parsed.ok());

        const QVariantMap raw = trackToVariantMap(*parsed.value, QString(), 0);
        QCOMPARE(raw.value("cover").toString(), QStringLiteral("http://cover/new.jpg"));
        QCOMPARE(raw.value("dur").toDouble(), 210.0);

        const QVariantMap sized = trackToVariantMap(*parsed.value);
        QCOMPARE(sized.value("cover").toString(),
                 QStringLiteral("http://cover/new.jpg?param=120y120"));
    }

    void playlistCardKeepsRawCoverAndFirstLine()
    {
        const QJsonObject playlist{
            {"id", 5}, {"name", "歌单"},
            {"coverImgUrl", "http://cover/p.jpg"},
            {"description", "第一行\n第二行"},
            {"trackCount", 12},
            {"creator", QJsonObject{{"userId", 9}, {"nickname", "歌单主"}}},
        };
        const auto parsed = parsePlaylistCard(playlist, "coverImgUrl");
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value->cover, QStringLiteral("http://cover/p.jpg"));
        QCOMPARE(parsed.value->description, QStringLiteral("第一行"));
        QCOMPARE(parsed.value->count, qint64(12));
        QCOMPARE(parsed.value->creatorId, qint64(9));

        const QVariantMap card = playlistCardToVariantMap(*parsed.value);
        QCOMPARE(card.value("cover").toString(),
                 QStringLiteral("http://cover/p.jpg?param=300y300"));
        QCOMPARE(card.value("description").toString(), QStringLiteral("第一行"));
    }

    void wrongTypeIsParseError()
    {
        const auto track = parseTrack(QJsonObject{{"id", "not-a-number"}});
        QVERIFY(!track.ok());
        QCOMPARE(track.error.kind, ErrorKind::Parse);
        QVERIFY(track.error.message.contains(QStringLiteral("id")));

        const auto card = parsePlaylistCard(QJsonObject{{"trackCount", "many"}},
                                            "coverImgUrl");
        QVERIFY(!card.ok());
        QCOMPARE(card.error.kind, ErrorKind::Parse);
    }

    // 列表里一首坏歌只丢它自己：每日推荐、歌单不能因为一行数据整张没了
    void malformedRowIsSkippedNotTheList()
    {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("skipped malformed song")));
        const auto tracks = parseTracks(QJsonArray{
            QJsonObject{{"id", 1}, {"name", "好"}},
            QJsonObject{{"id", "not-a-number"}, {"name", "坏"}},
            QJsonObject{{"id", 3}, {"name", "也好"}},
        });
        QVERIFY(tracks.ok());
        QCOMPARE(tracks.value->size(), 2);
        QCOMPARE(tracks.value->at(0).id, 1);
        QCOMPARE(tracks.value->at(1).id, 3);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("skipped malformed playlist card")));
        const auto cards = parsePlaylistCards(QJsonArray{
            QJsonObject{{"id", 1}, {"trackCount", "many"}},
            QJsonObject{{"id", 2}, {"name", "好"}},
        }, "coverImgUrl");
        QVERIFY(cards.ok());
        QCOMPARE(cards.value->size(), 1);
        QCOMPARE(cards.value->at(0).id, 2);
    }
};

QTEST_GUILESS_MAIN(SdkTrackTest)
#include "sdk_track_test.moc"

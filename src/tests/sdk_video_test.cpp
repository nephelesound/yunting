// Typed Video SDK contract: recommendation timeline, detail and signed URLs.
#include "sdk/video.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace ncm::sdk;

class SdkVideoTest : public QObject {
    Q_OBJECT

    static QJsonObject creatorObject()
    {
        return QJsonObject{
            {"userId", 42},
            {"nickname", "创作者"},
            {"avatarUrl", "http://avatar"},
            {"signature", "签名"},
        };
    }

    static QJsonObject urlInfoObject()
    {
        return QJsonObject{
            {"id", "A1B2C3D4"},
            {"url", "http://vod.example/video.mp4?token=a&sign=b"},
            {"size", 45648236.0},
            {"validityTime", 1200},
            {"needPay", false},
            {"r", 1080},
        };
    }

    static QJsonArray groupArray()
    {
        return QJsonArray{
            QJsonObject{{"id", 1}, {"name", "音乐"}},
            QJsonObject{{"id", 2}, {"name", "现场"}},
        };
    }

    static QJsonArray resolutionArray()
    {
        return QJsonArray{
            QJsonObject{{"resolution", 480}, {"size", 1000}},
            QJsonObject{{"resolution", 1080}, {"size", 2000}},
        };
    }

    static QJsonObject timelineDataObject()
    {
        return QJsonObject{
            {"vid", "A1B2C3D4"},
            {"threadId", "R_VI_62_A1B2C3D4"},
            {"title", "视频标题"},
            {"description", "视频简介"},
            {"coverUrl", "http://cover"},
            {"durationms", 213999.0},
            {"previewDurationms", 5999},
            {"playTime", 12345.0},
            {"praisedCount", 7},
            {"commentCount", 8},
            {"shareCount", 9},
            {"praised", true},
            {"subscribed", true},
            {"hasRelatedGameAd", false},
            {"width", 1920},
            {"height", 1080},
            {"previewUrl", "http://preview"},
            {"creator", creatorObject()},
            {"videoGroup", groupArray()},
            {"resolutions", resolutionArray()},
            {"urlInfo", urlInfoObject()},
        };
    }

    static QJsonObject detailDataObject()
    {
        return QJsonObject{
            {"vid", "A1B2C3D4"},
            {"title", "详情标题"},
            {"description", "详情简介"},
            {"coverUrl", "http://detail-cover"},
            {"threadId", "R_VI_62_A1B2C3D4"},
            {"durationms", 213999.0},
            {"publishTime", 1700000000000.0},
            {"playTime", 88},
            {"praisedCount", 11},
            {"commentCount", 12},
            {"shareCount", 13},
            {"subscribeCount", 14},
            {"hasRelatedGameAd", true},
            {"authType", 1},
            {"width", 1920},
            {"height", 1080},
            {"creator", creatorObject()},
            {"videoGroup", groupArray()},
            {"resolutions", resolutionArray()},
        };
    }

    static QJsonObject playUrlObject()
    {
        return QJsonObject{
            {"id", "A1B2C3D4"},
            {"url", "http://vod.example/video.mp4?token=abc&sign=def"},
            {"size", 45648236.0},
            {"validityTime", 1200},
            {"needPay", true},
            {"r", 720},
        };
    }

private slots:
    void parsesTimeline()
    {
        QJsonObject item{
            {"type", 1},
            {"displayed", false},
            {"alg", "special_first_page_rcmd"},
            {"data", timelineDataObject()},
        };
        QJsonObject root{
            {"code", 200},
            {"hasmore", true},
            {"msg", "发现了更多新内容"},
            {"datas", QJsonArray{item}},
        };

        const auto result = parseRecommendedVideos(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->more, true);
        QCOMPARE(result.value->offset, 0);
        QCOMPARE(result.value->items.size(), 1);

        const Video &video = result.value->items.first();
        QCOMPARE(video.vid, QStringLiteral("A1B2C3D4"));
        QCOMPARE(video.type, 1);
        QCOMPARE(video.title, QStringLiteral("视频标题"));
        QCOMPARE(video.description, QStringLiteral("视频简介"));
        QCOMPARE(video.cover, QStringLiteral("http://cover"));
        QCOMPARE(video.threadId, QStringLiteral("R_VI_62_A1B2C3D4"));
        QCOMPARE(video.duration, qint64(213));         // durationms -> seconds
        QCOMPARE(video.previewDuration, qint64(5));    // previewDurationms -> seconds
        QCOMPARE(video.playCount, qint64(12345));
        QCOMPARE(video.praisedCount, qint64(7));
        QCOMPARE(video.commentCount, qint64(8));
        QCOMPARE(video.shareCount, qint64(9));
        QCOMPARE(video.praised, true);
        QCOMPARE(video.subscribed, true);
        QCOMPARE(video.hasRelatedGameAd, false);
        QCOMPARE(video.width, 1920);
        QCOMPARE(video.height, 1080);
        QCOMPARE(video.previewUrl, QStringLiteral("http://preview"));
        QCOMPARE(video.creator.userId, qint64(42));
        QCOMPARE(video.creator.nickname, QStringLiteral("创作者"));
        QCOMPARE(video.creator.avatarUrl, QStringLiteral("http://avatar"));
        QCOMPARE(video.groups.size(), 2);
        QCOMPARE(video.groups.at(1).id, qint64(2));
        QCOMPARE(video.groups.at(1).name, QStringLiteral("现场"));
        QCOMPARE(video.resolutions.size(), 2);
        QCOMPARE(video.resolutions.at(1).resolution, 1080);
        QCOMPARE(video.resolutions.at(1).size, qint64(2000));
        QCOMPARE(video.urlInfo.vid, QStringLiteral("A1B2C3D4"));
        QCOMPARE(video.urlInfo.url,
                 QStringLiteral("http://vod.example/video.mp4?token=a&sign=b"));
        QCOMPARE(video.urlInfo.size, qint64(45648236));
        QCOMPARE(video.urlInfo.validityTime, qint64(1200));
        QCOMPARE(video.urlInfo.needPay, false);
        QCOMPARE(video.urlInfo.resolution, 1080);
    }

    void parsesTimelineMinimalItem()
    {
        QJsonObject data{{"vid", "MINIMAL"}, {"title", "只有标题"}};
        QJsonObject root{{"code", 200},
                         {"datas", QJsonArray{QJsonObject{{"data", data}}}}};

        const auto result = parseRecommendedVideos(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->more, false);
        QCOMPARE(result.value->items.size(), 1);
        const Video &video = result.value->items.first();
        QCOMPARE(video.vid, QStringLiteral("MINIMAL"));
        QCOMPARE(video.type, 0);
        QCOMPARE(video.duration, qint64(0));
        QCOMPARE(video.playCount, qint64(0));
        QCOMPARE(video.praised, false);
        QVERIFY(video.groups.isEmpty());
        QVERIFY(video.resolutions.isEmpty());
        QVERIFY(video.urlInfo.url.isEmpty());
    }

    void timelineSkipsNonVideoCards()
    {
        QJsonObject item{{"type", 9}, {"data", QJsonObject{}}};
        QJsonObject root{{"code", 200}, {"datas", QJsonArray{item}}};
        const auto result = parseRecommendedVideos(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->items.isEmpty());
    }

    void timelineMissingDatasIsParseError()
    {
        const auto result = parseRecommendedVideos(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("videotimeline/get"));
        QCOMPARE(result.error.path, QStringLiteral("video.timeline.datas"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing array")));
    }

    void timelineMalformedDatasIsParseError()
    {
        const auto result = parseRecommendedVideos(
            QJsonObject{{"code", 200}, {"datas", QStringLiteral("not-an-array")}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("videotimeline/get"));
        QCOMPARE(result.error.path, QStringLiteral("video.timeline.datas"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected array")));
    }

    void timelineMalformedItemDataIsParseError()
    {
        QJsonObject item{{"type", 1}, {"data", QStringLiteral("not-an-object")}};
        const auto result = parseRecommendedVideos(
            QJsonObject{{"code", 200}, {"datas", QJsonArray{item}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.timeline.datas[0]"));
        QVERIFY(result.error.message.contains(
            QStringLiteral("video.timeline.datas[0].data")));
    }

    void timelineTypedCreatorIsParseError()
    {
        QJsonObject data{{"vid", "A1B2C3D4"}, {"creator", QStringLiteral("not-an-object")}};
        QJsonObject root{{"code", 200},
                         {"datas", QJsonArray{QJsonObject{{"data", data}}}}};
        const auto result = parseRecommendedVideos(root);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.timeline.datas[0].data"));
        QVERIFY(result.error.message.contains(QStringLiteral("creator")));
    }

    void timelineMalformedResolutionsIsParseError()
    {
        QJsonObject data{
            {"vid", "A1B2C3D4"},
            {"resolutions", QJsonArray{QJsonValue(QStringLiteral("bad"))}},
        };
        QJsonObject root{{"code", 200},
                         {"datas", QJsonArray{QJsonObject{{"data", data}}}}};
        const auto result = parseRecommendedVideos(root);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path,
                 QStringLiteral("video.timeline.datas[0].data.resolutions[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected object")));
    }

    void parsesDetail()
    {
        QJsonObject root{
            {"code", 200},
            {"message", "success"},
            {"data", detailDataObject()},
        };

        const auto result = parseVideoDetail(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        const Video &video = *result.value;
        QCOMPARE(video.vid, QStringLiteral("A1B2C3D4"));
        QCOMPARE(video.type, 0);
        QCOMPARE(video.title, QStringLiteral("详情标题"));
        QCOMPARE(video.duration, qint64(213));
        QCOMPARE(video.publishTime, qint64(1700000000000)); // milliseconds
        QCOMPARE(video.playCount, qint64(88));
        QCOMPARE(video.praisedCount, qint64(11));
        QCOMPARE(video.commentCount, qint64(12));
        QCOMPARE(video.shareCount, qint64(13));
        QCOMPARE(video.subscribeCount, qint64(14));
        QCOMPARE(video.hasRelatedGameAd, true);
        QCOMPARE(video.authType, 1);
        QCOMPARE(video.creator.nickname, QStringLiteral("创作者"));
        QCOMPARE(video.groups.size(), 2);
        QCOMPARE(video.resolutions.at(0).size, qint64(1000));
        QVERIFY(video.urlInfo.url.isEmpty());
    }

    void detailWithoutVidIsParseError()
    {
        QJsonObject data{{"title", "没有 id"}};
        QJsonObject root{{"code", 200}, {"data", data}};
        const auto result = parseVideoDetail(root);
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.detail.data.vid"));
    }

    void detailMissingDataIsParseError()
    {
        const auto result = parseVideoDetail(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("cloudvideo/v1/video/detail"));
        QCOMPARE(result.error.path, QStringLiteral("video.detail.data"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing object")));
    }

    void detailMalformedDataIsParseError()
    {
        const auto result = parseVideoDetail(
            QJsonObject{{"code", 200}, {"data", QStringLiteral("not-an-object")}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.detail.data"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected object")));
    }

    void detailTypedFieldIsParseError()
    {
        const auto result = parseVideoDetail(QJsonObject{
            {"code", 200},
            {"data", QJsonObject{{"vid", 1234}}},
        });
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("cloudvideo/v1/video/detail"));
        QCOMPARE(result.error.path, QStringLiteral("video.detail.data"));
        QVERIFY(result.error.message.contains(QStringLiteral("data.vid")));
    }

    void parsesPlayUrls()
    {
        QJsonObject root{
            {"code", 200},
            {"urls", QJsonArray{playUrlObject()}},
        };
        const auto result = parseVideoUrls(root);
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QCOMPARE(result.value->size(), 1);
        const VideoPlayUrl &url = result.value->first();
        QCOMPARE(url.vid, QStringLiteral("A1B2C3D4"));
        QCOMPARE(url.url,
                 QStringLiteral("http://vod.example/video.mp4?token=abc&sign=def"));
        QCOMPARE(url.size, qint64(45648236));
        QCOMPARE(url.validityTime, qint64(1200));
        QCOMPARE(url.needPay, true);
        QCOMPARE(url.resolution, 720);
    }

    void emptyPlayUrlsIsSuccess()
    {
        const auto result = parseVideoUrls(QJsonObject{{"code", 200},
                                                       {"urls", QJsonArray{}}});
        QVERIFY2(result.ok(), qPrintable(result.error.message));
        QVERIFY(result.value->isEmpty());
    }

    void playUrlsMissingArrayIsParseError()
    {
        const auto result = parseVideoUrls(QJsonObject{{"code", 200}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.endpoint, QStringLiteral("cloudvideo/playurl"));
        QCOMPARE(result.error.path, QStringLiteral("video.playUrls.urls"));
        QVERIFY(result.error.message.contains(QStringLiteral("missing array")));
    }

    void playUrlsWrongTypeIsParseError()
    {
        const auto result = parseVideoUrls(
            QJsonObject{{"code", 200}, {"urls", QJsonObject{}}});
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.playUrls.urls"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected array")));
    }

    void playUrlsMalformedEntryIsParseError()
    {
        const auto result = parseVideoUrls(QJsonObject{
            {"code", 200},
            {"urls", QJsonArray{QJsonValue(QStringLiteral("bad"))}},
        });
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.playUrls.urls[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral("expected object")));
    }

    void playUrlsTypedFieldIsParseError()
    {
        QJsonObject url{{"id", "A1B2C3D4"}, {"url", 42}, {"r", 720}};
        const auto result = parseVideoUrls(QJsonObject{
            {"code", 200},
            {"urls", QJsonArray{url}},
        });
        QVERIFY(!result.ok());
        QCOMPARE(result.error.kind, ErrorKind::Parse);
        QCOMPARE(result.error.path, QStringLiteral("video.playUrls.urls[0]"));
        QVERIFY(result.error.message.contains(QStringLiteral(".url")));
    }
};

QTEST_GUILESS_MAIN(SdkVideoTest)
#include "sdk_video_test.moc"

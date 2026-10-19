// 歌曲百科：正在播放页右栏的第三栏，讲这首歌的来历，以及它和你的关系。
// 顺序：简介 → 你和这首歌 → 基本信息（副歌、语种曲风、创作者）→ MV → 获奖 → 乐评 → 乐谱 → 影视综艺。
// 副歌时间是链接：点了从副歌听起（放在表里而不是单独一个按钮，和其他值共用对齐线）。
// 组之间只用 24 的间距与小号组标题分开，不加框、不加分隔线；「标签 — 值」两栏的标签列共用一条对齐线。
// 数据形状见 src/sdk/samples/song_wiki.normalized.json；没有内容的组整组不出现
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Flickable {
    id: pane

    property var wiki   // Source.wiki(songId)

    signal seekRequested(real seconds)

    // 副歌时间：end ≤ start 是接口没给（两个接口之一失败时也是 0）
    readonly property bool hasChorus: Boolean(pane.wiki.chorus) && pane.wiki.chorus.end > pane.wiki.chorus.start
    // 回忆坐标是本人的听歌记录：未登录或没听过时整组不出现
    readonly property var memory: pane.wiki.memory && (pane.wiki.memory.timestamp > 0 || pane.wiki.memory.playCount > 0)
        ? pane.wiki.memory : null
    // 副歌、基本信息与创作者合成一张「标签 — 值」表：值是文字、标签、可点的创作者，或可点的副歌时间
    readonly property var facts: (pane.hasChorus
            ? [{ label: "副歌", text: "", tags: [], people: [], chorus: pane.wiki.chorus }] : [])
        .concat((pane.wiki.basic || []).filter(b => b.content.length > 0 || b.tags.length > 0)
            .map(b => ({ label: b.title, text: b.content, tags: b.tags, people: [], chorus: null })))
        .concat((pane.wiki.creators || []).filter(c => c.people.length > 0)
            .map(c => ({ label: c.role, text: "", tags: [], people: c.people, chorus: null })))

    contentHeight: column.implicitHeight + Tokens.space6
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}
    WheelScroll { flickable: pane }

    component GroupLabel: Label {
        Layout.fillWidth: true
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }
    component Body: Label {
        Layout.fillWidth: true
        font.pixelSize: Tokens.textBase
        lineHeightMode: Text.FixedHeight
        lineHeight: Tokens.lineBase
        wrapMode: Text.Wrap
    }
    component Note: Label {
        Layout.fillWidth: true
        font.pixelSize: Tokens.textSm
        lineHeightMode: Text.FixedHeight
        lineHeight: Tokens.lineSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
        wrapMode: Text.Wrap
    }

    ColumnLayout {
        id: column
        width: pane.width - Tokens.space4   // 右边留出滚动条
        spacing: Tokens.space6

        Body {
            visible: text.length > 0
            text: pane.wiki.description || ""
        }

        ColumnLayout {
            visible: pane.memory !== null
            Layout.fillWidth: true
            spacing: Tokens.space2
            GroupLabel { text: "你和这首歌" }
            Body {
                readonly property var m: pane.memory || {}
                text: m.timestamp > 0
                    ? Qt.formatDate(new Date(m.timestamp), "yyyy年M月d日") + "，" + m.season + "的" + m.period + " " + m.time + "，你第一次听到它"
                    : ""
                visible: text.length > 0
            }
            Body {
                text: pane.memory && pane.memory.playCount > 0 ? "到现在听了 " + Format.count(pane.memory.playCount) + " 次" : ""
                visible: text.length > 0
            }
        }

        ColumnLayout {
            visible: pane.facts.length > 0
            Layout.fillWidth: true
            spacing: Tokens.space2
            GroupLabel { text: "基本信息" }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Tokens.space4
                rowSpacing: Tokens.space2

                Repeater {
                    model: pane.facts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        Layout.alignment: Qt.AlignTop
                        Layout.maximumWidth: Tokens.albumColumn
                        text: modelData.label
                        font.pixelSize: Tokens.textBase
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Tokens.lineBase   // 与右边的值同一行高，首行对齐
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        wrapMode: Text.Wrap
                    }
                }
                Repeater {
                    model: pane.facts
                    delegate: Item {
                        id: value
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        Layout.fillWidth: true
                        implicitHeight: value.modelData.chorus ? chorus.implicitHeight
                            : value.modelData.people.length > 0 ? people.implicitHeight
                            : value.modelData.tags.length > 0 ? tags.implicitHeight : plain.implicitHeight

                        Label {
                            id: plain
                            visible: !value.modelData.chorus && value.modelData.people.length === 0 && value.modelData.tags.length === 0
                            width: parent.width
                            text: value.modelData.text
                            font.pixelSize: Tokens.textBase
                            lineHeightMode: Text.FixedHeight
                            lineHeight: Tokens.lineBase
                            wrapMode: Text.Wrap
                        }
                        LinkText {
                            id: chorus
                            visible: Boolean(value.modelData.chorus)
                            width: parent.width
                            parts: value.modelData.chorus
                                ? [{ text: Format.elapsed(value.modelData.chorus.start) + " – " + Format.elapsed(value.modelData.chorus.end), link: true }]
                                : []
                            font.pixelSize: Tokens.textBase
                            lineHeightMode: Text.FixedHeight
                            lineHeight: Tokens.lineBase
                            ToolTip.text: "从副歌听起"
                            ToolTip.visible: chorus.hoveredPart >= 0
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            onActivated: pane.seekRequested(value.modelData.chorus.start)
                        }
                        // 创作者有歌手页的可点（去歌手页），没有的只是文字
                        LinkText {
                            id: people
                            visible: value.modelData.people.length > 0
                            width: parent.width
                            parts: value.modelData.people.map(p => ({ text: p.name, artistId: p.id }))
                            font.pixelSize: Tokens.textBase
                            lineHeightMode: Text.FixedHeight
                            lineHeight: Tokens.lineBase
                        }
                        Flow {
                            id: tags
                            visible: value.modelData.people.length === 0 && value.modelData.tags.length > 0
                            width: parent.width
                            spacing: Tokens.space2
                            // 与正文行同高，标签在行里垂直居中
                            Repeater {
                                model: value.modelData.tags
                                delegate: Item {
                                    id: tagCell
                                    required property string modelData
                                    implicitWidth: tag.implicitWidth
                                    implicitHeight: plain.implicitHeight
                                    Tag {
                                        id: tag
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tagCell.modelData
                                        maximumWidth: tags.width
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // MV：可点的行，去 MV 页
        ColumnLayout {
            visible: (pane.wiki.mvs || []).length > 0
            Layout.fillWidth: true
            spacing: Tokens.space1
            GroupLabel { text: "MV"; Layout.bottomMargin: Tokens.space1 }
            Repeater {
                model: pane.wiki.mvs || []
                delegate: ResourceRow {
                    id: mvRow
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: -Tokens.space1
                    aspect: 16 / 9
                    cover: mvRow.modelData.cover
                    title: mvRow.modelData.name
                    detail: [mvRow.modelData.duration > 0 ? Format.duration(mvRow.modelData.duration) : "",
                             mvRow.modelData.playCount > 0 ? Format.count(mvRow.modelData.playCount) + " 次播放" : "",
                             mvRow.modelData.publishTime].filter(s => s.length > 0).join("   ")
                    onClicked: Actions.openMv(mvRow.modelData.id)
                }
            }
        }

        ColumnLayout {
            visible: (pane.wiki.awards || []).length > 0
            Layout.fillWidth: true
            spacing: Tokens.space3
            GroupLabel { text: "获奖" }
            Repeater {
                model: pane.wiki.awards || []
                // 奖项名后跟结果（获奖 / 提名）；评奖的名称在下一行
                delegate: ColumnLayout {
                    id: award
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    RowLayout {
                        id: awardLine
                        Layout.fillWidth: true
                        spacing: Tokens.space2
                        Body {
                            Layout.fillWidth: false
                            Layout.maximumWidth: awardLine.width - result.implicitWidth - Tokens.space2
                            text: award.modelData.awardName || award.modelData.title
                        }
                        Tag {
                            id: result
                            Layout.alignment: Qt.AlignVCenter
                            muted: true
                            text: award.modelData.result
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Note {
                        visible: award.modelData.awardName.length > 0 && text.length > 0
                        text: award.modelData.title
                    }
                }
            }
        }

        ColumnLayout {
            visible: (pane.wiki.reviews || []).length > 0
            Layout.fillWidth: true
            spacing: Tokens.space3
            GroupLabel { text: "乐评" }
            Repeater {
                model: pane.wiki.reviews || []
                delegate: ColumnLayout {
                    id: review
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: Tokens.space1
                    Body { text: review.modelData.content }
                    Note {
                        text: [review.modelData.user, review.modelData.likes > 0 ? Format.count(review.modelData.likes) + " 人赞" : ""]
                            .filter(s => s.length > 0).join("   ")
                        visible: text.length > 0
                    }
                }
            }
        }

        ColumnLayout {
            visible: (pane.wiki.sheets || []).length > 0
            Layout.fillWidth: true
            spacing: Tokens.space3
            GroupLabel { text: "乐谱" }
            Repeater {
                model: pane.wiki.sheets || []
                delegate: ColumnLayout {
                    id: sheet
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    Body { text: sheet.modelData.name }
                    Note {
                        readonly property var s: sheet.modelData
                        text: [s.types.join(" / "), s.key ? s.key + " 调" : "", s.bpm > 0 ? s.bpm + " BPM" : "", s.difficulty,
                               s.pages > 0 ? s.pages + " 页" : ""].filter(x => x.length > 0).join("   ")
                        visible: text.length > 0
                    }
                }
            }
        }

        ColumnLayout {
            visible: (pane.wiki.entertainments || []).length > 0
            Layout.fillWidth: true
            spacing: Tokens.space3
            GroupLabel { text: "影视综艺" }
            Repeater {
                model: pane.wiki.entertainments || []
                delegate: ColumnLayout {
                    id: show
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 0
                    Body { text: show.modelData.title }
                    Note { text: show.modelData.type; visible: text.length > 0 }
                }
            }
        }
    }
}

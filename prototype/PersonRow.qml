// 人物行：圆形头像 | 名字（后跟标签）与一行说明 | 右侧的时间与未读数（都可省）。
// 关注 / 粉丝列表与私信的会话列表共用；块状可点，悬停淡色调，选中常驻淡底（与曲目行同一套）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

AbstractButton {
    id: row

    property string avatar
    property string pendant       // 头像挂件
    property string vipLevel      // 会员标：svip | vip | ""
    property string name
    property string detail      // 签名 / 最后一条消息
    property var tags: []       // [{ text, muted }]：VIP、互相关注
    property string meta        // 时间
    property int unread         // 未读条数，0 不显示
    property bool current

    implicitHeight: Tokens.trackRow
    leftPadding: Tokens.space1
    rightPadding: Tokens.space3
    hoverEnabled: true
    Accessible.name: row.name

    background: Rectangle {
        radius: Tokens.radius
        color: Qt.alpha(Kirigami.Theme.highlightColor,
                        row.down ? Tokens.tintPressed
                        : row.current ? (row.hovered ? Tokens.tonalHover : Tokens.tonal)
                        : row.hovered ? Tokens.tintHover : 0)
        border.width: row.visualFocus ? Tokens.lineStrong : 0
        border.color: Kirigami.Theme.highlightColor
        Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
    }

    contentItem: RowLayout {
        spacing: Tokens.space3

        PersonAvatar {
            implicitWidth: Tokens.height10
            implicitHeight: Tokens.height10
            name: row.name
            source: row.avatar
            pendant: row.pendant
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 0   // 宽度只由剩余空间决定，长签名不挤右侧
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.space2
                Label {
                    Layout.fillWidth: true
                    // 向上取整：拉丁字母的宽度带小数，布局往下取整后会把最后一个字省略掉
                    Layout.maximumWidth: Math.ceil(implicitWidth)
                    text: row.name
                    font.pixelSize: Tokens.textBase
                    font.weight: row.unread > 0 ? Font.DemiBold : Font.Normal
                    elide: Text.ElideRight
                }
                VipTag { level: row.vipLevel }
                Repeater {
                    model: row.tags
                    delegate: Tag {
                        required property var modelData
                        text: modelData.text
                        muted: modelData.muted === true
                    }
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: text.length > 0
                    text: row.meta
                    font.pixelSize: Tokens.textSm
                    font.features: { "tnum": 1 }
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
            // 没有说明也没有未读时整行不占位，名字在行里垂直居中
            RowLayout {
                Layout.fillWidth: true
                visible: row.detail.length > 0 || row.unread > 0
                spacing: Tokens.space2
                Label {
                    Layout.fillWidth: true
                    text: row.detail
                    font.pixelSize: Tokens.textSm
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
                // 未读：弱化的数字，配合加粗的名字。不用强调色块——推送号常年几十上百条未读，一列色块只会催人
                Label {
                    visible: row.unread > 0
                    text: row.unread > 99 ? "99+" : row.unread
                    font.pixelSize: Tokens.textSm
                    font.features: { "tnum": 1 }
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                }
            }
        }
    }
}

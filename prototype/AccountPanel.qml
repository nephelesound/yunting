// 账号面板：点页头的头像与名字时，从它下方弹出。回答「我是用谁登录的、会员与等级怎样」，
// 再给去处：我的主页；最底下隔开一段是退出登录（破坏性操作放最后，确认在后面的对话框里）。
// 真正浮在页面之上的浮层：圆角与系统阴影。
// 会员：vipType > 0 即开通了会员，网易现在的会员品牌叫「黑胶 VIP」；等级、累计听歌没有值时不显示（缺失不等于 0）。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Popup {
    id: panel

    property var profile: Source.account || ({})   // 当前账号；测试与截图可直接给一份

    signal profileRequested()
    signal logoutRequested()

    width: Tokens.trackColumn + Tokens.space6 + Tokens.space4   // 320
    padding: Tokens.space4
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Kirigami.ShadowedRectangle {
        radius: Tokens.radiusLg
        color: Kirigami.Theme.backgroundColor
        border.width: Tokens.line
        border.color: Qt.alpha(Kirigami.Theme.textColor, Tokens.track)
        shadow.size: Tokens.space4
        shadow.color: Qt.alpha("black", Tokens.track)
        shadow.yOffset: Tokens.space1
    }

    // 一行可点的去处：块状可点（悬停淡色调），与曲目行、分享目标同一套
    component PanelRow: AbstractButton {
        id: row
        Layout.fillWidth: true
        implicitHeight: Tokens.height10
        hoverEnabled: true
        leftPadding: Tokens.space2
        rightPadding: Tokens.space2
        background: Rectangle {
            radius: Tokens.radius
            color: Qt.alpha(Kirigami.Theme.highlightColor, row.down ? Tokens.tintPressed : row.hovered ? Tokens.tintHover : 0)
            border.width: row.visualFocus ? Tokens.lineStrong : 0
            border.color: Kirigami.Theme.highlightColor
            Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
        }
        contentItem: RowLayout {
            spacing: Tokens.space3
            Kirigami.Icon {
                implicitWidth: Tokens.iconSm
                implicitHeight: Tokens.iconSm
                source: row.icon.name
                color: Kirigami.Theme.textColor
                isMask: true
            }
            Label {
                Layout.fillWidth: true
                text: row.text
                font.pixelSize: Tokens.textBase
                elide: Text.ElideRight
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Tokens.space1

        // 我是谁
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Tokens.space3
            spacing: Tokens.space3
            PersonAvatar {
                implicitWidth: Tokens.height12
                implicitHeight: Tokens.height12
                name: panel.profile.name || ""
                source: Source.sized(panel.profile.avatar || "", 96)
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.space1
                Label {
                    Layout.fillWidth: true
                    text: panel.profile.name || ""
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                // 会员 · 等级 · 累计听歌：只列有值的
                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.space2
                    Tag {
                        text: (panel.profile.vipType || 0) > 0 ? "黑胶 VIP" : ""
                    }
                    Label {
                        visible: (panel.profile.level || 0) > 0
                        text: "Lv." + (panel.profile.level || 0)
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    Label {
                        visible: (panel.profile.listenSongs || 0) > 0
                        text: "累计听歌 " + Format.count(panel.profile.listenSongs || 0) + " 首"
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }
            }
        }

        PanelRow {
            icon.name: "user-identity"
            text: "我的主页"
            enabled: (panel.profile.id || 0) > 0
            onClicked: {
                panel.close();
                panel.profileRequested();
            }
        }

        // 破坏性操作放最后，与上面隔开一段
        PanelRow {
            Layout.topMargin: Tokens.space3
            icon.name: "system-log-out"
            text: "退出登录…"
            onClicked: {
                panel.close();
                panel.logoutRequested();
            }
        }
    }
}

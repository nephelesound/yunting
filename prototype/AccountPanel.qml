// 账号浮层：点页头的头像与名字时，从它下方弹出。展示内容 + 操作，所以是 Popup 而不是菜单。
// 上：我是谁（头像、名字与会员、等级与累计听歌）；下：操作抽屉（淡色调底），去我的主页、退出登录。
// 两段靠底色区分，不用分割线；外观（圆角、边框、阴影）用系统 Popup 自带的。
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
    // 等级与累计听歌在单独的接口里，打开时才要
    readonly property var level: panel.opened ? Source.userLevel() : null

    signal profileRequested()
    signal logoutRequested()

    width: Tokens.trackColumn + Tokens.space6 + Tokens.space4   // 320
    padding: Tokens.line   // 让出系统边框，抽屉的底色画在边框里面
    // 按在账号块上由账号块自己开关；否则「按下关闭、松开又打开」
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    contentItem: ColumnLayout {
        spacing: 0

        // 我是谁：不可点，没有悬停
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Tokens.space4 - Tokens.line
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
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.space2
                    Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: implicitWidth
                        text: panel.profile.name || ""
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Tag {
                        text: (panel.profile.vipType || 0) > 0 ? "黑胶 VIP" : ""
                    }
                    Item { Layout.fillWidth: true }
                }
                // 等级与累计听歌：数值用正文色，说明用弱化色，两条事实之间只用间距（与 Facts 同一做法）
                RowLayout {
                    visible: panel.level !== null
                    spacing: Tokens.space4
                    Label {
                        visible: panel.level !== null && panel.level.level > 0
                        text: "Lv." + (panel.level ? panel.level.level : 0)
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.Medium
                        font.features: { "tnum": 1 }
                    }
                    Row {
                        visible: panel.level !== null && panel.level.nowPlayCount > 0
                        spacing: Tokens.space1
                        Label {
                            text: "累计听歌"
                            font.pixelSize: Tokens.textBase
                            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        }
                        Label {
                            text: Format.count(panel.level ? panel.level.nowPlayCount : 0)
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.Medium
                            font.features: { "tnum": 1 }
                        }
                        Label {
                            text: "首"
                            font.pixelSize: Tokens.textBase
                            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                        }
                    }
                }
            }
        }

        // 操作抽屉：两个去处并排。块状可点（悬停淡色调，与页头账号块、曲目行同一套），
        // 块向外伸出自己的内边距，图标左缘正好落在头像左缘、文字右缘落在名字所在的右边线上
        Kirigami.ShadowedRectangle {
            Layout.fillWidth: true
            implicitHeight: Tokens.height8 + 2 * Tokens.space2
            color: Qt.alpha(Kirigami.Theme.highlightColor, Tokens.tonal)
            corners.bottomLeftRadius: Kirigami.Units.cornerRadius - Tokens.line
            corners.bottomRightRadius: Kirigami.Units.cornerRadius - Tokens.line
            corners.topLeftRadius: 0
            corners.topRightRadius: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Tokens.space4 - Tokens.line - Tokens.space2
                anchors.rightMargin: Tokens.space4 - Tokens.line - Tokens.space2
                spacing: Tokens.space2
                DrawerAction {
                    icon.name: "user-identity"
                    text: "我的主页"
                    enabled: (panel.profile.id || 0) > 0
                    onClicked: {
                        panel.close();
                        panel.profileRequested();
                    }
                }
                Item { Layout.fillWidth: true }
                DrawerAction {
                    icon.name: "system-log-out"
                    text: "退出登录…"
                    onClicked: {
                        panel.close();
                        panel.logoutRequested();
                    }
                }
            }
        }
    }

    component DrawerAction: AbstractButton {
        id: action
        implicitHeight: Tokens.height8
        leftPadding: Tokens.space2
        rightPadding: Tokens.space2
        hoverEnabled: true
        background: Rectangle {
            radius: Tokens.radius
            color: Qt.alpha(Kirigami.Theme.highlightColor, action.down ? Tokens.tintPressed : action.hovered ? Tokens.tintHover : 0)
            border.width: action.visualFocus ? Tokens.lineStrong : 0
            border.color: Kirigami.Theme.highlightColor
            Behavior on color { ColorAnimation { duration: Tokens.motionFast } }
        }
        contentItem: RowLayout {
            spacing: Tokens.space2
            Kirigami.Icon {
                implicitWidth: Tokens.iconSm
                implicitHeight: Tokens.iconSm
                source: action.icon.name
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                isMask: true
            }
            Label {
                text: action.text
                font.pixelSize: Tokens.textBase
            }
        }
    }
}

// 账号菜单：点页头的头像与名字时，从它下方弹出。与曲目菜单同是系统菜单（外观、阴影、键盘操作都是 Breeze 的）。
// 三组：我是谁（头像、名字、会员与等级）· 去处（我的主页）· 离开（退出登录，确认在后面的对话框里）。
// 会员：vipType > 0 即开通了会员，网易现在的会员品牌叫「黑胶 VIP」；等级、累计听歌没有值时不显示（缺失不等于 0）。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "Format.js" as Format

Menu {
    id: panel

    property var profile: Source.account || ({})   // 当前账号；测试与截图可直接给一份
    // 等级与累计听歌在单独的接口里，打开菜单时才要
    readonly property var level: panel.opened ? Source.userLevel() : null

    signal profileRequested()
    signal logoutRequested()

    width: Tokens.trackColumn
    // 按在账号块上由账号块自己开关；否则「按下关闭、松开又打开」
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    // 我是谁：不可点。左右留白取菜单项的，头像左缘与下面菜单项的图标对齐
    Control {
        leftPadding: home.leftPadding
        rightPadding: home.rightPadding
        topPadding: Tokens.space2
        bottomPadding: Tokens.space2
        contentItem: RowLayout {
            Item { Layout.preferredWidth: Kirigami.Units.smallSpacing }   // Breeze 菜单项在图标前的留白
            PersonAvatar {
                implicitWidth: Tokens.height10
                implicitHeight: Tokens.height10
                name: panel.profile.name || ""
                source: Source.sized(panel.profile.avatar || "", 96)
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.space2
                spacing: Tokens.space1
                Label {
                    Layout.fillWidth: true
                    text: panel.profile.name || ""
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                // 会员、等级、累计听歌：只列有值的
                Flow {
                    Layout.fillWidth: true
                    spacing: Tokens.space2
                    Tag {
                        text: (panel.profile.vipType || 0) > 0 ? "黑胶 VIP" : ""
                    }
                    Label {
                        visible: panel.level !== null && panel.level.level > 0
                        text: "Lv." + (panel.level ? panel.level.level : 0)
                        font.pixelSize: Tokens.textSm
                        font.features: { "tnum": 1 }
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    Label {
                        visible: panel.level !== null && panel.level.nowPlayCount > 0
                        text: "累计听歌 " + Format.count(panel.level ? panel.level.nowPlayCount : 0) + " 首"
                        font.pixelSize: Tokens.textSm
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }
            }
        }
    }

    MenuSeparator {}

    MenuItem {
        id: home
        icon.name: "user-identity"
        text: "我的主页"
        enabled: (panel.profile.id || 0) > 0
        onTriggered: panel.profileRequested()
    }

    MenuSeparator {}

    MenuItem {
        icon.name: "system-log-out"
        text: "退出登录…"
        onTriggered: panel.logoutRequested()
    }
}

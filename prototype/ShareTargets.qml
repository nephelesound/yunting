// 分享对话框里的「发送到」：KDE 的原生分享（Purpose），与 Dolphin、Spectacle 的分享菜单是同一套。
// 单独成文件、由 ShareDialog 按需加载：系统没装 Purpose 时加载失败，这一节就不出现。
// 去掉与对话框重复的（剪贴板、二维码）和不适合链接的（上传文件类）目标；
// 有的目标需要先选一下（KDE Connect 选设备）：Purpose 自己的界面接在列表位置上，完成后回到列表并说明结果。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.purpose as Purpose

ColumnLayout {
    id: targets

    property string url
    property string title
    readonly property var shareData: ({ urls: [targets.url], title: targets.title, mimeType: "text/x-uri" })

    spacing: Tokens.space2

    Label {
        text: "发送到"
        font.pixelSize: Tokens.textSm
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
    }

    Purpose.AlternativesView {
        id: view
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        pluginType: "Export"
        inputData: targets.shareData
        disabledPlugins: ["clipboardplugin", "barcodeplugin", "nextcloudplugin", "imgurplugin", "saveasplugin",
                          "phabricatorplugin", "reviewboardplugin", "youtubeplugin"]
        onFinished: (output, error, message) => {
            result.text = error === 0 ? "已发送" : (message || "没有发送成功");
            result.ok = error === 0;
            view.reset();
        }
        // 一行一个目标：块状可点（悬停淡色调），与曲目行同一套
        delegate: AbstractButton {
            id: row
            required property int index
            required property var model   // Purpose 的角色：display、iconName（display 与按钮自身属性同名，经 model 取）
            width: ListView.view.width
            implicitHeight: Tokens.height10
            hoverEnabled: true
            leftPadding: Tokens.space2
            rightPadding: Tokens.space2
            Accessible.name: row.model.display
            onClicked: {
                result.text = "";
                view.createJob(row.index);
            }
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
                    implicitWidth: Tokens.iconMd
                    implicitHeight: Tokens.iconMd
                    source: row.model.iconName
                }
                Label {
                    Layout.fillWidth: true
                    text: row.model.display
                    font.pixelSize: Tokens.textBase
                    elide: Text.ElideRight
                }
                Kirigami.Icon {
                    implicitWidth: Tokens.iconSm
                    implicitHeight: Tokens.iconSm
                    source: "go-next-symbolic"
                    color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    isMask: true
                }
            }
        }
    }

    Label {
        id: result
        property bool ok
        visible: text.length > 0
        font.pixelSize: Tokens.textSm
        color: result.ok ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor
    }
}

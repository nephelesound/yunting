// 关于：单独的小窗口（KDE 应用的「关于」是对话窗口），内容是 kirigami-addons 的标准关于页，
// 读 C++ 里登记的 KAboutData（名称、版本、许可、主页、问题反馈、组织）。
// 最后一张卡片是「复制诊断信息」：版本、平台与最近的警告和错误，反馈问题时贴上；不含登录凭据。
import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard

Kirigami.ApplicationWindow {
    id: about

    property var diagnostics: null   // backend.diagnostics（原型里没有）

    title: "关于云听"
    width: Tokens.measureMax + Tokens.trackColumn - 2 * Tokens.space6   // 640
    height: 2 * Tokens.measureMax                                       // 800
    minimumWidth: Tokens.measureMax
    minimumHeight: Tokens.measureMax
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    pageStack.initialPage: FormCard.AboutPage {
        // 「使用的库」是一长串卡片，对用户没用；版本信息在「复制诊断信息」里
        showLibraries: false
        FormCard.FormHeader {
            title: "反馈问题"
        }
        FormCard.FormCard {
            FormCard.FormButtonDelegate {
                id: copyReport
                property bool copied: false
                icon.name: copyReport.copied ? "checkmark" : "edit-copy"
                text: copyReport.copied ? "已复制" : "复制诊断信息"
                description: "版本、系统与最近的警告和错误日志，贴进问题反馈里。不含账号与登录信息"
                enabled: about.diagnostics !== null
                onClicked: copyReport.copied = Clipboard.copy(about.diagnostics.report())
                Timer {
                    running: copyReport.copied
                    interval: 1500
                    onTriggered: copyReport.copied = false
                }
            }
            FormCard.FormDelegateSeparator {}
            FormCard.FormButtonDelegate {
                icon.name: "tools-report-bug"
                text: "到 GitHub 提交问题"
                onClicked: Qt.openUrlExternally("https://github.com/nephelesound/yunting/issues/new")
            }
        }
    }
}

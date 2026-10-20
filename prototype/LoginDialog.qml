// 扫码登录：一次性动作，状态全部来自 session：
//   waiting 等待扫码 · scanned 已扫码、等手机确认 · expired 二维码过期、可刷新 · done 登录成功（对话框自己关闭）。
// session 是 SimSession（原型）或 C++ Session（应用），接口相同。二维码用 KDE 自带的 org.kde.prison 生成。
// 两套会话可选：客户端（type=3）与网页（type=1）。打开时开始轮询，关闭（右上角或 Esc）时停止。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.prison as Prison

AppDialog {
    id: dialog

    required property var session   // 鸭子类型：SimSession 或 C++ Session

    readonly property string state: dialog.session.state
    readonly property bool webMode: dialog.session.loginMode === "web"

    title: "扫码登录"

    onOpened: if (dialog.state === "idle" || dialog.state === "done" || dialog.state === "error")
        dialog.session.start()
    onClosed: if (dialog.state !== "done") dialog.session.cancel()
    onStateChanged: if (dialog.state === "done") dialog.close()

    ColumnLayout {
        Layout.fillWidth: true
        Layout.margins: Tokens.space3   // 与标题同一内边距
        spacing: Tokens.space4

        // 二维码：过期时淡出并叠一个刷新按钮，位置不变，用户的视线不用移动
        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Tokens.tileLg + Tokens.space6 * 2   // 208
            Layout.preferredHeight: Layout.preferredWidth

            Rectangle {
                anchors.fill: parent
                radius: Tokens.radiusLg
                color: "white"   // 二维码必须在浅底上才可扫，暗色主题下也一样
            }
            Prison.Barcode {
                anchors.fill: parent
                anchors.margins: Tokens.space6
                visible: dialog.session.qrContent.length > 0
                barcodeType: Prison.Barcode.QRCode
                content: dialog.session.qrContent
                opacity: dialog.state === "expired" ? Tokens.faded : 1
            }
            BusyIndicator {
                anchors.centerIn: parent
                visible: dialog.session.qrContent.length === 0
                         && dialog.state !== "expired" && dialog.state !== "error"
                running: visible
            }
            Button {
                anchors.centerIn: parent
                visible: dialog.state === "expired" || dialog.state === "error"
                icon.name: "view-refresh-symbolic"
                text: dialog.state === "error" ? "重新获取" : "刷新二维码"
                onClicked: dialog.session.start()
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: dialog.state === "scanned" ? "已扫码，请在手机上确认登录"
                : dialog.state === "expired" ? "二维码已过期"
                : dialog.state === "error" ? "二维码获取失败"
                : "打开网易云音乐 App，扫一扫"
            font.pixelSize: Tokens.textBase
            font.weight: Font.Medium
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: dialog.state === "scanned" ? "确认后这里会自动完成登录"
                : dialog.state === "expired" ? "每个二维码只在几分钟内有效"
                : dialog.state === "error" ? "检查网络后重试"
                : "登录后可播放 VIP 曲目，并同步每日推荐与红心歌曲"
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            wrapMode: Text.Wrap
        }

        // 两套会话二选一：客户端（type=3，SPlayer 同款）与网页（type=1，YesPlayMusic 同款）。
        // 两种 MUSIC_U 接口都认，选哪套只取决于手里要登的是哪个会话
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Tokens.space2
            Button {
                objectName: "loginModeClient"
                text: "客户端扫码"
                checkable: true
                checked: !dialog.webMode
                onClicked: dialog.session.loginMode = "client"
            }
            Button {
                objectName: "loginModeWeb"
                text: "网页扫码"
                checkable: true
                checked: dialog.webMode
                onClicked: dialog.session.loginMode = "web"
            }
        }
    }
}

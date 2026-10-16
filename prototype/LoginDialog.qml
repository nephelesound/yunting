// 扫码登录：Kirigami 原生对话框（浮层，自带遮罩与投影）。一次性动作，只有三个状态：
//   waiting 等待扫码 · scanned 已扫码、等手机确认 · expired 二维码过期、可刷新。
// 二维码用 KDE 自带的 org.kde.prison 生成；内容是 /api/login/qrcode/unikey 换来的登录链接（原型里是示例 key）。
// 取消永远可用；登录成功由调用方关闭对话框并提示。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.prison as Prison

Kirigami.Dialog {
    id: dialog

    property string state: "waiting"   // waiting | scanned | expired
    property string unikey: "8f3c1e2a-prototype-unikey"

    signal refreshRequested()

    title: "扫码登录"
    // 不用 standardButtons：原型没加载 Qt 翻译，标准按钮会显示英文 Cancel
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: "取消"
            icon.name: "dialog-cancel-symbolic"
            onTriggered: dialog.close()
        }
    ]
    padding: Tokens.space6
    preferredWidth: Tokens.measureMax

    ColumnLayout {
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
                barcodeType: Prison.Barcode.QRCode
                content: "https://music.163.com/login?codekey=" + dialog.unikey
                opacity: dialog.state === "expired" ? Tokens.faded : 1
            }
            Button {
                anchors.centerIn: parent
                visible: dialog.state === "expired"
                icon.name: "view-refresh-symbolic"
                text: "刷新二维码"
                onClicked: dialog.refreshRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: dialog.state === "scanned" ? "已扫码，请在手机上确认登录"
                : dialog.state === "expired" ? "二维码已过期"
                : "打开网易云音乐 App，扫一扫"
            font.pixelSize: Tokens.textBase
            font.weight: Font.Medium
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: dialog.state === "scanned" ? "确认后这里会自动完成登录"
                : dialog.state === "expired" ? "每个二维码只在几分钟内有效"
                : "登录后可播放 VIP 曲目，并同步每日推荐与红心歌曲"
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            wrapMode: Text.Wrap
        }
    }
}

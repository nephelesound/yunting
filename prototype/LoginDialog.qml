// 扫码登录：一次性动作，状态全部来自 session：
//   waiting 等待扫码 · scanned 已扫码、等手机确认 · expired 二维码过期、可刷新 · done 登录成功（对话框自己关闭）。
// session 是 SimSession（原型）或 C++ Session（应用），接口相同。二维码用 KDE 自带的 org.kde.prison 生成。
// 扫不了码时的后路：二维码下面「用 Cookie 登录」，粘贴浏览器里的 Cookie（SPlayer、Alger 同样这样排）。
// 打开时开始轮询，关闭（右上角或 Esc）时停止。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import org.kde.prison as Prison

AppDialog {
    id: dialog

    required property var session   // 鸭子类型：SimSession 或 C++ Session

    readonly property string state: dialog.session.state
    // 两套扫码会话二选一：客户端（type=3，默认）与网页（type=1，gtk 客户端同款）
    readonly property bool webMode: dialog.session.loginMode === "web"
    // 「用 Cookie 登录」：拿不到手机时，用浏览器里已登录的那份 Cookie
    property bool cookieOpen: false
    property string cookieError: ""
    property bool cookiePending: false

    title: "扫码登录"

    onOpened: {
        dialog.cookieError = "";
        dialog.cookiePending = false;
        if (dialog.state === "idle" || dialog.state === "done" || dialog.state === "error")
            dialog.session.start();
    }
    onClosed: {
        // 粘贴的 Cookie 等同于账号密码：关掉就不留在输入框里
        cookieText.text = "";
        dialog.cookieOpen = false;
        if (dialog.state !== "done")
            dialog.session.cancel();
    }
    onStateChanged: if (dialog.state === "done") dialog.close()

    Connections {
        target: dialog.session
        function onLoginFailed(message) {
            dialog.cookiePending = false;
            dialog.cookieError = message;
        }
    }

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

        // 两套会话：客户端（type=3）与网页（type=1）。切换时 Session 重新取码，二维码与轮询的类型保持一致
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Tokens.space2
            ButtonGroup { id: loginModeGroup; exclusive: true }
            Button {
                id: loginModeClient
                objectName: "loginModeClient"
                text: "客户端扫码"
                checkable: true
                checked: !dialog.webMode
                ButtonGroup.group: loginModeGroup
                onClicked: dialog.session.loginMode = "client"
            }
            Button {
                id: loginModeWeb
                objectName: "loginModeWeb"
                text: "网页扫码"
                checkable: true
                checked: dialog.webMode
                ButtonGroup.group: loginModeGroup
                onClicked: dialog.session.loginMode = "web"
            }
        }

        // 扫不了码时（手机不在手边、扫码被风控拦下）：浏览器里已登录的 Cookie 也能登进来
        Button {
            objectName: "cookieLoginToggle"
            Layout.alignment: Qt.AlignHCenter
            flat: true
            text: dialog.cookieOpen ? "收起 Cookie 登录" : "用 Cookie 登录"
            onClicked: {
                dialog.cookieOpen = !dialog.cookieOpen;
                dialog.cookieError = "";
                if (dialog.cookieOpen) {
                    // 扫码在等也没关系：提交时 Session 会停掉轮询
                    cookieText.forceActiveFocus();
                }
            }
        }
        // 遮住输入（可点眼睛查看）：Cookie 等同于账号密码，屏幕共享、截图里不该露出来。整段 Cookie 也只有一行
        FormCard.FormPasswordFieldDelegate {
            id: cookieText
            objectName: "cookieLoginField"
            visible: dialog.cookieOpen
            label: "Cookie"
            placeholderText: "粘贴浏览器里的 Cookie（MUSIC_U=…; __csrf=…），或只粘贴 MUSIC_U 的值"
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.cookieOpen && dialog.cookieError.length === 0
            text: "Cookie 等同于账号密码：只粘贴你自己的，也不要发给任何人"
            font.pixelSize: Tokens.textSm
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            wrapMode: Text.Wrap
        }
        RowLayout {
            visible: dialog.cookieOpen
            Layout.fillWidth: true
            spacing: Tokens.space2
            Label {
                objectName: "cookieLoginError"
                Layout.fillWidth: true
                visible: dialog.cookieError.length > 0
                text: dialog.cookieError
                color: Kirigami.Theme.negativeTextColor
                font.pixelSize: Tokens.textSm
                wrapMode: Text.Wrap
            }
            Button {
                objectName: "cookieLoginButton"
                Layout.alignment: Qt.AlignRight
                text: dialog.cookiePending ? "登录中…" : "登录"
                enabled: cookieText.text.trim().length > 0 && !dialog.cookiePending
                onClicked: {
                    dialog.cookieError = "";
                    dialog.cookiePending = true;
                    dialog.session.loginWithCookie(cookieText.text);
                }
            }
        }
    }
}

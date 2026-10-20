// 「用 Cookie 登录」：折叠区把粘贴的原文交给 session；失败信息留在对话框里，按钮恢复可点。
import QtQuick
import QtTest
import ".."

Item {
    id: root
    width: 800
    height: 600

    Component {
        id: sessionComponent
        QtObject {
            property string state: "idle"
            property string qrContent: "https://music.163.com/login?codekey=test"
            property string pasted: ""
            property int starts: 0
            signal loginFailed(string message)
            function start() { starts++; }
            function cancel() { }
            function loginWithCookie(raw) { pasted = raw; }
        }
    }
    Component { id: dialogComponent; LoginDialog { } }

    TestCase {
        name: "LoginCookie"
        when: windowShown

        function test_submitsPastedCookieAndShowsError() {
            const session = createTemporaryObject(sessionComponent, root);
            const dialog = createTemporaryObject(dialogComponent, root, { session: session });
            verify(dialog);
            dialog.open();
            tryVerify(() => dialog.opened);
            compare(dialog.cookieOpen, false);

            const toggle = findChild(dialog, "cookieLoginToggle");
            verify(toggle, "展开按钮");
            toggle.clicked();
            compare(dialog.cookieOpen, true);

            const field = findChild(dialog, "cookieLoginField");
            const button = findChild(dialog, "cookieLoginButton");
            verify(field, "Cookie 输入框");
            verify(button, "登录按钮");
            compare(button.enabled, false, "空输入不能点登录");

            field.text = "MUSIC_U=web-cookie";
            tryVerify(() => button.enabled);
            button.clicked();
            compare(session.pasted, "MUSIC_U=web-cookie");
            compare(dialog.cookiePending, true);

            session.loginFailed("Cookie 无效或已过期");
            tryVerify(() => dialog.cookieError.length > 0);
            compare(dialog.cookiePending, false);
            dialog.close();
        }
    }
}

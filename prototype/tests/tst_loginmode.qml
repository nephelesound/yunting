// 扫码登录的模式切换：客户端（type=3）与网页（type=1）。切换要把选择交给 session，
// 由它重新取码；对话框只负责把当前模式显示成选中的按钮。
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
            property string loginMode: "client"
            property int starts: 0
            function start() { starts++; }
            function cancel() { }
        }
    }
    Component { id: dialogComponent; LoginDialog { } }

    TestCase {
        name: "LoginMode"
        when: windowShown

        function test_switchBetweenClientAndWeb() {
            const session = createTemporaryObject(sessionComponent, root);
            const dialog = createTemporaryObject(dialogComponent, root, { session: session });
            verify(dialog);
            dialog.open();
            tryVerify(() => dialog.opened);
            compare(session.loginMode, "client");
            compare(dialog.webMode, false);

            const client = findChild(dialog, "loginModeClient");
            const web = findChild(dialog, "loginModeWeb");
            verify(client, "客户端按钮");
            verify(web, "网页按钮");
            compare(client.checked, true);
            compare(web.checked, false);

            web.clicked();
            compare(session.loginMode, "web");
            compare(dialog.webMode, true);
            compare(client.checked, false);
            compare(web.checked, true);

            client.clicked();
            compare(session.loginMode, "client");
            compare(client.checked, true);
            compare(web.checked, false);
            dialog.close();
        }
    }
}

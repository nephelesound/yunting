// 扫码登录的原型实现：状态由截图 harness 直接给定。接口与 C++ Session 相同（见 DESIGN.md「后端接口」）：
//   state: idle | waiting | scanned | expired | done · qrContent（二维码内容）· start() · cancel()
//   loginWithCookie(raw)：网页端 Cookie 登录（原型里直接当作成功，校验只在 C++ 侧）
import QtQuick

QtObject {
    id: session

    property string state: "idle"
    property string qrContent: "https://music.163.com/login?codekey=8f3c1e2a-prototype-unikey"

    signal loginFailed(string message)

    function start() { session.state = "waiting"; }
    function cancel() { session.state = "idle"; }
    function loginWithCookie(raw) {
        if (raw.trim().length === 0) {
            session.loginFailed("粘贴 MUSIC_U 或整段 Cookie");
            return;
        }
        session.state = "done";
    }
}

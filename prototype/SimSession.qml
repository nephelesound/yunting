// 扫码登录的原型实现：状态由截图 harness 直接给定。接口与 C++ Session 相同（见 DESIGN.md「后端接口」）：
//   state: idle | waiting | scanned | expired | done · qrContent（二维码内容）· start() · cancel()
//   loginMode: client | web（type=3 客户端 / type=1 网页）
import QtQuick

QtObject {
    id: session

    property string state: "idle"
    property string qrContent: "https://music.163.com/login?codekey=8f3c1e2a-prototype-unikey"
    property string loginMode: "client"

    function start() { session.state = "waiting"; }
    function cancel() { session.state = "idle"; }
}

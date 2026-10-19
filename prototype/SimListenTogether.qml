// 一起听的原型实现：状态由截图 harness 直接给定（state：none | waiting | together）。
// 接口与 C++ ListenTogether 相同：inRoom · busy · host · users · tracks · error · shareUrl ·
// createRoom() · joinRoom(ref) · leaveRoom() · syncQueue(ids) · setRemoteApplying(b) · commandReceived · leftRoom
import QtQuick

QtObject {
    id: room

    property string state: "none"
    readonly property bool inRoom: room.state !== "none"
    property bool busy: false
    property bool host: true
    property bool remoteApplying: false
    property string error: ""
    readonly property string shareUrl: room.inRoom
        ? "https://st.music.163.com/listen-together/share/?songId=990&roomId=prototype-room&inviterId=1003" : ""
    readonly property var users: room.state === "together"
        ? [{ userId: 1003, name: "Jo", avatar: "", identity: "" }, { userId: 2, name: "一只想要变成猫但是没有成功的柴犬Official", avatar: "", identity: "" }]
        : room.inRoom ? [{ userId: 1003, name: "Jo", avatar: "", identity: "" }] : []
    property var tracks: []
    // 与 C++ 同形：好友来了才有；放不了的歌取压力数据里接下来要放的两首
    readonly property var statistics: room.state === "together" ? { listenCount: 12, totalSeconds: 12300 } : ({})
    readonly property var unplayableIds: room.state === "together" ? [903, 905] : []

    signal commandReceived(string type, real songId, real progressMs, bool playing)
    signal userInvited(real userId)
    signal leftRoom()

    function createRoom() { room.state = "waiting"; }
    function joinRoom(ref) { room.host = false; room.state = "together"; }
    function leaveRoom() { room.state = "none"; room.leftRoom(); }
    function syncQueue(ids) {}
    function inviteUser(userId) { room.userInvited(userId); }
    function setRemoteApplying(applying) { room.remoteApplying = applying; }
}

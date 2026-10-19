// 原型的数据来源：快照（Data.js 匿名 + Personal.js 个人）或 Fixtures.js 压力数据（worst | empty | one）。
// 只有没有 C++ 后端时，main.qml 才按文件路径动态创建它；qmldir 不登记，应用打包时它与三个数据文件都不进资源
// （Personal.js 是本人的账号数据，绝不能编进可执行文件）。
import QtQuick
import "Data.js" as Data
import "Personal.js" as Personal
import "Fixtures.js" as Fixtures

QtObject {
    property string fixture: ""
    // README 截图（account=stage）：推荐、歌单照常是个人快照里的，账号的名字、头像、挂件换成编造的，
    // 截图里就没有本人的身份，不必事后打码
    property bool stage: false
    readonly property var data: fixture === "" ? Data : Fixtures[fixture].data
    readonly property var personal: fixture !== "" ? Fixtures[fixture].personal
        : stage && Personal.account ? Object.assign({}, Personal, {
              account: Object.assign({}, Personal.account, { name: "云听用户", avatar: "", avatarPendant: "" }) })
        : Personal
}

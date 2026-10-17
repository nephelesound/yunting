// 原型的数据来源：快照（Data.js 匿名 + Personal.js 个人）或 Fixtures.js 压力数据（worst | empty | one）。
// 只有没有 C++ 后端时，main.qml 才按文件路径动态创建它；qmldir 不登记，应用打包时它与三个数据文件都不进资源
// （Personal.js 是本人的账号数据，绝不能编进可执行文件）。
import QtQuick
import "Data.js" as Data
import "Personal.js" as Personal
import "Fixtures.js" as Fixtures

QtObject {
    property string fixture: ""
    readonly property var data: fixture === "" ? Data : Fixtures[fixture].data
    readonly property var personal: fixture === "" ? Personal : Fixtures[fixture].personal
}

// 分页列表的模型。数据是 JS 数组，续取一页就换成一个更长的新数组（「加载中」标记变化时也会换）。
// 直接把数组当 ListView 的 model，每换一次列表就整个重置、滚回顶部。这里按差异同步：
//   新数组以旧内容开头 → 只追加多出来的行（已有的行原地不动，滚动位置不变）；
//   内容没变 → 不动；其他变化（换了歌、换了歌手）→ 整表重建。
// 每行只记下标；委托用 values[index] 取数据（见评论区与歌手页）。
import QtQuick

ListModel {
    id: growing

    property var values: []
    property var keyOf: value => value.id   // 判断「是不是同一项」的键
    property var shown: []                   // 上次同步时各行的键

    onValuesChanged: {
        const next = growing.values.map(v => growing.keyOf(v));
        const prefix = growing.shown.length <= next.length
            && growing.shown.every((key, i) => key === next[i]);
        if (!prefix) {
            growing.clear();
            growing.shown = [];
        }
        for (let i = growing.shown.length; i < next.length; i++)
            growing.append({ position: i });
        growing.shown = next;
    }
}

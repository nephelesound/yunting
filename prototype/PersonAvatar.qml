// 人物头像（账号、评论者）：圆形是人物的通行约定。全应用只有这一种头像画法。
// 没有图片（或加载失败）时：中性淡底 + 弱化色的人像符号，与封面占位同一种语言。
// 不按名字哈希上色（每个人一种随机彩色，和其余界面的克制用色冲突），也不显示名字首字（中文名会乱码）
import QtQuick
import org.kde.kirigami as Kirigami

Item {
    id: avatar

    property string name
    property string source
    // 网易云的头像挂件：一张方形、中间镂空的框图，头像占框的中间三分之二，所以框画成头像的 1.5 倍、居中，
    // 超出头像的部分不占布局。头像小于 pendantMinSize 时不画（别人的挂件缩到那么小只剩一团杂色）；
    // 页头右上角自己的头像例外：那是自己的装扮，小也要看得见
    property string pendant
    property int pendantMinSize: Tokens.height8
    // 解码尺寸：默认按显示的两倍。显示尺寸会变的地方（主页戴上挂件时头像缩小）给一个固定值，免得一缩就重新解码、闪一下占位
    property int decodeSize: 2 * Math.max(avatar.width, avatar.height)

    readonly property bool hasImage: image.status === Image.Ready

    Accessible.role: Accessible.Graphic
    Accessible.name: avatar.name

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: !avatar.hasImage
        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.tonal)
        Kirigami.Icon {
            anchors.centerIn: parent
            width: parent.width / 2
            height: width
            source: "avatar-default-symbolic"
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            isMask: true
        }
    }

    // 圆形裁切：ShadowedImage 的圆角（投影尺寸为 0，不是装饰投影）
    Kirigami.ShadowedImage {
        id: image
        anchors.fill: parent
        visible: avatar.hasImage
        radius: width / 2
        source: avatar.source
        sourceSize.width: avatar.decodeSize
        sourceSize.height: avatar.decodeSize
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    Image {
        objectName: "pendant"
        anchors.centerIn: parent
        width: 1.5 * avatar.width
        height: width
        visible: avatar.pendant.length > 0 && avatar.width >= avatar.pendantMinSize && status === Image.Ready
        source: avatar.pendant.length > 0 && avatar.width >= avatar.pendantMinSize ? avatar.pendant : ""
        sourceSize.width: 2 * width
        sourceSize.height: 2 * height
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
}

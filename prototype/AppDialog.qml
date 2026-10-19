// 云听的对话框：KDE 的 FormCardDialog。标题、内容与按钮同一种内边距（FormCard 的 12px），
// 宽度不超出窗口，长文字换行而不是把对话框撑出屏幕。内容放 FormCard 的行；自绘的内容左右各留 Tokens.space3 对齐。
// 应用没有加载 Qt 的翻译，标准按钮会显示英文：按钮文字在这里统一写成中文。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigamiaddons.formcard as FormCard

FormCard.FormCardDialog {
    id: dialog

    property string acceptText: ""       // 空：没有确认按钮，只有右上角的关闭（以及 Esc）
    property bool acceptEnabled: true
    // 删除、退出这类不可轻易撤回的确认换掉默认的勾：图标说出这一下做的是什么
    property string acceptIconName: ""
    property url acceptIconSource: ""
    property string message: ""          // 确认类对话框的说明：换行显示在最上面
    property int preferredWidth: Tokens.dialogWidth   // 内容更宽的（分享）自己加宽；窗口窄时两侧留页边距

    implicitWidth: Math.min(dialog.parent ? dialog.parent.width - 2 * Tokens.pageMargin : dialog.preferredWidth,
                            dialog.preferredWidth)

    standardButtons: dialog.acceptText.length > 0 ? Dialog.Ok | Dialog.Cancel : Dialog.Cancel

    Label {
        visible: dialog.message.length > 0
        Layout.fillWidth: true
        Layout.margins: Tokens.space3   // 与 FormCard 的行同一内边距
        text: dialog.message
        wrapMode: Text.Wrap
    }

    Component.onCompleted: {
        dialog.standardButton(Dialog.Cancel).text = "取消";
        const ok = dialog.standardButton(Dialog.Ok);
        if (ok) {
            ok.text = Qt.binding(() => dialog.acceptText);
            ok.enabled = Qt.binding(() => dialog.acceptEnabled);
            if (dialog.acceptIconName.length > 0)
                ok.icon.name = dialog.acceptIconName;
            if (dialog.acceptIconSource.toString().length > 0) {
                ok.icon.name = "";   // 主题图标名优先于 source：不清掉它，自带的图会被默认的勾盖住
                ok.icon.source = dialog.acceptIconSource;
            }
        } else {
            dialog.footer.visible = false;
        }
    }
}

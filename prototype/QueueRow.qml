// 队列行：比歌曲行更紧凑（队列是"证据面"，不是扫描面）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ItemDelegate {
    id: control

    required property int index
    required property var modelData

    property bool current: false

    signal activated(int index)

    width: ListView.view ? ListView.view.width : implicitWidth
    hoverEnabled: true
    highlighted: control.current

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            text: control.modelData.name
            elide: Text.ElideRight
        }

        Label {
            Layout.preferredWidth: Kirigami.Units.gridUnit * 7
            text: control.modelData.artist
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
        }
    }

    onClicked: control.activated(control.index)
}

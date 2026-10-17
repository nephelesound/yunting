// 10 段均衡器：31Hz..16kHz，-12..+12 dB；另带低音与立体声空间感。
// audio.eqAvailable 为 false（关闭 DSP 构建）时入口不出现；面板本身不判断构建。
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Popup {
    id: panel

    property var audio
    readonly property var bands: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]

    width: 10 * Tokens.height8 + 9 * Tokens.space2 + 2 * Tokens.space4
    height: 430
    padding: Tokens.space4
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function showAt(item) {
        parent = item;
        x = (item.width - width) / 2;
        y = -height - Tokens.space2;
        open();
    }

    background: Rectangle {
        radius: Tokens.radiusLg
        color: Kirigami.Theme.backgroundColor
        border.width: Tokens.line
        border.color: Qt.alpha(Kirigami.Theme.textColor, Tokens.track)
    }

    contentItem: ColumnLayout {
        spacing: Tokens.space3

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "均衡器"
                font.pixelSize: Tokens.textBase
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "重置"
                flat: true
                implicitHeight: Tokens.height7
                enabled: panel.audio && typeof panel.audio.resetEq === "function"
                onClicked: panel.audio.resetEq()
            }
            IconButton {
                icon.name: "window-close-symbolic"
                text: "关闭"
                onClicked: panel.close()
            }
        }

        // 预设：一键换整套曲线，之后每个频段仍可单独改
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space1
            Repeater {
                model: [
                    { key: "flat", label: "平直" }, { key: "pop", label: "流行" },
                    { key: "rock", label: "摇滚" }, { key: "bass", label: "低音" },
                    { key: "vocal", label: "人声" }, { key: "live", label: "现场" },
                    { key: "spatial", label: "空间" },
                ]
                delegate: Button {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: Tokens.height7
                    flat: true
                    text: modelData.label
                    font.pixelSize: Tokens.textXs
                    onClicked: panel.audio.applyEqPreset(modelData.key)
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Tokens.space2

            Repeater {
                model: 10
                delegate: ColumnLayout {
                    id: band
                    required property int index
                    spacing: Tokens.space1

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: Math.round(slider.value) + ""
                        font.pixelSize: Tokens.textXs
                        font.features: { "tnum": 1 }
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                    Slider {
                        id: slider
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        Layout.preferredWidth: Tokens.height8
                        Layout.preferredHeight: 150
                        from: -12
                        to: 12
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        enabled: panel.audio && typeof panel.audio.setEqGain === "function"
                        onMoved: panel.audio.setEqGain(band.index, value)
                        Binding {
                            target: slider
                            property: "value"
                            value: panel.audio && panel.audio.eqGains && panel.audio.eqGains.length > band.index
                                ? panel.audio.eqGains[band.index] : 0
                            when: !slider.pressed
                        }
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: panel.bands[band.index]
                        font.pixelSize: Tokens.textXs
                        color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space2
            Label {
                text: "低音"
                font.pixelSize: Tokens.textXs
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Slider {
                id: bass
                Layout.fillWidth: true
                from: -12
                to: 12
                stepSize: 1
                snapMode: Slider.SnapAlways
                onMoved: panel.audio.setBassBoost(value)
                Binding {
                    target: bass
                    property: "value"
                    value: panel.audio ? panel.audio.bassBoost : 0
                    when: !bass.pressed
                }
            }
            Label {
                Layout.preferredWidth: Tokens.height10
                horizontalAlignment: Text.AlignRight
                text: Math.round(bass.value) + " dB"
                font.pixelSize: Tokens.textXs
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.space2
            Label {
                text: "空间感"
                font.pixelSize: Tokens.textXs
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
            Slider {
                id: space
                Layout.fillWidth: true
                from: 0
                to: 2
                stepSize: 0.1
                snapMode: Slider.SnapAlways
                onMoved: panel.audio.setStereoWidth(value)
                Binding {
                    target: space
                    property: "value"
                    value: panel.audio ? panel.audio.stereoWidth : 1
                    when: !space.pressed
                }
            }
            Label {
                Layout.preferredWidth: Tokens.height10
                horizontalAlignment: Text.AlignRight
                text: Math.round(space.value * 100) + "%"
                font.pixelSize: Tokens.textXs
                font.features: { "tnum": 1 }
                color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            }
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: "EQ · 低音 · 空间感"
            font.pixelSize: Tokens.textXs
            color: Qt.alpha(Kirigami.Theme.textColor, Tokens.muted)
            elide: Text.ElideRight
        }
    }
}

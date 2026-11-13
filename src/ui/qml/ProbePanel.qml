pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The pixel read by the probe, in the forms worth copying, and the ones remembered, as a list
// or as swatches.
ToolPanel {
    id: panel

    title: "Probe"
    buttons: false
    fills: true

    required property BrowseContext context
    // What ImageView.probed reported last, or null.
    property var probed: null
    // A remembered pixel that was clicked, shown until the next one is probed.
    property var remembered: null
    // What the rows show.
    readonly property var shown: remembered ? remembered : probed

    onProbedChanged: remembered = null

    Settings {
        id: settings
        category: "Probe"
        property string history: "[]"
        property bool swatches: false
    }

    property var entries: {
        try {
            const parsed = JSON.parse(settings.history)
            return Array.isArray(parsed) ? parsed : []
        } catch (e) {
            return []
        }
    }

    // Newest first; probing the same pixel again does not repeat it.
    function remember(pixel) {
        if (!pixel)
            return
        const entry = {
            x: pixel.x, y: pixel.y, hex: pixel.hex, values: pixel.values, fractions: pixel.fractions,
            depth: pixel.depth, srgbHex: pixel.srgbHex, srgbValues: pixel.srgbValues,
            color: pixel.color.toString()
        }
        const first = entries.length > 0 ? entries[0] : null
        if (first && first.x === entry.x && first.y === entry.y && first.hex === entry.hex && first.values === entry.values)
            return
        entries = [entry].concat(entries).slice(0, 48)
        settings.history = JSON.stringify(entries)
    }

    function clearHistory() {
        entries = []
        settings.history = "[]"
    }

    function copy(text) {
        if (!text)
            return
        FileClipboard.copyText(text)
        copied.text = "Copied " + text
        copiedTimer.restart()
    }

    RowLayout {
        spacing: 10
        Rectangle {
            objectName: "probeSwatch"
            implicitWidth: 52
            implicitHeight: 52
            color: panel.shown ? panel.shown.color : "transparent"
            border.color: Theme.swatchFrame
        }
        ColumnLayout {
            spacing: 2
            Label {
                text: panel.shown ? "x " + panel.shown.x + ", y " + panel.shown.y : "No pixel yet"
            }
            Label {
                visible: !!panel.shown
                text: panel.shown ? panel.shown.depth + (panel.remembered ? ", remembered" : "") : ""
                color: Theme.textMuted
            }
        }
    }

    ColumnLayout {
        visible: !!panel.shown
        spacing: 0
        Layout.fillWidth: true

        Repeater {
            model: panel.shown ? [
                { label: panel.shown.depth === "8-bit" ? "Hex" : "Hex, 8-bit", value: panel.shown.hex },
                { label: "Values", value: panel.shown.values },
                { label: "Fractions", value: panel.shown.fractions },
                { label: "sRGB", value: panel.shown.srgbHex ? panel.shown.srgbHex : "" },
                { label: "sRGB values", value: panel.shown.srgbValues ? panel.shown.srgbValues : "" },
            ].filter(row => row.value !== "") : []

            // The label above, so that 16-bit and float values have the whole width.
            delegate: ColumnLayout {
                id: row
                required property var modelData
                spacing: 0
                Layout.fillWidth: true
                Label {
                    text: row.modelData.label
                    color: Theme.textMuted
                    font.pointSize: Theme.smallSize
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: row.modelData.value
                        font.family: Theme.monospace
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        text: "Copy"
                        onClicked: panel.copy(row.modelData.value)
                    }
                }
            }
        }
    }

    Label {
        visible: !panel.shown
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pointSize: Theme.smallSize
        text: "Click the image to read a pixel at full size. Drag to follow the mouse; arrow keys move by one pixel. The wheel zooms and the right button pans. Ctrl+C copies the hex value."
    }

    RowLayout {
        Layout.topMargin: 6
        Layout.fillWidth: true
        Label { text: "Remembered"; color: Theme.textMuted; Layout.fillWidth: true }
        ToolButton {
            text: "List"
            checkable: true
            checked: !settings.swatches
            onClicked: settings.swatches = false
        }
        ToolButton {
            objectName: "probeSwatchesButton"
            text: "Swatches"
            checkable: true
            checked: settings.swatches
            onClicked: settings.swatches = true
        }
        ToolButton {
            text: "Clear"
            enabled: panel.entries.length > 0
            onClicked: panel.clearHistory()
        }
    }

    ListView {
        id: historyList
        objectName: "probeHistory"
        visible: !settings.swatches
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: panel.entries
        ScrollBar.vertical: AppScrollBar {}
        delegate: ItemDelegate {
            id: entry
            required property var modelData
            width: historyList.width
            height: 30
            padding: 4
            onClicked: {
                panel.remembered = modelData
                panel.copy(modelData.hex)
            }
            contentItem: RowLayout {
                spacing: 8
                Rectangle {
                    implicitWidth: 20
                    implicitHeight: 20
                    color: entry.modelData.color
                    border.color: Theme.swatchFrame
                }
                Label { text: entry.modelData.hex; font.family: Theme.monospace }
                Label {
                    text: entry.modelData.values
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }
        }
    }

    GridView {
        id: swatchGrid
        visible: settings.swatches
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        cellWidth: 32
        cellHeight: 32
        model: panel.entries
        delegate: Rectangle {
            id: swatch
            required property var modelData
            width: 28
            height: 28
            color: modelData.color
            border.color: swatchArea.containsMouse ? Theme.onImageBright : Theme.swatchFrame
            ToolTip.visible: swatchArea.containsMouse
            ToolTip.text: modelData.hex + "  " + modelData.values
            MouseArea {
                id: swatchArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    panel.remembered = swatch.modelData
                    panel.copy(swatch.modelData.hex)
                }
            }
        }
    }

    Label {
        id: copied
        objectName: "probeCopied"
        Layout.fillWidth: true
        color: Theme.accent
        elide: Text.ElideRight
        opacity: copiedTimer.running ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }
    Timer { id: copiedTimer; interval: 2000 }

}

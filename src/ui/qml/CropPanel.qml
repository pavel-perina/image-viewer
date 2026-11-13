pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The values of the crop: its ratio, position and size, to the pixel, and the buttons to apply
// or leave it.
Rectangle {
    id: panel

    required property CropOverlay overlay
    required property ImageView view

    signal applyRequested()
    signal cancelRequested()
    // Enter in a field took its value, and the keys can go back to the image.
    signal doneEditing()

    // A field has the keys, so Enter is its own rather than the crop's.
    readonly property bool editing: {
        for (let item = Window.activeFocusItem; item; item = item.parent) {
            if (item === panel)
                return true
        }
        return false
    }

    width: Theme.panelWidth
    color: Theme.panel

    readonly property int imageWidth: view.imageWidth
    // The stored choice, if it is still one of the list; a screen may have gone.
    readonly property int ratioIndex: Math.max(0, ratios.findIndex(r => r.name === settings.ratioName))
    readonly property var ratio: ratios[ratioIndex]
    readonly property int imageHeight: view.imageHeight

    // Ratios with the longer side first; the orientation decides which way round they go. A
    // screen also says which way it stands.
    readonly property var ratios: [
        { name: "Free", w: 0, h: 0 },
        { name: "Keep image", w: -1, h: -1 },
        { name: "1:1", w: 1, h: 1 },
        { name: "5:4", w: 5, h: 4 },
        { name: "4:3", w: 4, h: 3 },
        { name: "3:2", w: 3, h: 2 },
        { name: "16:10", w: 16, h: 10 },
        { name: "16:9", w: 16, h: 9 },
    ].concat(Screens.sizes.map(s => ({
        name: s.name, w: Math.max(s.width, s.height), h: Math.min(s.width, s.height), portrait: s.height > s.width
    }))).concat([
        { name: "A4", w: 297, h: 210 },
        { name: "Letter", w: 11, h: 8.5 },
        { name: "Custom", w: -2, h: -2 },
    ])

    Settings {
        id: settings
        category: "Crop"
        property string ratioName: "Free"
        property bool portrait: false
        property int customWidth: 4
        property int customHeight: 3
        property alias grid: gridCheck.checked
    }

    function chosenRatio() {
        const r = ratio
        let w = r.w, h = r.h
        if (w === -1) {
            w = imageWidth; h = imageHeight
        } else if (w === -2) {
            w = settings.customWidth; h = settings.customHeight
        }
        if (w <= 0 || h <= 0)
            return [0, 0]
        // Halves such as Letter's 8.5 become whole numbers.
        if (w % 1 !== 0 || h % 1 !== 0) {
            w *= 2; h *= 2
        }
        const longer = Math.max(w, h), shorter = Math.min(w, h)
        return settings.portrait ? [shorter, longer] : [longer, shorter]
    }

    function applyRatio() {
        const r = chosenRatio()
        overlay.setAspect(r[0], r[1])
    }

    // A new image starts with its own orientation and all of it.
    function start() {
        if (ratio.w === -1)
            settings.portrait = imageHeight > imageWidth
        applyRatio()
        overlay.reset()
    }

    // Keeps the mouse from reaching the image underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelMargin
        spacing: Theme.panelSpacing

        Label {
            text: "Crop"
            font.bold: true
            font.pointSize: Theme.titleSize
        }

        Label { text: "Aspect ratio"; color: Theme.textMuted }
        ComboBox {
            id: ratioBox
            objectName: "cropRatio"
            Layout.fillWidth: true
            model: panel.ratios.map(r => r.name)
            currentIndex: panel.ratioIndex
            onActivated: (index) => {
                settings.ratioName = panel.ratios[index].name
                if (panel.ratios[index].w === -1)
                    settings.portrait = panel.imageHeight > panel.imageWidth
                else if (panel.ratios[index].portrait !== undefined)
                    settings.portrait = panel.ratios[index].portrait
                panel.applyRatio()
            }
        }

        RowLayout {
            visible: panel.ratio.w === -2
            spacing: 4
            NumberBox {
                from: 1; to: 9999
                value: settings.customWidth
                onValueModified: { settings.customWidth = value; panel.applyRatio() }
            }
            Label { text: ":" }
            NumberBox {
                from: 1; to: 9999
                value: settings.customHeight
                onValueModified: { settings.customHeight = value; panel.applyRatio() }
            }
        }

        RowLayout {
            enabled: panel.ratioIndex !== 0 && panel.ratio.name !== "1:1"
            spacing: 0
            ButtonGroup { id: orientationGroup }
            RadioButton {
                text: "Landscape"
                checked: !settings.portrait
                ButtonGroup.group: orientationGroup
                onClicked: { settings.portrait = false; panel.applyRatio() }
            }
            RadioButton {
                objectName: "cropPortrait"
                text: "Portrait"
                checked: settings.portrait
                ButtonGroup.group: orientationGroup
                onClicked: { settings.portrait = true; panel.applyRatio() }
            }
        }

        Label { text: "Position and size, in pixels"; color: Theme.textMuted; Layout.topMargin: 4 }
        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 4
            Label { text: "X" }
            PixelBox {
                objectName: "cropX"
                to: Math.max(0, panel.imageWidth - panel.overlay.cropRect.width)
                value: panel.overlay.cropRect.x
                onValueModified: panel.overlay.setPosition(value, panel.overlay.cropRect.y)
            }
            Label { text: "Y" }
            PixelBox {
                objectName: "cropY"
                to: Math.max(0, panel.imageHeight - panel.overlay.cropRect.height)
                value: panel.overlay.cropRect.y
                onValueModified: panel.overlay.setPosition(panel.overlay.cropRect.x, value)
            }
            Label { text: "Width" }
            PixelBox {
                objectName: "cropWidth"
                from: 1
                to: Math.max(1, panel.imageWidth)
                value: panel.overlay.cropRect.width
                onValueModified: panel.overlay.setWidth(value)
            }
            Label { text: "Height" }
            PixelBox {
                objectName: "cropHeight"
                from: 1
                to: Math.max(1, panel.imageHeight)
                value: panel.overlay.cropRect.height
                onValueModified: panel.overlay.setHeight(value)
            }
        }

        CheckBox {
            id: gridCheck
            text: "Rule of thirds grid"
            checked: true
        }
        Binding { target: panel.overlay; property: "grid"; value: gridCheck.checked }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
            text: "Drag the edges or corners, or draw a new rectangle on the image. The wheel zooms; the right button, or Space with the left one, pans."
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "Reset"
                onClicked: panel.overlay.reset()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                onClicked: panel.cancelRequested()
            }
            Button {
                objectName: "cropApply"
                text: "Crop"
                highlighted: true
                onClicked: panel.applyRequested()
            }
        }
    }

    component NumberBox: SpinBox {
        editable: true
        // Plain digits, without the thousands separator of the locale.
        textFromValue: (value) => String(value)
        valueFromText: (text) => parseInt(text, 10)
        Connections {
            target: contentItem
            ignoreUnknownSignals: true
            function onAccepted() { panel.doneEditing() }
        }
    }

    component PixelBox: NumberBox {
        Layout.fillWidth: true
        from: 0
    }
}

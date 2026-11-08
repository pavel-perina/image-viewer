pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The size to scale the image shown to, typed or from presets such as the screens. A size of
// another ratio fits inside it, crops the middle or stretches; within a pixel it just stretches.
AppDialog {
    id: dialog
    objectName: "resizeDialog"

    required property ImageView view

    readonly property int imageWidth: view.imageWidth
    readonly property int imageHeight: view.imageHeight
    // The other side for the ratio of the image.
    function heightFor(width) { return Math.max(1, Math.round(width * imageHeight / imageWidth)) }
    function widthFor(height) { return Math.max(1, Math.round(height * imageWidth / imageHeight)) }

    // Whether the size typed is of another ratio than the image's, beyond rounding.
    readonly property bool otherRatio: Math.abs(heightBox.value - heightFor(widthBox.value)) > 1
                                       && Math.abs(widthBox.value - widthFor(heightBox.value)) > 1
    // What is applied: with fit, the largest size of the image's ratio inside the one typed.
    readonly property size target: {
        const w = widthBox.value, h = heightBox.value
        if (otherRatio && settings.mode === "fit") {
            return heightFor(w) <= h ? Qt.size(w, heightFor(w)) : Qt.size(widthFor(h), h)
        }
        return Qt.size(w, h)
    }
    readonly property string problem: visible ? view.resizeProblem(target.width, target.height) : ""

    title: "Resize"
    width: Math.min(380, parent.width - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel

    Settings {
        id: settings
        category: "Resize"
        property bool keepRatio: true
        property string mode: "fill"  // for another ratio: fit, fill (crop the middle) or stretch
    }

    // Starts from the size the image has.
    onAboutToShow: {
        setting = true
        widthBox.value = imageWidth
        heightBox.value = imageHeight
        presetBox.currentIndex = 0
        setting = false
        widthBox.contentItem.forceActiveFocus()
    }

    onAccepted: {
        if (problem === "")
            view.resizeImage(target.width, target.height, otherRatio && settings.mode === "fill")
    }

    property bool setting: false
    function setWidth(width) {
        setting = true
        widthBox.value = width
        if (settings.keepRatio)
            heightBox.value = heightFor(width)
        setting = false
    }
    function setHeight(height) {
        setting = true
        heightBox.value = height
        if (settings.keepRatio)
            widthBox.value = widthFor(height)
        setting = false
    }

    readonly property var presets: [{ name: "Choose a size" }]
        .concat(Screens.sizes.map(s => ({ name: s.name, width: s.width, height: s.height })))
        .concat([3840, 2560, 1920, 1280].map(edge => ({ name: "Long edge " + edge, edge: edge })))
        .concat([50, 25].map(percent => ({ name: percent + "%", percent: percent })))

    function applyPreset(preset) {
        if (preset.width) {
            // A screen is a size of its own, whatever the ratio.
            setting = true
            widthBox.value = preset.width
            heightBox.value = preset.height
            setting = false
        } else if (preset.edge) {
            // The ratio of the image, whether kept or not.
            setting = true
            widthBox.value = imageWidth >= imageHeight ? preset.edge : widthFor(preset.edge)
            heightBox.value = imageWidth >= imageHeight ? heightFor(preset.edge) : preset.edge
            setting = false
        } else if (preset.percent) {
            setting = true
            widthBox.value = Math.max(1, Math.round(imageWidth * preset.percent / 100))
            heightBox.value = Math.max(1, Math.round(imageHeight * preset.percent / 100))
            setting = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            text: "Now " + dialog.imageWidth + " x " + dialog.imageHeight + " pixels"
            color: "#a0a0a0"
        }

        ComboBox {
            id: presetBox
            objectName: "resizePreset"
            Layout.fillWidth: true
            model: dialog.presets.map(p => p.name)
            onActivated: (index) => {
                if (index > 0)
                    dialog.applyPreset(dialog.presets[index])
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 10
            Label { text: "Width" }
            SizeBox {
                id: widthBox
                objectName: "resizeWidth"
                onValueChanged: if (!dialog.setting) dialog.setWidth(value)
            }
            Label { text: "Height" }
            SizeBox {
                id: heightBox
                objectName: "resizeHeight"
                onValueChanged: if (!dialog.setting) dialog.setHeight(value)
            }
        }

        CheckBox {
            text: "Keep the ratio"
            checked: settings.keepRatio
            onToggled: {
                settings.keepRatio = checked
                if (checked)
                    dialog.setWidth(widthBox.value)
            }
        }

        ColumnLayout {
            visible: dialog.otherRatio
            spacing: 0
            Label { text: "The image has another ratio:"; color: "#a0a0a0" }
            RadioButton {
                text: "Fit inside, " + Math.round(dialog.target.width) + " x " + Math.round(dialog.target.height)
                checked: settings.mode === "fit"
                onToggled: if (checked) settings.mode = "fit"
            }
            RadioButton {
                objectName: "resizeFill"
                text: "Crop the middle to fill it"
                checked: settings.mode === "fill"
                onToggled: if (checked) settings.mode = "fill"
            }
            RadioButton {
                text: "Stretch"
                checked: settings.mode === "stretch"
                onToggled: if (checked) settings.mode = "stretch"
            }
        }

        Label {
            visible: dialog.problem !== ""
            text: dialog.problem
            color: "#e07070"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(() => dialog.problem === "")

    // Enter resizes from anywhere in the dialog, with what a field holds taken first.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: dialog.opened
        onActivated: {
            for (const box of [widthBox, heightBox]) {
                if (box.contentItem.activeFocus) {
                    const value = box.valueFromText(box.contentItem.text, box.locale)
                    if (!isNaN(value))
                        box.value = Math.max(box.from, Math.min(box.to, value))
                }
            }
            if (dialog.problem === "")
                dialog.accept()
        }
    }

    component SizeBox: SpinBox {
        editable: true
        from: 1
        to: 65535
        Layout.fillWidth: true
        // Plain digits, without the thousands separator of the locale.
        textFromValue: (value) => String(value)
        valueFromText: (text) => parseInt(text, 10)
        validator: IntValidator { bottom: 1; top: 65535 }
    }
}

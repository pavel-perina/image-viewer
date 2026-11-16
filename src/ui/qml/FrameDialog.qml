pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Up to three frames around the image, outer first, each a width and a colour, then a shadow
// on white or on transparency. Applied as an edit, so it can be undone.
AppDialog {
    id: dialog
    objectName: "frameDialog"

    required property ImageView view

    // What the view takes, from the choices below.
    readonly property var look: ({
        frames: [{ width: settings.outerWidth, colour: settings.outerColour },
                 { width: settings.middleWidth, colour: settings.middleColour },
                 { width: settings.innerWidth, colour: settings.innerColour }],
        shadow: settings.shadow,
        transparent: settings.transparent
    })
    readonly property size framed: visible ? view.framedSize(look) : Qt.size(0, 0)
    readonly property string problem: visible ? view.frameProblem(look) : ""

    title: "Frame"
    width: Math.min(420, parent.width - 32)
    standardButtons: Dialog.Ok | Dialog.Cancel

    Settings {
        id: settings
        category: "Frame"
        property int outerWidth: 1
        property color outerColour: "#404040"
        property int middleWidth: 12
        property color middleColour: "#ffffff"
        property int innerWidth: 1
        property color innerColour: "#a0a0a0"
        property int shadow: 0
        property bool transparent: false
    }

    onAccepted: {
        if (problem === "")
            view.frameImage(look)
    }

    component FrameRow: RowLayout {
        id: frameRow
        required property string label
        required property string name
        property int value
        property color colour
        signal widthSet(int value)
        signal colourSet(color colour)
        Layout.fillWidth: true
        spacing: Theme.panelSpacing
        Label { text: frameRow.label; Layout.fillWidth: true }
        SpinBox {
            objectName: frameRow.name + "Width"
            editable: true
            from: 0
            to: 1000
            value: frameRow.value
            onValueModified: frameRow.widthSet(value)
        }
        ColorField {
            objectName: frameRow.name + "Colour"
            Layout.preferredWidth: 120
            colour: frameRow.colour
            onPicked: (colour) => frameRow.colourSet(colour)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.panelSpacing

        Label {
            text: "Widths in pixels; 0 leaves a frame out."
            color: Theme.textMuted
        }
        FrameRow {
            label: "Outer"; name: "frameOuter"
            value: settings.outerWidth; colour: settings.outerColour
            onWidthSet: (value) => settings.outerWidth = value
            onColourSet: (colour) => settings.outerColour = colour
        }
        FrameRow {
            label: "Middle"; name: "frameMiddle"
            value: settings.middleWidth; colour: settings.middleColour
            onWidthSet: (value) => settings.middleWidth = value
            onColourSet: (colour) => settings.middleColour = colour
        }
        FrameRow {
            label: "Inner"; name: "frameInner"
            value: settings.innerWidth; colour: settings.innerColour
            onWidthSet: (value) => settings.innerWidth = value
            onColourSet: (colour) => settings.innerColour = colour
        }
        NumberRow {
            label: "Shadow"
            boxName: "frameShadow"
            to: 100
            value: settings.shadow
            onModified: (value) => settings.shadow = value
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: settings.shadow > 0
            Label { text: "Background"; Layout.fillWidth: true }
            ComboBox {
                objectName: "frameBackground"
                model: ["White", "Transparent"]
                currentIndex: settings.transparent ? 1 : 0
                onActivated: (index) => settings.transparent = index === 1
            }
        }
        Label {
            objectName: "frameResult"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: dialog.problem ? Theme.danger : Theme.textMuted
            text: dialog.problem ? dialog.problem
                  : "Now " + dialog.view.imageWidth + " × " + dialog.view.imageHeight
                    + ", framed " + dialog.framed.width + " × " + dialog.framed.height + " pixels"
        }
    }

    Component.onCompleted: standardButton(Dialog.Ok).enabled = Qt.binding(() => dialog.problem === "")
}

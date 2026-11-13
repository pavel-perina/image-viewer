pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The tools to draw on the image with and the style of what they draw: of the shape selected,
// or else of the tool, which the overlay remembers. The buttons apply the shapes or leave.
Rectangle {
    id: panel

    required property AnnotateOverlay overlay

    signal applyRequested()
    signal cancelRequested()
    // Enter in a field took its value, and the keys can go back to the image.
    signal doneEditing()

    // A field has the keys, so Enter is its own rather than applying.
    readonly property bool editing: {
        for (let item = Window.activeFocusItem; item; item = item.parent) {
            if (item === panel)
                return true
        }
        return false
    }

    // What the style shown belongs to, which decides the controls that make sense.
    readonly property int styled: overlay.styledTool
    readonly property bool hasStyle: styled !== AnnotateOverlay.Select
    readonly property bool hasColour: hasStyle && styled !== AnnotateOverlay.Pixelate
    readonly property bool isText: styled === AnnotateOverlay.Text
    readonly property bool isNumber: styled === AnnotateOverlay.Number
    // Kinds with letters: a typeface, a size, and words to set while one is selected.
    readonly property bool hasLetters: isText || isNumber
    readonly property bool canFill: styled === AnnotateOverlay.Rectangle || styled === AnnotateOverlay.Ellipse
    readonly property bool hasWidth: hasStyle && styled !== AnnotateOverlay.Marker && !hasLetters
    readonly property bool hasEdge: hasColour && styled !== AnnotateOverlay.Marker

    readonly property var tools: [
        { name: "Select", tool: AnnotateOverlay.Select },
        { name: "Line", tool: AnnotateOverlay.Line },
        { name: "Arrow", tool: AnnotateOverlay.Arrow },
        { name: "Rectangle", tool: AnnotateOverlay.Rectangle },
        { name: "Ellipse", tool: AnnotateOverlay.Ellipse },
        { name: "Marker", tool: AnnotateOverlay.Marker },
        { name: "Pixelate", tool: AnnotateOverlay.Pixelate },
        { name: "Text", tool: AnnotateOverlay.Text },
        { name: "Number", tool: AnnotateOverlay.Number },
    ]

    width: Theme.panelWidth
    color: Theme.panel

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
            text: "Annotate"
            font.bold: true
            font.pointSize: Theme.titleSize
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: panel.tools
                Button {
                    required property var modelData
                    objectName: "annotateTool" + modelData.name
                    Layout.fillWidth: true
                    text: modelData.name
                    checkable: true
                    checked: panel.overlay.tool === modelData.tool
                    onClicked: {
                        panel.overlay.tool = modelData.tool
                        panel.doneEditing()
                    }
                }
            }
        }

        Label { text: "Colour"; color: Theme.textMuted; visible: panel.hasColour }
        Flow {
            Layout.fillWidth: true
            spacing: 4
            visible: panel.hasColour
            Repeater {
                model: Theme.annotationColours
                Rectangle {
                    id: swatch
                    required property color modelData
                    readonly property bool current: Qt.colorEqual(panel.overlay.colour, modelData)
                    objectName: "annotateColour"
                    width: 26
                    height: 26
                    color: modelData
                    border.width: current ? 2 : 1
                    border.color: current ? Theme.accent : Theme.swatchFrame
                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.overlay.colour = swatch.modelData
                    }
                }
            }
        }
        TextField {
            objectName: "annotateColourHex"
            Layout.fillWidth: true
            visible: panel.hasColour
            text: panel.overlay.colour.toString()
            onEditingFinished: {
                panel.overlay.colour = text
                panel.doneEditing()
            }
        }

        NumberRow {
            Layout.fillWidth: true
            visible: panel.hasWidth
            label: panel.styled === AnnotateOverlay.Pixelate ? "Block size" : "Line width"
            boxName: "annotateWidth"
            from: 1
            value: Math.round(panel.overlay.lineWidth)
            onModified: (value) => panel.overlay.lineWidth = value
            onAccepted: panel.doneEditing()
        }

        ComboBox {
            id: fontBox
            objectName: "annotateFont"
            Layout.fillWidth: true
            visible: panel.hasLetters
            model: panel.overlay.fontFamilies
            currentIndex: model.indexOf(panel.overlay.fontFamily)
            onActivated: (index) => {
                panel.overlay.fontFamily = model[index]
                panel.doneEditing()
            }
        }
        NumberRow {
            Layout.fillWidth: true
            visible: panel.hasLetters
            label: "Size (pixels)"
            boxName: "annotateTextSize"
            from: 4
            to: 2000
            value: Math.round(panel.overlay.textSize)
            onModified: (value) => panel.overlay.textSize = value
            onAccepted: panel.doneEditing()
        }
        // The words of the text box selected, where Enter starts a new line, or what the
        // number selected says.
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: panel.isNumber ? 40 : 90
            visible: panel.hasLetters && panel.overlay.selected >= 0
            TextArea {
                id: textArea
                objectName: "annotateText"
                placeholderText: "Text"
                wrapMode: TextEdit.Wrap
                text: panel.overlay.text
                onTextChanged: if (activeFocus) panel.overlay.text = text
            }
        }
        Connections {
            target: panel.overlay
            function onTextWanted() { textArea.forceActiveFocus() }
        }

        CheckBox {
            objectName: "annotateBold"
            text: "Bold"
            visible: panel.hasLetters
            checked: panel.overlay.bold
            onToggled: panel.overlay.bold = checked
        }
        CheckBox {
            objectName: "annotateFilled"
            text: "Filled"
            visible: panel.canFill
            checked: panel.overlay.filled
            onToggled: panel.overlay.filled = checked
        }
        CheckBox {
            objectName: "annotateOutline"
            text: "Dark outline"
            visible: panel.hasEdge
            checked: panel.overlay.outline
            onToggled: panel.overlay.outline = checked
        }
        NumberRow {
            Layout.fillWidth: true
            visible: panel.hasEdge && panel.overlay.outline
            label: "Outline width"
            boxName: "annotateOutlineWidth"
            from: 1
            automatic: true
            value: Math.round(panel.overlay.outlineWidth)
            onModified: (value) => panel.overlay.outlineWidth = value
            onAccepted: panel.doneEditing()
        }
        CheckBox {
            objectName: "annotateShadow"
            text: "Shadow"
            visible: panel.hasEdge
            checked: panel.overlay.shadow
            onToggled: panel.overlay.shadow = checked
        }
        NumberRow {
            Layout.fillWidth: true
            visible: panel.hasEdge && panel.overlay.shadow
            label: "Shadow offset"
            boxName: "annotateShadowOffset"
            automatic: true
            value: Math.round(panel.overlay.shadowOffset)
            onModified: (value) => panel.overlay.shadowOffset = value
            onAccepted: panel.doneEditing()
        }
        NumberRow {
            Layout.fillWidth: true
            visible: panel.hasEdge && panel.overlay.shadow
            label: "Shadow blur"
            boxName: "annotateShadowBlur"
            automatic: true
            value: Math.round(panel.overlay.shadowBlur)
            onModified: (value) => panel.overlay.shadowBlur = value
            onAccepted: panel.doneEditing()
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
            text: "Drag to draw. Click a shape to select it, then drag its grips, or press Delete. With Select, drag a shape to move it. With Text, drag a box or click, then type here. Number counts up with each click. Hold Shift for a square, a circle, or a line at steps of 15°. Sizes are in pixels of the image."
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                onClicked: panel.cancelRequested()
            }
            Button {
                objectName: "annotateApply"
                text: "Apply"
                highlighted: true
                enabled: panel.overlay.count > 0
                onClicked: panel.applyRequested()
            }
        }
    }
}

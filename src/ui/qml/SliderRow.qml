import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// A value to set, as tool panels have many of, on two rows: its name and the number in a box,
// which steps with its buttons and can be typed in; then a slider across the width, and an
// arrow that puts the value back to where it starts while it is elsewhere. The slider spreads
// the values as `kind` says (SliderScale). It takes the keys when clicked or reached by Tab:
// the arrows take a step, Page Up and Down ten, Home and End go to the ends, and Enter gives the
// keys back, as Enter in the box does, with doneEditing.
ColumnLayout {
    id: row

    required property string label
    property real value: 0
    property real defaultValue: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 0
    property int kind: SliderScale.Linear

    // The user set a value, by the box, the slider or the reset.
    signal modified(real value)
    // Enter took the value, and the keys can go back to the picture.
    signal doneEditing()

    Layout.fillWidth: true
    spacing: 2

    function set(value) {
        const kept = Math.min(Math.max(value, Math.min(from, to)), Math.max(from, to))
        if (kept !== row.value)
            row.modified(kept)
    }

    SliderScale {
        id: spread
        kind: row.kind
        from: row.from
        to: row.to
        step: row.stepSize
        value: row.value
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.panelSpacing

        Label {
            Layout.fillWidth: true
            text: row.label
            elide: Text.ElideRight
        }
        DecimalSpinBox {
            id: box
            objectName: row.objectName + "Box"
            Layout.preferredWidth: 112
            decimals: row.decimals
            realFrom: row.from
            realTo: row.to
            realStepSize: row.stepSize
            realValue: row.value
            onRealValueModified: (value) => row.set(value)
            Connections {
                target: box.contentItem
                ignoreUnknownSignals: true
                function onAccepted() { row.doneEditing() }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 2

        Slider {
            id: slider
            objectName: row.objectName + "Slider"
            Layout.fillWidth: true
            focusPolicy: Qt.StrongFocus
            from: 0
            to: 1
            value: spread.position
            onMoved: row.set(spread.valueAt(value))
            // Steps of the value, not of the slider's length, which is uneven on some scales.
            Keys.onPressed: (event) => {
                const steps = {
                    [Qt.Key_Right]: 1, [Qt.Key_Up]: 1, [Qt.Key_Left]: -1, [Qt.Key_Down]: -1,
                    [Qt.Key_PageUp]: 10, [Qt.Key_PageDown]: -10
                }
                if (event.key in steps) {
                    row.set(Math.round((row.value + steps[event.key] * row.stepSize) / row.stepSize) * row.stepSize)
                } else if (event.key === Qt.Key_Home) {
                    row.set(row.from)
                } else if (event.key === Qt.Key_End) {
                    row.set(row.to)
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    row.doneEditing()
                } else {
                    return
                }
                event.accepted = true
            }
        }
        IconButton {
            objectName: row.objectName + "Reset"
            focusPolicy: Qt.NoFocus
            iconName: "edit-reset"
            iconSize: 16
            padding: 2
            text: "Reset " + row.label
            enabled: row.value !== row.defaultValue
            onClicked: row.set(row.defaultValue)
        }
    }
}

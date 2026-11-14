import QtQuick
import QtQuick.Templates as T
import QtQuick.Shapes
import ImageViewer

// A number in a field, with up and down stacked at its right end behind a thin line. The
// arrows, the wheel and the arrow keys step it; the field takes typing.
T.SpinBox {
    id: control

    readonly property int stepperWidth: 20

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, Theme.controlHeight)

    padding: 4
    leftPadding: control.mirrored ? stepperWidth + 4 : Theme.controlPadding - 4
    rightPadding: control.mirrored ? Theme.controlPadding - 4 : stepperWidth + 4

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.highlight
        selectedTextColor: Theme.highlightedText
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        clip: width < implicitWidth
    }

    component Chevron: Item {
        id: chevron
        property bool up
        property bool pressed
        property bool usable
        implicitWidth: control.stepperWidth
        implicitHeight: control.height / 2

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Theme.controlRadius - 1
            visible: chevron.pressed
            color: Theme.hover
        }
        Shape {
            anchors.centerIn: parent
            width: 8
            height: 5
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: chevron.usable ? Theme.textMuted : Theme.textDisabled
                strokeWidth: 1.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 1; startY: chevron.up ? 4 : 1
                PathLine { x: 4; y: chevron.up ? 1 : 4 }
                PathLine { x: 7; y: chevron.up ? 4 : 1 }
            }
        }
    }

    up.indicator: Chevron {
        up: true
        pressed: control.up.pressed
        usable: control.enabled && control.value < Math.max(control.from, control.to)
        x: control.mirrored ? 0 : control.width - width
        y: 0
    }

    down.indicator: Chevron {
        up: false
        pressed: control.down.pressed
        usable: control.enabled && control.value > Math.min(control.from, control.to)
        x: control.mirrored ? 0 : control.width - width
        y: control.height / 2
    }

    background: Rectangle {
        implicitWidth: 96
        implicitHeight: Theme.controlHeight
        radius: Theme.controlRadius
        color: control.enabled ? Theme.base : Theme.buttonDisabled
        border.width: 1
        border.color: !control.enabled ? Theme.buttonDisabled : control.hovered ? Theme.light : Theme.mid

        // The line between the number and the arrows.
        Rectangle {
            x: control.mirrored ? control.stepperWidth : parent.width - control.stepperWidth - 1
            y: 4
            width: 1
            height: parent.height - 8
            color: Theme.mid
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.controlRadius + 2
            visible: control.activeFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }
}

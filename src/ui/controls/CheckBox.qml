import QtQuick
import QtQuick.Templates as T
import QtQuick.Shapes
import ImageViewer

// A box that shows when it is off, not only when it is on: a clear edge on the colour of a
// field. On, it is filled with the accent and holds a dark tick; partly on, a dark bar.
T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: 8

    readonly property bool on: checkState !== Qt.Unchecked

    indicator: Rectangle {
        objectName: "checkBoxIndicator"
        implicitWidth: 16
        implicitHeight: 16
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: !control.enabled ? Theme.buttonDisabled : control.on ? Theme.accent : Theme.base
        // Disabled, the edge shows whether it is on or not, and the mark is dimmed.
        border.width: control.on && control.enabled ? 0 : 1
        border.color: !control.enabled ? Theme.textDisabled
                      : control.hovered ? Theme.text : Theme.textMuted

        // The tick, when on.
        Shape {
            anchors.fill: parent
            visible: control.checkState === Qt.Checked
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: control.enabled ? Theme.base : Theme.textDisabled
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 3.5; startY: 8.5
                PathLine { x: 6.5; y: 11.5 }
                PathLine { x: 12.5; y: 4.5 }
            }
        }
        // The bar, when partly on.
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 2
            radius: 1
            visible: control.checkState === Qt.PartiallyChecked
            color: control.enabled ? Theme.base : Theme.textDisabled
        }
        // The focus ring.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: 5
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}

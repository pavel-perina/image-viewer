import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import ImageViewer

// A tool bar button: flat, the icon or text alone, until the mouse is over it (a light
// background), it is pressed (a stronger one) or it is on (a tint of the accent, so that on
// reads apart from pressed). A ring in the accent with keyboard focus.
T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 4
    horizontalPadding: control.display === T.AbstractButton.IconOnly ? 4 : 8
    spacing: 6

    icon.width: 24
    icon.height: 24
    icon.color: textColour

    readonly property color textColour: enabled ? Theme.text : Theme.textDisabled

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.textColour
    }

    background: Item {
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight

        // On: the accent, faint, with an edge of it.
        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            visible: control.checked
            color: Theme.withAlpha(Theme.accent, control.enabled ? 0.35 : 0.12)
            border.width: 1
            border.color: Theme.withAlpha(Theme.accent, control.enabled ? 0.6 : 0.2)
        }
        // Lighter under the mouse, more so while pressed.
        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            visible: control.enabled && (control.hovered || control.down)
            color: control.down ? Theme.withAlpha(Theme.text, 0.2) : Theme.hover
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.controlRadius + 2
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }
}

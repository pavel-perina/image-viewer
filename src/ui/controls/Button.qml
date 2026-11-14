import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import ImageViewer

// Three kinds, by what pressing does:
//  * the default, the button that does the job (highlighted: Apply, Save): a fill of the
//    accent, so it is found at a glance;
//  * an ordinary one (Cancel, Reset): a soft gradient with an edge;
//  * a quiet one (flat): text alone, for actions on the side.
// States are layered on the base: lighter under the mouse, darker while pressed, dimmed when
// disabled, and a ring in the accent with keyboard focus.
T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: Theme.controlPadding
    spacing: 6

    icon.width: 16
    icon.height: 16
    icon.color: textColour

    readonly property bool primary: highlighted && !flat
    readonly property color textColour: !enabled ? Theme.textDisabled
                                        : flat ? Theme.accent
                                        : Theme.text

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
        implicitWidth: 80
        implicitHeight: Theme.controlHeight

        // The base: the accent for the default button, a soft gradient for the others, nothing
        // for a quiet one but while the mouse is over it.
        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            visible: !control.flat
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: !control.enabled ? Theme.buttonDisabled
                           : control.primary ? Theme.defaultButtonTop : Theme.controlTop
                }
                GradientStop {
                    position: 1
                    color: !control.enabled ? Theme.buttonDisabled
                           : control.primary ? Theme.defaultButtonBottom : Theme.controlBottom
                }
            }
            border.width: 1
            border.color: !control.enabled ? Theme.buttonDisabled
                          : control.primary ? Theme.defaultButtonEdge : Theme.light
        }
        // A light line inside the top edge, as light falls from above.
        Rectangle {
            x: Theme.controlRadius
            y: 1
            width: parent.width - 2 * Theme.controlRadius
            height: 1
            visible: !control.flat && control.enabled && !control.down
            color: Theme.withAlpha(Theme.text, control.primary ? 0.18 : 0.08)
        }
        // Lighter under the mouse, darker while pressed.
        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            visible: control.enabled && (control.hovered || control.down || control.checked)
            color: control.down || control.checked ? Theme.withAlpha(Theme.shadow, 0.3) : Theme.hover
        }
        // The focus ring, outside the edge so that it does not cover the fill.
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

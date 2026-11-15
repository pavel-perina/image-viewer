import QtQuick
import QtQuick.Templates as T
import ImageViewer

// A groove sunk into the surface, filled with the accent up to the handle; the handle a light
// disc that the accent rings while it is held.
T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: 4

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width)
                                                     : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2
                                                    : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 16
        implicitHeight: 16
        radius: width / 2
        color: !control.enabled ? Theme.textDisabled : control.pressed ? Theme.accent : Theme.text
        border.width: control.pressed || control.hovered ? 2 : 1
        border.color: !control.enabled ? Theme.buttonDisabled : control.pressed || control.hovered ? Theme.accent : Theme.dark

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: width / 2
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    background: Item {
        implicitWidth: control.horizontal ? 200 : 16
        implicitHeight: control.horizontal ? 16 : 200
        x: control.leftPadding
        y: control.topPadding
        width: control.availableWidth
        height: control.availableHeight

        // The groove.
        Rectangle {
            x: control.horizontal ? 0 : (parent.width - width) / 2
            y: control.horizontal ? (parent.height - height) / 2 : 0
            width: control.horizontal ? parent.width : 4
            height: control.horizontal ? 4 : parent.height
            radius: 2
            color: Theme.base
            border.width: 1
            border.color: Theme.dark

            // Filled up to the handle: from the left, or up from the bottom.
            Rectangle {
                x: 0
                y: control.horizontal ? 0 : control.visualPosition * parent.height
                width: control.horizontal ? control.position * parent.width : parent.width
                height: control.horizontal ? parent.height : (1 - control.visualPosition) * parent.height
                radius: 2
                color: control.enabled ? Theme.accent : Theme.textDisabled
            }
        }
    }
}

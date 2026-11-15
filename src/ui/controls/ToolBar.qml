import QtQuick
import QtQuick.Templates as T
import ImageViewer

// A bar of tool buttons, flat in the window's colour. A dark line parts it from what is beside
// it: the menu bar and the page for the bar at the top, the page for the one at the bottom.
// Fusion's light line along the top stood out too much against a dark window.
T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: 6
    verticalPadding: 3

    background: Rectangle {
        implicitHeight: Theme.controlHeight + 6
        color: Theme.window

        Rectangle {
            objectName: "topLine"
            width: parent.width
            height: 1
            color: Theme.dark
        }
        Rectangle {
            y: parent.height - height
            width: parent.width
            height: 1
            visible: control.position === T.ToolBar.Header
            color: Theme.dark
        }
    }
}

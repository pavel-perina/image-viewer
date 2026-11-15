import QtQuick
import QtQuick.Templates as T
import ImageViewer

// A line between groups of a menu, clear enough to see at a glance.
T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 4
    verticalPadding: 4

    contentItem: Rectangle {
        implicitWidth: 120
        implicitHeight: 1
        color: Theme.light
    }
}

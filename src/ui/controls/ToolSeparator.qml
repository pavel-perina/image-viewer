import QtQuick
import QtQuick.Templates as T
import ImageViewer

// A line between groups of tool bar buttons, as the one between a spin box's number and its
// arrows: thin, a little shorter than the buttons, with room on each side.
T.ToolSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: vertical ? 6 : 2
    verticalPadding: vertical ? 2 : 6

    contentItem: Rectangle {
        implicitWidth: control.vertical ? 1 : 20
        implicitHeight: control.vertical ? 20 : 1
        color: Theme.mid
    }
}

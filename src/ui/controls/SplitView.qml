import QtQuick
import QtQuick.Templates as T
import ImageViewer

// Panes side by side or one above another, with the same gap between them either way: a
// gap of the window's colour, which shows a line only under the mouse and while dragged, as
// the panes' own colours or frames already set them apart. It is narrow, but grabbed over a
// wider strip than it takes.
T.SplitView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    handle: Item {
        id: handle
        readonly property bool across: control.orientation === Qt.Horizontal
        readonly property int grabMargin: 3
        readonly property bool pressed: T.SplitHandle.pressed
        readonly property bool hovered: T.SplitHandle.hovered
        implicitWidth: across ? Theme.paneGap : control.width
        implicitHeight: across ? control.height : Theme.paneGap

        containmentMask: Item {
            x: handle.across ? -handle.grabMargin : 0
            y: handle.across ? 0 : -handle.grabMargin
            width: handle.width + (handle.across ? 2 * handle.grabMargin : 0)
            height: handle.height + (handle.across ? 0 : 2 * handle.grabMargin)
        }

        Rectangle {
            x: handle.across ? (handle.width - 2) / 2 : 0
            y: handle.across ? 0 : (handle.height - 2) / 2
            width: handle.across ? 2 : parent.width
            height: handle.across ? parent.height : 2
            visible: handle.hovered || handle.pressed
            color: handle.pressed ? Theme.accent : Theme.light
        }
    }
}

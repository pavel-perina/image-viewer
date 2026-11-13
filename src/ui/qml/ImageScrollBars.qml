import QtQuick
import ImageViewer

// Handles over a zoomed image, showing which part of it is visible: pixels 1000-2000 of 4000 give
// a handle from 25% to 50% of the view. They show while the image moves or zooms and fade out
// about half a second after it stops. They only indicate, as the image itself can be dragged, so
// they take no mouse input from the image or from the panel at the edge.
Item {
    id: root

    required property ImageView view
    readonly property rect area: view.visibleArea
    readonly property int margin: 2
    readonly property int breadth: 8
    readonly property int shortest: 32

    opacity: 0

    onAreaChanged: {
        fade.stop()
        opacity = 1
        still.restart()
    }

    Timer {
        id: still
        interval: 200
        onTriggered: fade.start()
    }

    NumberAnimation {
        id: fade
        target: root
        property: "opacity"
        to: 0
        duration: 300
    }

    // Over an image of any brightness, so it does not use the colour scheme: a light handle
    // with a dark edge shows on both dark and bright pixels.
    component Handle: Rectangle {
        property real start
        property real size
        property real track
        // Rounding in the view's whole-pixel offsets should not show a handle for a fitted image.
        readonly property bool needed: size < 0.999
        readonly property real length: Math.min(track, Math.max(root.shortest, size * track))
        // With the shortest length, the last position still has to reach the end of the track.
        readonly property real offset: root.margin + (needed ? start / (1 - size) * (track - length) : 0)

        visible: needed
        radius: 3
        color: Theme.scrollHandle
        border.width: 1
        border.color: Theme.scrollHandleEdge
    }

    Handle {
        id: horizontalHandle
        objectName: "horizontalHandle"
        start: root.area.x
        size: root.area.width
        track: root.width - 2 * root.margin - (verticalHandle.needed ? root.breadth + root.margin : 0)
        x: offset
        y: root.height - root.margin - height
        width: length
        height: root.breadth
    }

    Handle {
        id: verticalHandle
        objectName: "verticalHandle"
        start: root.area.y
        size: root.area.height
        track: root.height - 2 * root.margin - (horizontalHandle.needed ? root.breadth + root.margin : 0)
        x: root.width - root.margin - width
        y: offset
        width: root.breadth
        height: length
    }
}

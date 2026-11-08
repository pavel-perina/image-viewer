import QtQuick
import QtQuick.Controls
import ImageViewer

// Two pictures of the same size, one each side of a line that can be dragged: the first surface
// shows left of it and the second, on top, right of it. While active their views zoom and scroll
// together. The line stays at its place in the window, whatever the zoom. Place it over the
// second surface, with the first under it at the same place.
Item {
    id: splitter

    required property ImageSurface first
    required property ImageSurface second
    property bool active: false
    property real position: 0.5  // of the width
    // Shows the first picture alone, as when holding a key to see the original.
    property bool firstOnly: false
    property string firstLabel: "Before"
    property string secondLabel: "After"

    readonly property real lineX: {
        const dpr = Window.window ? Window.window.devicePixelRatio : 1
        return Math.round((firstOnly ? 1 : position) * width * dpr) / dpr
    }

    visible: active

    Binding { target: splitter.second; property: "clipLeft"; value: splitter.lineX; when: splitter.active }
    Binding { target: splitter.first.view; property: "linkedView"; value: splitter.second.view; when: splitter.active }
    Binding { target: splitter.second.view; property: "linkedView"; value: splitter.first.view; when: splitter.active }

    component Tag: Label {
        padding: 4
        leftPadding: 8
        rightPadding: 8
        color: "#f0f0f0"
        background: Rectangle { color: "#a0181818"; radius: 3 }
    }

    Tag {
        objectName: "compareFirstLabel"
        x: 8
        y: 8
        visible: text !== "" && splitter.lineX > width + 16
        text: splitter.firstLabel
    }
    Tag {
        objectName: "compareSecondLabel"
        x: splitter.width - width - 8
        y: 8
        visible: text !== "" && !splitter.firstOnly && splitter.width - splitter.lineX > width + 16
        text: splitter.secondLabel
    }

    // The line, with a grip in the middle; it alone takes the mouse.
    Item {
        id: handle
        objectName: "compareHandle"
        visible: !splitter.firstOnly
        x: splitter.lineX - width / 2
        width: 16
        height: splitter.height

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 2
            height: parent.height
            color: "#e0e0e0"
            opacity: 0.85
        }
        Rectangle {
            anchors.centerIn: parent
            width: 16
            height: 40
            radius: 4
            color: "#e0e0e0"
            border.color: "#404040"
            Column {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    Rectangle { width: 6; height: 1; color: "#404040" }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            property real grab: 0
            onPressed: (mouse) => grab = mouse.x - width / 2
            onPositionChanged: (mouse) => {
                const x = handle.mapToItem(splitter, mouse.x - grab, 0).x
                splitter.position = Math.max(0, Math.min(1, x / splitter.width))
            }
            onDoubleClicked: splitter.position = 0.5
        }
    }
}

import QtQuick
import QtQuick.Controls
import ImageViewer

// Shows while the mouse is over what it scrolls, or while that moves, then fades out. It can be
// invisible otherwise because the mouse has to cross the list to reach it, which reveals it.
ScrollBar {
    id: control

    property int thickness: 12
    readonly property bool revealed: size < 1 && (areaHover.hovered || active || pressed || recentlyMoved.running)

    padding: 2
    hoverEnabled: true
    policy: size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    opacity: revealed ? 1 : 0
    // A folder of many thousands of files still leaves a handle that can be grabbed.
    minimumSize: Math.min(1, 32 / Math.max(1, orientation === Qt.Horizontal ? width : height))

    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }

    // Keys and new folders move the list without the mouse.
    onPositionChanged: recentlyMoved.restart()

    Timer {
        id: recentlyMoved
        interval: 800
    }

    HoverHandler {
        id: areaHover
        parent: control.parent
    }

    contentItem: Rectangle {
        implicitWidth: control.thickness - 2 * control.padding
        implicitHeight: control.thickness - 2 * control.padding
        radius: 2
        color: Theme.withAlpha(Theme.accent, control.pressed ? 0.6 : control.hovered ? 0.4 : 0.2)
    }

    background: Item {
        implicitWidth: control.thickness
        implicitHeight: control.thickness
    }
}

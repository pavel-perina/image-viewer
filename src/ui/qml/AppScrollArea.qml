import QtQuick
import QtQuick.Controls
import ImageViewer

// A vertical viewport shared by settings pages and tool panels. Its content has a
// fixed scrollbar gutter, so wrapped fields do not change width as overflow changes.
Flickable {
    id: scrollArea

    property alias verticalScrollBar: verticalBar
    property alias autoHideScrollBar: verticalBar.autoHide
    readonly property real availableWidth: Math.max(0, width - Theme.panelMargin)
    readonly property real availableHeight: height
    default property alias content: contents.data

    contentWidth: width
    contentHeight: contents.childrenRect.height
    clip: true
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    interactive: contentHeight > height

    Item {
        id: contents
        width: scrollArea.availableWidth
        height: childrenRect.height
    }

    ScrollBar.vertical: AppScrollBar {
        id: verticalBar
        objectName: "scrollAreaScrollBar"
        parent: scrollArea
        orientation: Qt.Vertical
        x: scrollArea.width - width
        y: 0
        height: scrollArea.height
        interactive: scrollArea.interactive
    }

    // Tab navigation brings a focused field into the viewport, in either context.
    function reveal(item) {
        if (!item || !interactive) return
        for (let up = item; up; up = up.parent) {
            if (up === contents) {
                const top = item.mapToItem(contents, 0, 0).y
                const bottom = top + item.height
                if (top < contentY) contentY = Math.max(0, top)
                else if (bottom > contentY + height) contentY = Math.min(bottom - height, contentHeight - height)
                return
            }
        }
    }
    Connections {
        target: scrollArea.Window.window
        function onActiveFocusItemChanged() { scrollArea.reveal(scrollArea.Window.activeFocusItem) }
    }
}

import QtQuick
import ImageViewer

// Shared selection grammar for navigation rows, file rows and thumbnails. A row can
// carry more fill than a large thumbnail, but focus and edges mean the same thing.
Rectangle {
    property bool selected: false
    property bool focused: false
    property bool subtle: false
    property bool hovered: false

    radius: Theme.controlRadius
    color: !selected ? (hovered ? Theme.hover : "transparent")
           : subtle ? (focused ? Theme.thumbnailSelection : Theme.thumbnailSelectionInactive)
           : focused ? Theme.listSelection : Theme.listSelectionInactive
    border.width: selected ? 1 : 0
    border.color: focused ? Theme.selectionEdge : Theme.selectionEdgeInactive
}

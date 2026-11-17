pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import ImageViewer

// The entries of a FolderListing, either as names in columns that run top to bottom and scroll
// sideways (brief), or as rows with size, type and date under headers that sort (details). One
// entry is current. Up and Down move by one, Left and Right by a column in brief, Enter or a
// double click activates the current entry and Backspace asks for the parent folder.
FocusScope {
    id: list

    required property FolderListing listing
    property bool details: false
    property int currentIndex: -1

    signal clicked(int index)
    signal activated(int index)
    signal upRequested()

    readonly property int rowHeight: Math.ceil(fontMetrics.height + 8)
    readonly property real sizeWidth: 90
    readonly property real typeWidth: 170
    readonly property real modifiedWidth: 150
    readonly property Item view: details ? detailsView : briefView
    // Rows in one column of the brief view.
    readonly property int columnRows: Math.max(1, Math.floor(briefView.height / rowHeight))

    // The current entry as a map of the listing's roles, or an empty one.
    function currentEntry() {
        return listing.get(currentIndex)
    }

    function moveTo(index) {
        if (listing.count === 0)
            return
        currentIndex = Math.max(0, Math.min(listing.count - 1, index))
        view.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    function select(name) {
        const index = listing.indexOf(name)
        if (index >= 0)
            moveTo(index)
        else
            currentIndex = -1
    }

    // The same entry stays current when the folder is read again, as after a file appears.
    property string currentName
    onCurrentIndexChanged: currentName = currentIndex >= 0 ? (listing.get(currentIndex).name ?? "") : ""
    Connections {
        target: list.listing
        function onModelReset() {
            const index = list.currentName ? list.listing.indexOf(list.currentName) : -1
            list.currentIndex = index
            if (index >= 0)
                list.view.positionViewAtIndex(index, ListView.Contain)
        }
        function onPathChanged() {
            list.currentName = ""
            list.currentIndex = -1
        }
    }

    Keys.onPressed: (event) => {
        const page = details ? Math.max(1, Math.floor(detailsView.height / rowHeight) - 1) : columnRows
        const step = { [Qt.Key_Up]: -1, [Qt.Key_Down]: 1, [Qt.Key_PageUp]: -page, [Qt.Key_PageDown]: page }
        if (!details) {
            step[Qt.Key_Left] = -columnRows
            step[Qt.Key_Right] = columnRows
        }
        if (event.modifiers & ~Qt.KeypadModifier)
            return
        if (step[event.key] !== undefined) {
            moveTo(currentIndex < 0 ? 0 : currentIndex + step[event.key])
        } else if (event.key === Qt.Key_Home) {
            moveTo(0)
        } else if (event.key === Qt.Key_End) {
            moveTo(listing.count - 1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (currentIndex >= 0)
                activated(currentIndex)
        } else if (event.key === Qt.Key_Backspace) {
            upRequested()
        } else {
            return
        }
        event.accepted = true
    }

    FontMetrics {
        id: fontMetrics
    }

    ListFrame {
        objectName: "fileListFrame"
        anchors.fill: parent
        focused: list.activeFocus
    }

    Label {
        anchors.centerIn: parent
        visible: list.listing.count === 0
        text: list.listing.loading ? "Reading the folder..." : "Nothing here"
        opacity: 0.6
    }

    GridView {
        id: briefView
        objectName: "briefView"
        anchors.fill: parent
        anchors.margins: Theme.paneInset
        visible: !list.details
        clip: true
        flow: GridView.FlowTopToBottom
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        model: list.listing
        cellHeight: list.rowHeight
        cellWidth: 240
        ScrollBar.horizontal: AppScrollBar {}
        delegate: Entry {
            width: briefView.cellWidth
        }
        // Clears nothing, but takes the keys from a click between entries.
        TapHandler {
            onTapped: list.forceActiveFocus()
        }
    }

    ListView {
        id: detailsView
        objectName: "detailsView"
        anchors.fill: parent
        anchors.margins: Theme.paneInset
        visible: list.details
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        model: list.listing
        headerPositioning: ListView.OverlayHeader
        ScrollBar.vertical: AppScrollBar {}
        header: Rectangle {
            z: 2
            width: detailsView.width
            height: list.rowHeight
            color: Theme.header
            Row {
                anchors.fill: parent
                HeaderCell { width: parent.width - list.sizeWidth - list.typeWidth - list.modifiedWidth; text: "Name"; key: FolderListing.Name }
                HeaderCell { width: list.sizeWidth; text: "Size"; key: FolderListing.Size; alignRight: true }
                HeaderCell { width: list.typeWidth; text: "Type"; key: FolderListing.Type }
                HeaderCell { width: list.modifiedWidth; text: "Modified"; key: FolderListing.Modified }
            }
        }
        delegate: Entry {
            width: detailsView.width
        }
        TapHandler {
            onTapped: list.forceActiveFocus()
        }
    }

    // A column header: a click sorts by it, a second click turns the order around.
    component HeaderCell: Item {
        id: cell
        property string text
        property int key
        property bool alignRight: false
        readonly property bool sorted: list.listing.sortKey === key
        height: parent.height
        Label {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: cell.alignRight ? Text.AlignRight : Text.AlignLeft
            text: cell.text + (cell.sorted ? (list.listing.sortDescending ? "  ▾" : "  ▴") : "")
            font.weight: cell.sorted ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.withAlpha(list.palette.windowText, 0.15)
        }
        TapHandler {
            onTapped: {
                if (cell.sorted) {
                    list.listing.sortDescending = !list.listing.sortDescending
                } else {
                    list.listing.sortKey = cell.key
                    list.listing.sortDescending = false
                }
            }
        }
    }

    component Entry: Item {
        id: entry
        required property int index
        required property string name
        required property bool isFolder
        required property string sizeText
        required property string type
        required property string modifiedText
        readonly property bool current: index === list.currentIndex
        readonly property color textColor: current && list.activeFocus ? list.palette.highlightedText : list.palette.text

        height: list.rowHeight
        SelectionBackground {
            anchors.fill: parent
            anchors.margins: 1
            selected: entry.current
            focused: list.activeFocus
        }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 6
            spacing: 6

            Item {
                width: 16
                height: parent.height
                FolderGlyph {
                    anchors.centerIn: parent
                    visible: entry.isFolder
                }
            }
            Label {
                width: (list.details ? parent.width - list.sizeWidth - list.typeWidth - list.modifiedWidth
                                     : parent.width) - 28
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: entry.name
                color: entry.textColor
                elide: Text.ElideMiddle
            }
            Label {
                visible: list.details
                width: list.sizeWidth - 6
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignRight
                text: entry.sizeText
                color: entry.textColor
            }
            Label {
                visible: list.details
                width: list.typeWidth - 6
                leftPadding: 6
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: entry.type
                color: entry.textColor
                elide: Text.ElideRight
            }
            Label {
                visible: list.details
                width: list.modifiedWidth - 6
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: entry.modifiedText
                color: entry.textColor
                elide: Text.ElideRight
            }
        }

        TapHandler {
            onTapped: {
                list.forceActiveFocus()
                list.currentIndex = entry.index
                list.clicked(entry.index)
            }
            onDoubleTapped: list.activated(entry.index)
        }
    }

    // A folder drawn from two shapes, until there are icons.
    component FolderGlyph: Item {
        width: 15
        height: 12
        Rectangle {
            x: 0
            y: 0
            width: 7
            height: 4
            radius: 1
            color: Theme.folderShade
        }
        Rectangle {
            y: 2
            width: parent.width
            height: parent.height - 2
            radius: 1.5
            color: Theme.folder
        }
    }
}

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Directory tree and preview on the left; breadcrumb and thumbnails on the right.
SplitView {
    id: page

    required property BrowseContext context
    required property bool active
    readonly property alias preview: preview
    readonly property alias pathBar: pathBar

    // Emitted when the user wants the current image in the large view.
    signal viewRequested()
    // A right click on the files, or on a folder of the tree.
    signal fileMenuRequested()
    signal folderMenuRequested(string path)

    function moveCurrent(rows) {
        page.context.setCurrentIndex(page.context.currentIndex + rows * grid.columns)
    }

    function extendSelection(rows) {
        page.context.extendSelectionTo(page.context.currentIndex + rows * grid.columns)
    }

    orientation: Qt.Horizontal

    // The side buttons of a mouse.
    TapHandler {
        acceptedButtons: Qt.BackButton
        onTapped: page.context.goBack()
    }
    TapHandler {
        acceptedButtons: Qt.ForwardButton
        onTapped: page.context.goForward()
    }

    SplitView {
        orientation: Qt.Vertical
        SplitView.preferredWidth: 300
        SplitView.minimumWidth: 120

        TreeView {
            id: tree
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80
            clip: true
            model: DirectoryTreeModel {
                id: treeModel
            }

            // Opens the folder too, so its subfolders show. Only when arriving at a folder, so
            // that one the user has collapsed stays that way.
            property string revealedPath

            function reveal(path) {
                if (path === revealedPath)
                    return
                revealedPath = path
                const index = treeModel.indexForPath(path)
                expandToIndex(index)
                forceLayout()
                const row = rowAtIndex(index)
                expand(row)
                positionViewAtRow(row, TableView.Contain)
            }

            delegate: TreeViewDelegate {
                id: treeRow
                implicitWidth: tree.width
                // Rows inserted above can leave this row showing what it held before, reading
                // "c" while it opens "b", so both of these read the model again when they appear.
                highlighted: {
                    tree.rows
                    return treeModel.pathForIndex(tree.index(row, 0)) === page.context.directory
                }
                contentItem: Label {
                    text: {
                        tree.rows
                        treeRow.model.display  // for a rename, which keeps the number of rows
                        return treeModel.data(tree.index(treeRow.row, 0)) ?? ""
                    }
                    elide: Text.ElideRight
                    visible: !treeRow.editing
                }
                TapHandler {
                    // The arrow belongs to expanding and collapsing, not to opening.
                    function onArrow(point) {
                        return point.position.x < treeRow.indicator.x + treeRow.indicator.width
                    }
                    onTapped: (point) => {
                        if (onArrow(point))
                            return
                        const path = treeModel.pathForIndex(tree.index(treeRow.row, 0))
                        if (path !== page.context.directory)
                            page.context.openPath(path)
                    }
                    // The delegate toggles on a double click, which would close what the first
                    // click just opened. Later, so that it comes after the toggle.
                    onDoubleTapped: (point) => {
                        if (!onArrow(point))
                            Qt.callLater(tree.expand, treeRow.row)
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: page.folderMenuRequested(treeModel.pathForIndex(tree.index(treeRow.row, 0)))
                }
            }

            ScrollBar.vertical: AppScrollBar {}

            Connections {
                target: page.context
                function onDirectoryChanged() {
                    tree.reveal(page.context.directory)
                }
            }
        }

        Rectangle {
            SplitView.preferredHeight: 260
            SplitView.minimumHeight: 80
            color: "#202020"

            ImageView {
                id: preview
                anchors.fill: parent
                clip: true
                source: page.active ? page.context.currentPath : ""
            }

            ImageScrollBars {
                anchors.fill: preview
                view: preview
            }

            Label {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                color: "#e0e0e0"
                visible: preview.status === ImageView.Error
                text: preview.errorString
            }
        }
    }

    ColumnLayout {
        spacing: 0

        PathBar {
            id: pathBar
            Layout.fillWidth: true
            context: page.context
        }

        GridView {
            id: grid

            readonly property int columns: Math.max(1, Math.floor(width / cellWidth))

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 180
            cellHeight: 200
            cacheBuffer: 400
            keyNavigationEnabled: false
            model: page.context.files
            currentIndex: page.context.currentIndex
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)

            ScrollBar.vertical: AppScrollBar {}

            // A click between the thumbnails clears the selection.
            TapHandler {
                onTapped: (point) => {
                    const x = point.position.x + grid.contentX
                    const y = point.position.y + grid.contentY
                    if (grid.indexAt(x, y) < 0)
                        page.context.clearSelection()
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (point) => {
                    if (grid.indexAt(point.position.x + grid.contentX, point.position.y + grid.contentY) < 0)
                        page.fileMenuRequested()
                }
            }

            delegate: Item {
                id: cell

                required property int index
                required property string name
                required property string thumbnail
                required property bool selected

                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    color: cell.selected ? palette.highlight : "transparent"
                    // The current image is shown large, and can be outside the selection.
                    border.width: cell.index === page.context.currentIndex ? 2 : 0
                    border.color: palette.highlightedText
                }

                Image {
                    id: thumbnailImage
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.bottomMargin: 28
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    source: cell.thumbnail
                }

                Label {
                    anchors.centerIn: thumbnailImage
                    visible: thumbnailImage.status === Image.Error
                    text: "Cannot read"
                }

                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    text: cell.name
                }

                TapHandler {
                    id: cellTap
                    onTapped: {
                        // On the handler's point: the signal's own argument has no modifiers.
                        const modifiers = cellTap.point.modifiers
                        // The second tap of a double click only views what the first one chose.
                        if (cellTap.tapCount === 1) {
                            page.context.clickFile(cell.index, (modifiers & Qt.ControlModifier) !== 0,
                                                   (modifiers & Qt.ShiftModifier) !== 0)
                        }
                    }
                    onDoubleTapped: page.viewRequested()
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        // On a selected file the menu is for the whole selection.
                        if (!cell.selected)
                            page.context.clickFile(cell.index, false, false)
                        page.fileMenuRequested()
                    }
                }
            }
        }
    }
}

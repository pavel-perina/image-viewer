import QtQuick
import QtQuick.Controls
import ImageViewer

// The folders of the file system as a tree that follows a browsing context: a click opens a
// folder there, and the folder it opens is revealed and highlighted.
TreeView {
    id: tree

    required property BrowseContext context
    signal folderMenuRequested(string path)

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
        treeModel.readParentsOf(path)
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
            treeRow.model.display  // rows sorted in place keep their number and change their folder
            return treeModel.pathForIndex(tree.index(row, 0)) === tree.context.directory
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
                if (path !== tree.context.directory)
                    tree.context.openPath(path)
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
            onTapped: tree.folderMenuRequested(treeModel.pathForIndex(tree.index(treeRow.row, 0)))
        }
    }

    ScrollBar.vertical: AppScrollBar {}

    Connections {
        target: tree.context
        function onDirectoryChanged() {
            tree.reveal(tree.context.directory)
        }
    }

    // Made later than the folder was opened, as in a file chooser.
    Component.onCompleted: {
        if (context.directory)
            reveal(context.directory)
    }

    // Its parents have filled in above it, which pushes it down, perhaps out of sight.
    // In the middle, as its own subfolders come below it.
    Connections {
        target: treeModel
        function onDirectoryLoaded(path) {
            if (tree.revealedPath.startsWith(path) && path !== tree.revealedPath) {
                Qt.callLater(() => {
                    tree.forceLayout()
                    tree.positionViewAtRow(tree.rowAtIndex(treeModel.indexForPath(tree.revealedPath)), TableView.AlignVCenter)
                })
            }
        }
    }
}

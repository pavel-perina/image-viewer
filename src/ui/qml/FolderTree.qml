import QtQuick
import QtQuick.Controls
import ImageViewer

// The folders of the file system as a tree that follows a browsing context: a click opens a
// folder there, and the folder it opens is revealed and highlighted.
TreeView {
    id: tree

    required property BrowseContext context
    signal folderMenuRequested(string path)
    signal renameRequested(string path)
    signal trashRequested(string path)
    // Enter: back to the files of the folder.
    signal filesRequested()
    signal filesDropped(var paths, string folder, bool moving)

    // The folder files are dragged over, shown framed.
    property string dropPath: ""

    clip: true
    activeFocusOnTab: true
    keyNavigationEnabled: false
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

    function pathAt(row) {
        return treeModel.pathForIndex(tree.index(row, 0))
    }

    // Opens the folder of a row without unfolding it, so that moving down does not descend into
    // every folder on the way.
    function openRow(row) {
        if (row < 0 || row >= rows)
            return
        const path = pathAt(row)
        revealedPath = path
        context.openPath(path)
        positionViewAtRow(row, TableView.Contain)
    }

    // Keys as in a file manager's tree. Left collapses an open folder, otherwise goes to its parent;
    // Right opens a closed one, otherwise goes to its first subfolder.
    function handleKey(event) {
        if ((event.modifiers & ~Qt.KeypadModifier) !== 0)
            return false
        const row = rowAtIndex(treeModel.indexForPath(context.directory))
        switch (event.key) {
        case Qt.Key_Up: openRow(row < 0 ? 0 : row - 1); break
        case Qt.Key_Down: openRow(row + 1); break
        case Qt.Key_Left:
            if (row >= 0 && isExpanded(row))
                collapse(row)
            else
                context.goUp()
            break
        case Qt.Key_Right:
            if (row < 0)
                break
            if (!isExpanded(row))
                expand(row)
            else if (row + 1 < rows && depth(row + 1) > depth(row))
                openRow(row + 1)
            break
        case Qt.Key_Backspace: context.goUp(); break
        case Qt.Key_F2: if (context.directory) renameRequested(context.directory); break
        case Qt.Key_Delete: if (context.directory) trashRequested(context.directory); break
        case Qt.Key_Return:
        case Qt.Key_Enter: filesRequested(); break
        default: return false
        }
        return true
    }

    Keys.onPressed: (event) => event.accepted = handleKey(event)
    // Taken from the window's shortcuts, where they would rename or delete the files instead.
    // Shift+Del too, which does nothing to a folder.
    Keys.onShortcutOverride: (event) => {
        const plain = (event.modifiers & ~Qt.KeypadModifier) === 0
        event.accepted = (plain && event.key === Qt.Key_F2)
                || (event.key === Qt.Key_Delete && (plain || event.modifiers === Qt.ShiftModifier))
    }

    // Files dropped on a folder are copied there, or moved with Shift. Not into the folder they
    // are all in already, which would only ask about each one.
    DropArea {
        id: dropArea
        // On the view itself, not in its scrolling content where children otherwise go.
        parent: tree
        anchors.fill: parent
        keys: ["text/uri-list"]

        function folderAt(x, y) {
            const point = tree.mapToItem(tree.contentItem, x, y)
            const cell = tree.cellAtPosition(point, true)
            return cell.y >= 0 && cell.y < tree.rows ? tree.pathAt(cell.y) : ""
        }

        function pathsOf(urls) {
            return urls.map(url => tree.context.pathFromUrl(url)).filter(path => path !== "")
        }

        function update(drag) {
            const folder = folderAt(drag.x, drag.y)
            const paths = pathsOf(drag.urls)
            const parentOf = (path) => path.substring(0, path.lastIndexOf("/")) || "/"
            // Not while files are being copied, as the drop would then be taken and lost.
            const usable = folder !== "" && paths.length > 0 && !tree.context.busy
                    && !paths.every(path => parentOf(path) === folder)
                    && !paths.some(path => folder === path || folder.startsWith(path + "/"))
            tree.dropPath = usable ? folder : ""
            drag.accepted = usable
        }

        onEntered: (drag) => update(drag)
        onPositionChanged: (drag) => update(drag)
        onExited: tree.dropPath = ""
        onDropped: (drop) => {
            update(drop)
            const folder = tree.dropPath
            tree.dropPath = ""
            if (!folder)
                return
            // The other side is told it was a copy in any case, so that it never deletes what
            // was not moved here, as when a file of that name is skipped.
            drop.accept(Qt.CopyAction)
            tree.filesDropped(pathsOf(drop.urls), folder, drop.proposedAction === Qt.MoveAction)
        }
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
        // Framed while files are dragged over it.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: 1
            border.color: palette.highlight
            visible: tree.dropPath !== "" && tree.dropPath === treeModel.pathForIndex(tree.index(treeRow.row, 0))
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
                tree.forceActiveFocus()
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
            onTapped: {
                tree.forceActiveFocus()
                tree.folderMenuRequested(treeModel.pathForIndex(tree.index(treeRow.row, 0)))
            }
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

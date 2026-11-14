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
    readonly property alias preview: previewPanel.view
    property bool sharpResampling: false
    readonly property alias pathBar: pathBar

    // Emitted when the user wants the current image in the large view.
    signal viewRequested()
    // A right click on the files, or on a folder of the tree.
    signal fileMenuRequested()
    signal folderMenuRequested(string path)
    // F2 and Del in the folder tree.
    signal folderRenameRequested(string path)
    signal folderTrashRequested(string path)
    // Files dropped on a folder of the tree, from the thumbnails or from another application.
    signal filesDropped(var paths, string folder, bool moving)

    // While thumbnails are dragged out, so that the window does not open them when they are
    // dropped back on it.
    property bool draggingFiles: false

    readonly property bool filesFocused: grid.activeFocus
    readonly property bool treeFocused: folderTree.activeFocus
    readonly property Item folderTree: folderTree

    // The thumbnails take the keys unless the tree or the path bar has them.
    function focusFiles() {
        grid.forceActiveFocus()
    }

    // A full page of whole rows, stopping at the first or last image rather than not moving.
    function pageTarget(pages) {
        const rows = Math.max(1, Math.floor(grid.height / grid.cellHeight))
        return Math.max(0, Math.min(page.context.fileCount - 1, page.context.currentIndex + pages * rows * grid.columns))
    }

    function movePage(pages) {
        page.context.setCurrentIndex(pageTarget(pages))
    }

    function extendSelectionByPage(pages) {
        page.context.extendSelectionTo(pageTarget(pages))
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

        // The folders under a caption as the preview has, though they do not collapse.
        ColumnLayout {
            SplitView.fillHeight: true
            SplitView.minimumHeight: 80
            spacing: 0

            PaneHeader {
                objectName: "foldersHeader"
                Layout.fillWidth: true
                Layout.preferredHeight: previewPanel.headerHeight
                text: "Folders"
            }

            FolderTree {
                id: folderTree
                objectName: "folderTree"
                Layout.fillWidth: true
                Layout.fillHeight: true
                context: page.context
                onFolderMenuRequested: (path) => page.folderMenuRequested(path)
                onRenameRequested: (path) => page.folderRenameRequested(path)
                onTrashRequested: (path) => page.folderTrashRequested(path)
                onFilesRequested: grid.forceActiveFocus()
                onFilesDropped: (paths, folder, moving) => page.filesDropped(paths, folder, moving)
            }
        }

        PreviewPanel {
            id: previewPanel
            context: page.context
            active: page.active
            sharp: page.sharpResampling
            SplitView.preferredHeight: 300
            SplitView.minimumHeight: collapsed ? headerHeight : 140
            SplitView.maximumHeight: collapsed ? headerHeight : Number.POSITIVE_INFINITY
        }
    }

    ColumnLayout {
        spacing: 0

        PathBar {
            id: pathBar
            Layout.fillWidth: true
            context: page.context
            onFinished: grid.forceActiveFocus()
        }

        GridView {
            id: grid
            objectName: "thumbnailGrid"

            readonly property int columns: Math.max(1, Math.floor(width / cellWidth))

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: 180
            cellHeight: 200
            cacheBuffer: 400
            keyNavigationEnabled: false
            activeFocusOnTab: true
            model: page.context.files
            currentIndex: page.context.currentIndex
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)

            ScrollBar.vertical: AppScrollBar {}

            // Moving with Shift chooses everything from the anchor to where it stops.
            Keys.onPressed: (event) => {
                const shift = (event.modifiers & Qt.ShiftModifier) !== 0
                if ((event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier)) !== 0)
                    return
                const context = page.context
                const go = (index) => shift ? context.extendSelectionTo(index) : context.setCurrentIndex(index)
                switch (event.key) {
                case Qt.Key_Left: shift ? go(context.currentIndex - 1) : context.previous(); break
                case Qt.Key_Right: shift ? go(context.currentIndex + 1) : context.next(); break
                case Qt.Key_Up: go(context.currentIndex - grid.columns); break
                case Qt.Key_Down: go(context.currentIndex + grid.columns); break
                case Qt.Key_PageUp: go(page.pageTarget(-1)); break
                case Qt.Key_PageDown: go(page.pageTarget(1)); break
                case Qt.Key_Home: go(0); break
                case Qt.Key_End: go(context.fileCount - 1); break
                case Qt.Key_Space: if (!shift) context.next(); break
                case Qt.Key_Backspace: if (!shift) context.goUp(); break
                case Qt.Key_Return:
                case Qt.Key_Enter:
                    if (shift || context.currentPath === "")
                        return
                    page.viewRequested()
                    break
                default:
                    return
                }
                event.accepted = true
            }

            // A click on empty space clears the selection: the free places after the last
            // thumbnail, the strip right of the last column, and below the last row. Where it
            // was pressed counts, not where it was let go: the thumbnail's own handler takes the
            // release first and may scroll the grid to show it, which moves what lies under the
            // release. The handlers of a grid belong to its content item, so the point is in
            // the content already, scrolled or not.
            TapHandler {
                id: emptyTap
                onTapped: {
                    grid.forceActiveFocus()
                    if (grid.indexAt(emptyTap.point.pressPosition.x, emptyTap.point.pressPosition.y) < 0)
                        page.context.clearSelection()
                }
            }

            TapHandler {
                id: emptyMenuTap
                acceptedButtons: Qt.RightButton
                onTapped: {
                    grid.forceActiveFocus()
                    if (grid.indexAt(emptyMenuTap.point.pressPosition.x, emptyMenuTap.point.pressPosition.y) < 0)
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
                    // Paler while the keys are elsewhere, as they then do not act on it.
                    color: !cell.selected ? "transparent" : grid.activeFocus ? palette.highlight : Theme.withAlpha(palette.highlight, 0.5)
                    // The current image is shown large, and can be outside the selection.
                    border.width: cell.index === page.context.currentIndex ? 2 : 0
                    border.color: palette.highlightedText
                }

                Item {
                    id: thumbnailArea
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.bottomMargin: 28

                    // Made at the size it is shown, one pixel for one, so nothing filters it
                    // again. Centring rounds to whole pixels, which keeps that exact.
                    Image {
                        id: thumbnailImage
                        objectName: "thumbnailImage"
                        anchors.centerIn: parent
                        asynchronous: true
                        // From the cell, as the area has no size yet when this starts loading.
                        sourceSize: Qt.size(grid.cellWidth - 16, grid.cellWidth - 16)
                        source: cell.thumbnail
                    }
                }

                Label {
                    anchors.centerIn: thumbnailArea
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
                        grid.forceActiveFocus()
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

                // Dragged out as files, of the whole selection if it is chosen: to a file manager,
                // a mail, an editor, or a folder of the tree. The thumbnail goes with it, small.
                DragHandler {
                    id: fileDrag
                    target: null
                    acceptedButtons: Qt.LeftButton
                    // The grid would scroll instead, as it does for a touch.
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    grabPermissions: PointerHandler.TakeOverForbidden
                    // From a plain press, as with Ctrl it would start at once and take the click
                    // that adds to the selection. Ctrl and Shift still choose when dropping.
                    acceptedModifiers: Qt.NoModifier
                    onActiveChanged: {
                        if (!active)
                            return
                        grid.forceActiveFocus()
                        if (!cell.selected)
                            page.context.clickFile(cell.index, false, false)
                        const paths = page.context.selectedPaths
                        const start = (image) => {
                            // Only while the button is still down: a drag started after the
                            // release would wait for another click.
                            if (!fileDrag.active)
                                return
                            page.draggingFiles = true
                            FileClipboard.dragFiles(paths, image)
                            page.draggingFiles = false
                        }
                        if (!thumbnailImage.grabToImage((result) => start(result.image), Qt.size(64, 64)))
                            start(null)
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        grid.forceActiveFocus()
                        // On a selected file the menu is for the whole selection.
                        if (!cell.selected)
                            page.context.clickFile(cell.index, false, false)
                        page.fileMenuRequested()
                    }
                }
            }
        }

        Label {
            objectName: "browserStatus"
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 3
            Layout.bottomMargin: 3
            text: page.context.summary
            elide: Text.ElideRight
        }
    }
}

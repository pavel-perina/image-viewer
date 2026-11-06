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

    // Emitted when the user wants the current image in the large view.
    signal viewRequested()

    function moveCurrent(rows) {
        page.context.setCurrentIndex(page.context.currentIndex + rows * grid.columns)
    }

    orientation: Qt.Horizontal

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

            function reveal(path) {
                const index = treeModel.indexForPath(path)
                expandToIndex(index)
                forceLayout()
                positionViewAtRow(rowAtIndex(index), TableView.Contain)
            }

            delegate: TreeViewDelegate {
                id: treeRow
                implicitWidth: tree.width
                highlighted: treeModel.pathForIndex(tree.index(row, column)) === page.context.directory
                TapHandler {
                    onTapped: page.context.openPath(treeModel.pathForIndex(tree.index(treeRow.row, treeRow.column)))
                }
            }

            ScrollBar.vertical: ScrollBar {}

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

        // Breadcrumb
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            ScrollBar.vertical.policy: ScrollBar.AlwaysOff

            Row {
                Repeater {
                    model: page.context.breadcrumbs
                    delegate: Row {
                        id: crumb
                        required property var modelData
                        required property int index
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: crumb.index > 1
                            text: "/"
                        }
                        ToolButton {
                            text: crumb.modelData.name
                            onClicked: page.context.openPath(crumb.modelData.path)
                        }
                    }
                }
            }
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

            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: cell

                required property int index
                required property string name
                required property string thumbnail

                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    color: cell.index === page.context.currentIndex ? palette.highlight : "transparent"
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
                    onTapped: page.context.setCurrentIndex(cell.index)
                    onDoubleTapped: {
                        page.context.setCurrentIndex(cell.index)
                        page.viewRequested()
                    }
                }
            }
        }
    }
}

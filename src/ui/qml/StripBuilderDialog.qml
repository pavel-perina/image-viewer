pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Pictures side by side in one, for a web page or a manual: the chosen pictures in a list to
// put in order, the options under it, and the size of the result. Save As makes the strip.
DialogWindow {
    id: dialog

    title: "Image Strip"
    sizeKey: "strip"
    initialWidth: 760
    initialHeight: 620

    // Save As was chosen, with the strip ready to be made.
    signal saveRequested(var strip, string name)

    function start(paths) {
        builder.paths = paths
        grid.currentIndex = 0
        open()
    }

    StripBuilder {
        id: builder
        down: kept.down
        extent: kept.extent
        resize: kept.resize
        spacing: kept.spacing
        margin: kept.margin
        shadow: kept.shadow
        transparent: kept.transparent
    }

    // The options last used, for next time.
    Settings {
        id: kept
        category: "Strip"
        property bool down: false
        property int extent: 200
        property int resize: 1  // only smaller
        property int spacing: 8
        property int margin: 0
        property int shadow: 0
        property bool transparent: false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.panelSpacing

        Label {
            text: "Drag the pictures into order. Delete takes the chosen one out."
            color: Theme.textMuted
        }

        ListFrame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            focused: grid.activeFocus

            GridView {
                id: grid
                objectName: "stripGrid"
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                focus: true
                model: builder
                cellWidth: 150
                cellHeight: 140
                // Where a dragged picture would go, or -1.
                property int dropIndex: -1
                keyNavigationWraps: false
                ScrollBar.vertical: AppScrollBar {}
                Keys.onDeletePressed: builder.remove(grid.currentIndex)

                delegate: ThumbnailCell {
                    id: cell

                    required property int index
                    required name
                    required thumbnail

                    width: grid.cellWidth
                    height: grid.cellHeight
                    selected: cell.index === grid.currentIndex
                    keysHere: grid.activeFocus
                    current: cell.index === grid.dropIndex && grid.dropIndex !== reorder.from
                    opacity: reorder.active ? 0.5 : 1

                    TapHandler {
                        onTapped: {
                            grid.forceActiveFocus()
                            grid.currentIndex = cell.index
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            grid.forceActiveFocus()
                            grid.currentIndex = cell.index
                            pictureMenu.popup()
                        }
                    }
                    DragHandler {
                        id: reorder
                        target: null
                        property int from: -1
                        onActiveChanged: {
                            if (active) {
                                from = cell.index
                                grid.currentIndex = cell.index
                                return
                            }
                            if (grid.dropIndex >= 0)
                                builder.move(from, grid.dropIndex)
                            grid.currentIndex = grid.dropIndex >= 0 ? grid.dropIndex : from
                            grid.dropIndex = -1
                            from = -1
                        }
                        onCentroidChanged: {
                            if (!active)
                                return
                            const at = cell.mapToItem(grid.contentItem, centroid.position.x, centroid.position.y)
                            grid.dropIndex = grid.indexAt(at.x, at.y)
                        }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 24
            // Equal columns, so that a label changing its text moves nothing.
            uniformCellWidths: true
            rowSpacing: Theme.panelSpacing

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Direction"; Layout.fillWidth: true }
                ComboBox {
                    objectName: "stripDirection"
                    model: ["Horizontal", "Vertical"]
                    currentIndex: kept.down ? 1 : 0
                    onActivated: (index) => kept.down = index === 1
                }
            }
            NumberRow {
                label: kept.down ? "Width of every picture" : "Height of every picture"
                boxName: "stripExtent"
                enabled: kept.resize !== 2
                Layout.fillWidth: true
                from: 1
                to: 20000
                value: kept.extent
                onModified: (value) => kept.extent = value
            }
            NumberRow {
                label: "Space between"
                boxName: "stripSpacing"
                Layout.fillWidth: true
                to: 500
                value: kept.spacing
                onModified: (value) => kept.spacing = value
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Resize"; Layout.fillWidth: true }
                ComboBox {
                    objectName: "stripResize"
                    model: ["To this size", "Only smaller", "Keep sizes"]
                    currentIndex: kept.resize
                    onActivated: (index) => kept.resize = index
                }
            }
            NumberRow {
                label: "Margin"
                boxName: "stripMargin"
                Layout.fillWidth: true
                to: 500
                value: kept.margin
                onModified: (value) => kept.margin = value
            }
            NumberRow {
                label: "Shadow"
                boxName: "stripShadow"
                Layout.fillWidth: true
                to: 50
                value: kept.shadow
                onModified: (value) => kept.shadow = value
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Background"; Layout.fillWidth: true }
                ComboBox {
                    objectName: "stripBackground"
                    model: ["White", "Transparent"]
                    currentIndex: kept.transparent ? 1 : 0
                    onActivated: (index) => kept.transparent = index === 1
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing

            Label {
                objectName: "stripResult"
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: builder.problem ? Theme.warning : Theme.text
                text: builder.reading ? "Reading the pictures…"
                      : builder.problem ? builder.problem
                      : builder.count + " pictures: " + builder.resultSize.width + " × " + builder.resultSize.height + " pixels"
            }
            Button {
                objectName: "stripCancel"
                text: "Cancel"
                onClicked: dialog.close()
            }
            Button {
                objectName: "stripSave"
                text: "Save As…"
                highlighted: true
                enabled: !builder.reading && !builder.problem
                onClicked: {
                    const strip = builder.strip()
                    if (!strip)
                        return
                    dialog.close()
                    dialog.saveRequested(strip, builder.suggestedName())
                }
            }
        }
    }

    AppMenu {
        id: pictureMenu
        AppMenuItem {
            text: "Take Out"
            enabled: builder.count > 0
            onTriggered: builder.remove(grid.currentIndex)
        }
    }
}

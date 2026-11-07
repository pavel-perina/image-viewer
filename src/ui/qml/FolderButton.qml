pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import ImageViewer

// One icon for the folders worth returning to: favourites and those visited lately.
BarButton {
    id: button

    required property BrowseContext context

    readonly property bool isFavorite: FolderLists.favorites.includes(context.directory)
    readonly property alias menuRows: menu.rows
    readonly property alias organizer: organizer

    function rows() {
        const list = [{ kind: "toggle", text: isFavorite ? "Remove This Folder from Favorites" : "Add This Folder to Favorites" }]
        if (FolderLists.favorites.length > 0) {
            list.push({ kind: "heading", text: "Favorites" })
            for (const path of FolderLists.favorites)
                list.push({ kind: "folder", text: context.nativePath(path), path: path })
            list.push({ kind: "organize", text: "Organize Favorites..." })
        }
        const recent = FolderLists.recent.filter(path => path !== context.directory)
        if (recent.length > 0) {
            list.push({ kind: "heading", text: "Recent" })
            for (const path of recent)
                list.push({ kind: "folder", text: context.nativePath(path), path: path })
        }
        return list
    }

    function choose(row) {
        menu.close()
        if (row.kind === "toggle") {
            if (isFavorite)
                FolderLists.removeFavorite(context.directory)
            else
                FolderLists.addFavorite(context.directory)
        } else if (row.kind === "organize") {
            organizer.open()
        } else if (row.kind === "folder") {
            context.openPath(row.path)
        }
    }

    icon: "star"
    menuOpen: menu.visible
    lit: isFavorite
    tip: "Favorite and recent folders"
    onActivated: {
        menu.rows = rows()
        const place = mapToItem(Overlay.overlay, 0, height)
        menu.x = place.x
        menu.y = place.y
        menu.open()
    }

    // Modal so that shortcuts for the grid do not take its keys, without dimming the window.
    Popup {
        id: menu
        property var rows: []
        parent: Overlay.overlay
        modal: true
        dim: false
        padding: 2
        width: 420
        height: Math.min(list.contentHeight + 2 * padding, Math.max(80, (parent ? parent.height : 400) - y - 8))

        background: Rectangle {
            color: palette.base
            border.width: 1
            border.color: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.12)
        }

        ListView {
            id: list
            anchors.fill: parent
            clip: true
            model: menu.rows
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool heading: modelData.kind === "heading"
                readonly property bool usable: !heading && (modelData.kind !== "folder" || FolderLists.exists(modelData.path))
                width: ListView.view.width
                height: 26
                radius: 2
                color: rowHover.hovered && usable ? Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.12) : "transparent"
                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    text: row.modelData.text
                    color: palette.text
                    opacity: row.heading ? 0.6 : row.usable ? 1 : 0.4
                    font.pointSize: row.heading ? Qt.application.font.pointSize - 1 : Qt.application.font.pointSize
                }
                HoverHandler { id: rowHover }
                // Not a TapHandler, which leaves the press to whatever is below the list too.
                MouseArea {
                    anchors.fill: parent
                    enabled: row.usable
                    onClicked: button.choose(row.modelData)
                }
            }
        }
    }

    AppDialog {
        id: organizer
        title: "Organize Favorites"
        standardButtons: Dialog.Close
        width: Math.min(560, parent ? parent.width - 40 : 560)
        height: Math.min(420, parent ? parent.height - 40 : 420)

        ListView {
            anchors.fill: parent
            clip: true
            model: FolderLists.favorites
            ScrollBar.vertical: AppScrollBar {}

            Label {
                anchors.centerIn: parent
                visible: FolderLists.favorites.length === 0
                text: "No favorites"
                opacity: 0.6
            }

            delegate: Item {
                id: favorite
                required property string modelData
                required property int index
                width: ListView.view.width
                height: 36
                Label {
                    anchors.left: parent.left
                    anchors.right: controls.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    text: button.context.nativePath(favorite.modelData)
                }
                Row {
                    id: controls
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    ToolButton {
                        text: "Up"
                        enabled: favorite.index > 0
                        onClicked: FolderLists.moveFavorite(favorite.index, favorite.index - 1)
                    }
                    ToolButton {
                        text: "Down"
                        enabled: favorite.index + 1 < FolderLists.favorites.length
                        onClicked: FolderLists.moveFavorite(favorite.index, favorite.index + 1)
                    }
                    ToolButton {
                        text: "Remove"
                        onClicked: FolderLists.removeFavorite(favorite.modelData)
                    }
                }
            }
        }
    }
}

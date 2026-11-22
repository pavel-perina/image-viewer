pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import ImageViewer

// What can be done to a folder of a folder tree, from its menu or its keys: the same in the
// browser and in the file chooser. Acts on a BrowseContext, and asks names through a NameDialog
// of its user, who also has that dialog for files.
Item {
    id: root

    required property BrowseContext context
    required property NameDialog names
    // Whether folders may be changed now: not while files are being moved, nor during a tool.
    property bool idle: true

    function popup(path) {
        menu.path = path
        menu.popup()
    }

    // The context's folderCreated says when the folder exists.
    function createIn(parentFolder) {
        if (root.idle)
            root.names.ask("New Folder", "New Folder", (name) => root.context.createFolder(parentFolder, name))
    }

    function rename(path) {
        if (root.idle)
            root.names.ask("Rename Folder", path.substring(path.lastIndexOf("/") + 1), (name) => root.context.rename(path, name))
    }

    function trash(path) {
        if (!root.idle)
            return
        trashDialog.path = path
        trashDialog.open()
    }

    AppMenu {
        id: menu
        objectName: "folderMenu"
        property string path
        parent: Overlay.overlay
        AppMenuItem {
            text: "&New Folder..."
            enabled: root.idle
            onTriggered: root.createIn(menu.path)
        }
        AppMenuItem {
            text: "&Rename..."
            enabled: root.idle
            onTriggered: root.rename(menu.path)
        }
        AppMenuItem {
            text: "Move to &Trash..."
            enabled: root.idle
            onTriggered: root.trash(menu.path)
        }
        MenuSeparator {}
        // Explorer on Windows, Dolphin, Files or whatever the desktop opens folders with.
        AppMenuItem {
            objectName: "openFolderInFileManagerItem"
            text: "&Open in File Manager"
            onTriggered: Qt.openUrlExternally(root.context.urlFromPath(menu.path))
        }
    }

    MessageBox {
        id: trashDialog
        objectName: "trashFolderDialog"
        property string path
        kind: "question"
        title: "Move Folder to Trash"
        text: "Move this folder and everything in it to the trash?\n\n" + path
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: root.context.trashFolder(path)
    }
}

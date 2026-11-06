pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ImageViewer

// What can be done to files and folders: the actions for menus and shortcuts, and their dialogs.
Item {
    id: root

    required property BrowseContext context
    required property bool viewing
    property bool showProgress: false

    // The large view shows one image, whatever was selected before entering it.
    readonly property var targetPaths: viewing ? (context.currentPath ? [context.currentPath] : []) : context.selectedPaths
    readonly property bool hasTargets: targetPaths.length > 0
    readonly property bool idle: !context.busy

    readonly property alias renameAction: renameAction
    readonly property alias trashAction: trashAction
    readonly property alias deleteAction: deleteAction
    readonly property alias copyToAction: copyToAction
    readonly property alias moveToAction: moveToAction
    readonly property alias newFolderAction: newFolderAction
    readonly property alias openFolderAction: openFolderAction
    readonly property alias copyAction: copyAction
    readonly property alias pasteAction: pasteAction
    readonly property alias copyPathAction: copyPathAction
    readonly property alias refreshAction: refreshAction
    readonly property alias fileMenu: fileMenu

    function showFolderMenu(path) {
        folderMenu.path = path
        folderMenu.popup()
    }

    function folderName(path) {
        return path.substring(path.lastIndexOf("/") + 1)
    }

    function rememberFolder(folder) {
        const others = settings.recentFolders.filter(item => item !== folder)
        settings.recentFolders = [folder].concat(others).slice(0, 8)
    }

    // Asks first if files of the same names are there.
    function transfer(paths, folder, moving) {
        const conflicts = context.conflictCount(paths, folder)
        if (conflicts > 0) {
            conflictDialog.paths = paths
            conflictDialog.folder = folder
            conflictDialog.moving = moving
            conflictDialog.conflicts = conflicts
            conflictDialog.open()
        } else if (moving) {
            context.moveTo(paths, folder, BrowseContext.SkipExisting)
        } else {
            context.copyTo(paths, folder, BrowseContext.SkipExisting)
        }
    }

    function transferToChosen(folder) {
        rememberFolder(folder)
        transfer(targetPaths, folder, folderDialog.moving)
    }

    function chooseFolder(moving) {
        folderDialog.moving = moving
        const recent = settings.recentFolders
        folderDialog.currentFolder = context.urlFromPath(recent.length > 0 ? recent[0] : context.directory)
        folderDialog.open()
    }

    function paste() {
        const files = context.clipboardFiles()
        if (files.length > 0)
            transfer(files, context.directory, false)
        else
            context.pasteImage()
    }

    function askName(title, text, accept) {
        nameDialog.title = title
        nameDialog.initialText = text
        nameDialog.accept = accept
        nameDialog.open()
    }

    Settings {
        id: settings
        category: "FileOperations"
        property var recentFolders: []
    }

    Action {
        id: renameAction
        text: "&Rename..."
        shortcut: "F2"
        enabled: root.idle && root.targetPaths.length === 1
        onTriggered: {
            const path = root.targetPaths[0]
            root.askName("Rename", root.folderName(path), (name) => root.context.rename(path, name))
        }
    }
    Action {
        id: trashAction
        text: "Move to &Trash"
        shortcut: "Del"
        enabled: root.idle && root.hasTargets
        onTriggered: root.context.trash(root.targetPaths)
    }
    Action {
        id: deleteAction
        text: "&Delete Permanently..."
        shortcut: "Shift+Del"
        enabled: root.idle && root.hasTargets
        onTriggered: {
            // Kept from now on, so the selection cannot change what the answer applies to.
            deleteDialog.paths = root.targetPaths
            deleteDialog.open()
        }
    }
    Action {
        id: copyToAction
        text: "&Copy To..."
        enabled: root.idle && root.hasTargets
        onTriggered: root.chooseFolder(false)
    }
    Action {
        id: moveToAction
        text: "&Move To..."
        enabled: root.idle && root.hasTargets
        onTriggered: root.chooseFolder(true)
    }
    Action {
        id: newFolderAction
        text: "&New Folder..."
        shortcut: "Ctrl+Shift+N"
        enabled: root.idle && root.context.directory !== ""
        onTriggered: {
            const parent = root.context.directory
            root.askName("New Folder", "New Folder", (name) => root.context.createFolder(parent, name))
        }
    }
    Action {
        id: openFolderAction
        text: "Open Containing &Folder"
        enabled: root.context.directory !== ""
        onTriggered: Qt.openUrlExternally(root.context.urlFromPath(root.context.directory))
    }
    Action {
        id: copyAction
        text: "&Copy"
        shortcut: StandardKey.Copy
        enabled: root.hasTargets
        onTriggered: root.context.copyFilesToClipboard(root.targetPaths)
    }
    Action {
        id: pasteAction
        text: "&Paste"
        shortcut: StandardKey.Paste
        enabled: root.idle && root.context.canPaste && root.context.directory !== ""
        onTriggered: root.paste()
    }
    Action {
        id: copyPathAction
        text: "Copy Full Pat&h"
        shortcut: "Ctrl+Shift+C"
        enabled: root.hasTargets
        onTriggered: root.context.copyPathsToClipboard(root.targetPaths)
    }
    Action {
        id: refreshAction
        text: "&Refresh"
        shortcut: "F5"
        onTriggered: root.context.refresh()
    }

    Menu {
        id: fileMenu
        parent: Overlay.overlay
        MenuItem { action: copyAction }
        MenuItem { action: pasteAction }
        MenuItem { action: copyPathAction }
        MenuSeparator {}
        MenuItem { action: renameAction }
        MenuItem { action: trashAction }
        MenuItem { action: deleteAction }
        MenuSeparator {}
        Menu {
            id: copyToMenu
            title: "Copy To"
            enabled: root.idle && root.hasTargets
            MenuItem { action: copyToAction; text: "Choose Folder..." }
            MenuSeparator { visible: settings.recentFolders.length > 0 }
            Repeater {
                model: settings.recentFolders
                MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        root.rememberFolder(modelData)
                        root.transfer(root.targetPaths, modelData, false)
                    }
                }
            }
        }
        Menu {
            id: moveToMenu
            title: "Move To"
            enabled: root.idle && root.hasTargets
            MenuItem { action: moveToAction; text: "Choose Folder..." }
            MenuSeparator { visible: settings.recentFolders.length > 0 }
            Repeater {
                model: settings.recentFolders
                MenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        root.rememberFolder(modelData)
                        root.transfer(root.targetPaths, modelData, true)
                    }
                }
            }
        }
        MenuSeparator {}
        MenuItem { action: newFolderAction }
        MenuItem { action: openFolderAction }
    }

    Menu {
        id: folderMenu
        property string path
        parent: Overlay.overlay
        MenuItem {
            text: "&New Folder..."
            enabled: root.idle
            onTriggered: {
                const parent = folderMenu.path
                root.askName("New Folder", "New Folder", (name) => root.context.createFolder(parent, name))
            }
        }
        MenuItem {
            text: "&Rename..."
            enabled: root.idle
            onTriggered: {
                const path = folderMenu.path
                root.askName("Rename Folder", root.folderName(path), (name) => root.context.rename(path, name))
            }
        }
        MenuItem {
            text: "Move to &Trash..."
            enabled: root.idle
            onTriggered: {
                trashFolderDialog.path = folderMenu.path
                trashFolderDialog.open()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        property bool moving: false
        title: moving ? "Move To" : "Copy To"
        onAccepted: root.transferToChosen(root.context.pathFromUrl(selectedFolder))
    }

    Dialog {
        id: nameDialog
        property string initialText
        property var accept: null
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, parent.width - 32)

        function submit() {
            const problem = nameDialog.accept(nameField.text)
            nameProblem.text = problem
            if (!problem)
                nameDialog.close()
        }

        // Closed by submit, because a name that cannot be used keeps the dialog open.
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            nameProblem.text = ""
            nameField.text = initialText
            nameField.forceActiveFocus()
            // Selects the name without its extension, as that is what usually changes.
            const dot = initialText.lastIndexOf(".")
            nameField.select(0, dot > 0 ? dot : initialText.length)
        }

        footer: DialogButtonBox {
            Button {
                text: "OK"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: nameDialog.submit()
            }
            Button {
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: nameDialog.close()
            }
        }

        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            TextField {
                id: nameField
                Layout.fillWidth: true
                onAccepted: nameDialog.submit()
            }
            Label {
                id: nameProblem
                Layout.fillWidth: true
                visible: text !== ""
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: conflictDialog
        property var paths: []
        property string folder
        property bool moving: false
        property int conflicts: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Files Already Exist"
        width: Math.min(640, parent.width - 32)

        function choose(conflict) {
            close()
            if (moving)
                root.context.moveTo(paths, folder, conflict)
            else
                root.context.copyTo(paths, folder, conflict)
        }

        footer: DialogButtonBox {
            Button {
                text: "Skip"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: conflictDialog.choose(BrowseContext.SkipExisting)
            }
            Button {
                text: "Keep Both"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: conflictDialog.choose(BrowseContext.KeepBoth)
            }
            Button {
                text: "Replace"
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: conflictDialog.choose(BrowseContext.ReplaceExisting)
            }
            Button {
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: conflictDialog.close()
            }
        }

        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            wrapMode: Text.WrapAnywhere
            text: (conflictDialog.paths.length === 1 ? "This file already exists in\n"
                   : conflictDialog.conflicts + " of " + conflictDialog.paths.length + " files already exist in\n")
                  + conflictDialog.folder + "\n\nReplacing sends the existing files to the trash."
        }
    }

    Dialog {
        id: trashFolderDialog
        property string path
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Move Folder to Trash"
        width: Math.min(640, parent.width - 32)
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: root.context.trashFolder(path)
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            wrapMode: Text.WrapAnywhere
            text: "Move this folder and everything in it to the trash?\n\n" + trashFolderDialog.path
        }
    }

    Dialog {
        id: deleteDialog
        property var paths: []
        readonly property int shown: 5
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Delete Permanently"
        width: Math.min(640, parent.width - 32)
        standardButtons: Dialog.Yes | Dialog.No
        onAboutToShow: standardButton(Dialog.No).forceActiveFocus()
        onAccepted: root.context.deletePermanently(paths)
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            wrapMode: Text.WrapAnywhere
            text: {
                const names = deleteDialog.paths.slice(0, deleteDialog.shown).map(path => root.folderName(path)).join("\n")
                const more = deleteDialog.paths.length - deleteDialog.shown
                return (deleteDialog.paths.length === 1 ? "Delete this file?" : "Delete these " + deleteDialog.paths.length + " files?")
                       + " This cannot be undone, as the trash is not used.\n\n"
                       + names + (more > 0 ? "\nand " + more + " more" : "")
            }
        }
    }

    Dialog {
        id: problemDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Some Operations Failed"
        standardButtons: Dialog.Ok
        width: Math.min(640, parent.width - 32)
        Label {
            id: problemText
            anchors.left: parent.left
            anchors.right: parent.right
            wrapMode: Text.Wrap
        }
    }

    Connections {
        target: root.context
        function onOperationFinished(problems) {
            if (problems.length === 0)
                return
            const limit = 10
            let text = problems.slice(0, limit).join("\n")
            if (problems.length > limit)
                text += "\n\nand " + (problems.length - limit) + " more"
            problemText.text = text
            problemDialog.open()
        }
    }
}

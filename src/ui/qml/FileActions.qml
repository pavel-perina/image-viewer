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
    // The page shown of the target, for Save As.
    property int page: 0
    // The view whose edits Save As saves, if any.
    property ImageView editView: null
    // Runs what would change or remove the file on show, once its edits are saved or given up.
    property var guard: (proceed) => proceed()

    signal saveCancelled()

    // The large view shows one image, whatever was selected before entering it.
    readonly property var targetPaths: viewing ? (context.currentPath ? [context.currentPath] : []) : context.selectedPaths
    readonly property bool hasTargets: targetPaths.length > 0
    readonly property bool idle: !context.busy
    // The lossless JPEG tools work only when every target is a JPEG.
    readonly property bool jpegTargets: context.allJpeg(targetPaths)

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
    readonly property alias rotateClockwiseAction: rotateClockwiseAction
    readonly property alias rotateCounterclockwiseAction: rotateCounterclockwiseAction
    readonly property alias rotate180Action: rotate180Action
    readonly property alias flipHorizontalAction: flipHorizontalAction
    readonly property alias flipVerticalAction: flipVerticalAction
    readonly property alias applyOrientationAction: applyOrientationAction
    readonly property alias convertToJxlAction: convertToJxlAction
    readonly property alias askBeforeEditingAction: askBeforeEditingAction
    readonly property alias trimEdgesAction: trimEdgesAction
    readonly property alias saveAsAction: saveAsAction

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

    // Asks first unless told not to, as the files change in place.
    function editJpegs(action, operation) {
        guard(() => editJpegsNow(action, operation))
    }

    function editJpegsNow(action, operation) {
        const paths = targetPaths
        if (!settings.askBeforeEditing) {
            context.editJpegs(paths, operation, settings.trimEdges)
            return
        }
        editDialog.paths = paths
        editDialog.operation = operation
        editDialog.title = action.text.replace("&", "")
        editDialog.open()
    }

    // Asks for the new file in the native dialog, then for the format's options.
    function saveAs() {
        const path = targetPaths[0]
        const slash = path.lastIndexOf("/")
        const name = path.substring(slash + 1)
        const dot = name.lastIndexOf(".")
        const base = dot > 0 ? name.substring(0, dot) : name
        saveDialog.source = path
        saveDialog.page = page
        saveDialog.edited = editView ? editView.editedImage() : undefined
        saveDialog.currentFolder = context.urlFromPath(path.substring(0, slash))
        saveDialog.selectedFile = context.urlFromPath(path.substring(0, slash + 1)
                                                      + (context.saveFormat(path) ? name : base + ".png"))
        saveDialog.open()
    }

    function chooseSaveOptions(source, page, target, edited) {
        const format = context.saveFormat(target)
        if (!format) {
            problemDialog.text = root.folderName(target) + ": choose a name ending in .jpg, .png, .tif, .webp, .avif or .jxl"
            problemDialog.open()
            saveCancelled()
            return
        }
        saveOptionsDialog.source = source
        saveOptionsDialog.page = page
        saveOptionsDialog.edited = edited
        saveOptionsDialog.target = target
        saveOptionsDialog.format = format
        saveOptionsDialog.open()
    }

    function fileCountText(paths, what) {
        return paths.length === 1 ? root.folderName(paths[0]) : "these " + paths.length + " " + what
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
        property bool askBeforeEditing: true
        property bool trimEdges: false
        property bool trashAfterJxl: false
    }

    Action {
        id: renameAction
        text: "&Rename..."
        shortcut: "F2"
        enabled: root.idle && root.targetPaths.length === 1
        onTriggered: {
            const path = root.targetPaths[0]
            root.guard(() => root.askName("Rename", root.folderName(path), (name) => root.context.rename(path, name)))
        }
    }
    Action {
        id: trashAction
        text: "Move to &Trash"
        shortcut: "Del"
        enabled: root.idle && root.hasTargets
        onTriggered: {
            const paths = root.targetPaths
            root.guard(() => root.context.trash(paths))
        }
    }
    Action {
        id: deleteAction
        text: "&Delete Permanently..."
        shortcut: "Shift+Del"
        enabled: root.idle && root.hasTargets
        onTriggered: {
            // Kept from now on, so the selection cannot change what the answer applies to.
            const paths = root.targetPaths
            root.guard(() => {
                deleteDialog.paths = paths
                deleteDialog.open()
            })
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
        onTriggered: root.guard(() => root.chooseFolder(true))
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

    Action {
        id: rotateClockwiseAction
        text: "Rotate 90° &Clockwise"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.RotateClockwise)
    }
    Action {
        id: rotateCounterclockwiseAction
        text: "Rotate 90° Counterclock&wise"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.RotateCounterclockwise)
    }
    Action {
        id: rotate180Action
        text: "Rotate &180°"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.Rotate180)
    }
    Action {
        id: flipHorizontalAction
        text: "Flip &Horizontally"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.FlipHorizontal)
    }
    Action {
        id: flipVerticalAction
        text: "Flip &Vertically"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.FlipVertical)
    }
    Action {
        id: applyOrientationAction
        text: "Apply EXIF &Orientation"
        enabled: root.idle && root.jpegTargets
        onTriggered: root.editJpegs(this, BrowseContext.ApplyOrientation)
    }
    Action {
        id: trimEdgesAction
        text: "&Trim Edges When Needed"
        checkable: true
        checked: settings.trimEdges
        onToggled: settings.trimEdges = checked
    }
    Action {
        id: askBeforeEditingAction
        text: "&Ask Before Changing Files"
        checkable: true
        checked: settings.askBeforeEditing
        onToggled: settings.askBeforeEditing = checked
    }
    Action {
        id: convertToJxlAction
        text: "Convert to JPEG &XL..."
        enabled: root.idle && root.jpegTargets && root.context.canConvertToJxl
        onTriggered: {
            const paths = root.targetPaths
            root.guard(() => {
                jxlDialog.paths = paths
                jxlDialog.open()
            })
        }
    }

    Action {
        id: saveAsAction
        text: "Save &As..."
        shortcut: "Ctrl+Shift+S"
        enabled: root.idle && root.targetPaths.length === 1
        onTriggered: root.saveAs()
    }

    AppMenu {
        id: fileMenu
        parent: Overlay.overlay
        AppMenuItem { action: copyAction }
        AppMenuItem { action: pasteAction }
        AppMenuItem { action: copyPathAction }
        MenuSeparator {}
        AppMenuItem { action: renameAction }
        AppMenuItem { action: trashAction }
        AppMenuItem { action: deleteAction }
        MenuSeparator {}
        AppMenuItem { action: saveAsAction }
        JpegMenu { actions: root; enabled: root.idle && root.jpegTargets }
        MenuSeparator {}
        AppMenu {
            id: copyToMenu
            title: "Copy To"
            enabled: root.idle && root.hasTargets
            AppMenuItem { action: copyToAction; text: "Choose Folder..." }
            MenuSeparator { visible: settings.recentFolders.length > 0 }
            Repeater {
                model: settings.recentFolders
                AppMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        root.rememberFolder(modelData)
                        root.transfer(root.targetPaths, modelData, false)
                    }
                }
            }
        }
        AppMenu {
            id: moveToMenu
            title: "Move To"
            enabled: root.idle && root.hasTargets
            AppMenuItem { action: moveToAction; text: "Choose Folder..." }
            MenuSeparator { visible: settings.recentFolders.length > 0 }
            Repeater {
                model: settings.recentFolders
                AppMenuItem {
                    required property string modelData
                    text: modelData
                    onTriggered: {
                        const folder = modelData
                        root.guard(() => {
                            root.rememberFolder(folder)
                            root.transfer(root.targetPaths, folder, true)
                        })
                    }
                }
            }
        }
        MenuSeparator {}
        AppMenuItem { action: newFolderAction }
        AppMenuItem { action: openFolderAction }
    }

    AppMenu {
        id: folderMenu
        property string path
        parent: Overlay.overlay
        AppMenuItem {
            text: "&New Folder..."
            enabled: root.idle
            onTriggered: {
                const parent = folderMenu.path
                root.askName("New Folder", "New Folder", (name) => root.context.createFolder(parent, name))
            }
        }
        AppMenuItem {
            text: "&Rename..."
            enabled: root.idle
            onTriggered: {
                const path = folderMenu.path
                root.askName("Rename Folder", root.folderName(path), (name) => root.context.rename(path, name))
            }
        }
        AppMenuItem {
            text: "Move to &Trash..."
            enabled: root.idle
            onTriggered: {
                trashFolderDialog.path = folderMenu.path
                trashFolderDialog.open()
            }
        }
    }

    FileDialog {
        id: saveDialog
        objectName: "saveDialog"
        property string source
        property int page: 0
        property var edited
        title: "Save As"
        fileMode: FileDialog.SaveFile
        nameFilters: root.context.saveFilters()
        // A name typed without an extension gets the chosen format's.
        defaultSuffix: selectedNameFilter.extensions.length > 0 ? selectedNameFilter.extensions[0] : ""
        onAccepted: {
            root.chooseSaveOptions(source, page, root.context.pathFromUrl(selectedFile), edited)
            edited = undefined
        }
        onRejected: {
            edited = undefined
            root.saveCancelled()
        }
    }

    SaveOptionsDialog {
        id: saveOptionsDialog
        context: root.context
        onRejected: {
            edited = undefined
            root.saveCancelled()
        }
    }

    FolderDialog {
        id: folderDialog
        property bool moving: false
        title: moving ? "Move To" : "Copy To"
        onAccepted: root.transferToChosen(root.context.pathFromUrl(selectedFolder))
    }

    AppDialog {
        id: nameDialog
        property string initialText
        property var accept: null
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

    MessageBox {
        id: conflictDialog
        kind: "question"
        property var paths: []
        property string folder
        property bool moving: false
        property int conflicts: 0
        title: "Files Already Exist"
        text: (paths.length === 1 ? "This file already exists in\n" : conflicts + " of " + paths.length + " files already exist in\n")
              + folder + "\n\nReplacing sends the existing files to the trash."
        onOpened: conflictCancel.forceActiveFocus()

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
                id: conflictCancel
                text: "Cancel"
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: conflictDialog.close()
            }
        }
    }

    MessageBox {
        id: trashFolderDialog
        property string path
        kind: "question"
        title: "Move Folder to Trash"
        text: "Move this folder and everything in it to the trash?\n\n" + path
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: root.context.trashFolder(path)
    }

    MessageBox {
        id: deleteDialog
        kind: "warning"
        property var paths: []
        readonly property int shown: 5
        title: "Delete Permanently"
        standardButtons: Dialog.Yes | Dialog.No
        focusedButton: Dialog.No
        onAccepted: root.context.deletePermanently(paths)
        text: {
            const names = paths.slice(0, shown).map(path => root.folderName(path)).join("\n")
            const more = paths.length - shown
            return (paths.length === 1 ? "Delete this file?" : "Delete these " + paths.length + " files?")
                   + " This cannot be undone, as the trash is not used.\n\n"
                   + names + (more > 0 ? "\nand " + more + " more" : "")
        }
    }

    MessageBox {
        id: editDialog
        objectName: "jpegEditDialog"
        kind: "question"
        property var paths: []
        property int operation: BrowseContext.RotateClockwise
        text: root.fileCountText(paths, "files") + " will change in place. Nothing is recompressed, so no "
              + "quality is lost, and the EXIF orientation is reset to match."
        standardButtons: Dialog.Ok | Dialog.Cancel
        extras: [
            CheckBox {
                objectName: "trimEdgesCheck"
                text: "Trim edges that cannot be turned, up to 15 pixels"
                checked: settings.trimEdges
                onToggled: settings.trimEdges = checked
            },
            CheckBox {
                id: dontAskCheck
                objectName: "dontAskCheck"
                text: "Do not ask again"
            }
        ]
        onOpened: dontAskCheck.checked = false
        onAccepted: {
            if (dontAskCheck.checked)
                settings.askBeforeEditing = false
            root.context.editJpegs(paths, operation, settings.trimEdges)
        }
    }

    MessageBox {
        id: jxlDialog
        objectName: "jxlDialog"
        property var paths: []
        kind: settings.trashAfterJxl ? "warning" : "question"
        title: "Convert to JPEG XL"
        readonly property bool one: paths.length === 1
        text: "Convert " + root.fileCountText(paths, "JPEG files") + " to JPEG XL? "
              + (one ? "The picture is repacked without loss, and the new file is checked to give back the original exactly."
                     : "The pictures are repacked without loss, and each new file is checked to give back its original exactly.")
              + "\n\n"
              + (settings.trashAfterJxl ? (one ? "The original then goes to the trash." : "Each original then goes to the trash.")
                                        : (one ? "The original is kept." : "The originals are kept."))
        standardButtons: Dialog.Ok | Dialog.Cancel
        extras: [
            CheckBox {
                objectName: "trashOriginalsCheck"
                text: "Move the originals to the trash"
                checked: settings.trashAfterJxl
                onToggled: settings.trashAfterJxl = checked
            }
        ]
        onAccepted: root.context.convertToJxl(paths, settings.trashAfterJxl)
    }

    MessageBox {
        id: problemDialog
        kind: "error"
        title: "Some Operations Failed"
        standardButtons: Dialog.Ok
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
            problemDialog.text = text
            problemDialog.open()
        }
    }
}

pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Chooses a file to open, a name to save as, or a folder, in a window of its own. The places and the
// folder tree are on the left, the path bar above and the files of the folder in the middle, as a
// brief or a detailed list.
//
// Saving takes the name typed, in the folder shown, and nothing else: a file selected in the list
// only fills in the name. Choosing another type changes the extension of the name, typing a known
// extension chooses its type, and a name without one gets the type's. A name of a folder opens
// that folder instead, and a file that exists is replaced only when the user says so.
DialogWindow {
    id: chooser
    objectName: "fileChooser"

    property string mode: "open"  // "open", "save" or "folder"
    // One {label, extensions} per type to offer, extensions in lower case without the dot; empty
    // extensions offer every file.
    property var types: []
    property int typeIndex: 0
    property string acceptText: mode === "save" ? "Save" : "Open"

    signal chosen(string path)
    signal cancelled()

    readonly property alias directory: browse.directory
    readonly property alias nameField: nameField
    readonly property alias fileList: fileList
    readonly property alias acceptButton: acceptButton
    readonly property alias typeBox: typeBox
    readonly property alias replaceBox: replaceBox
    property string problem
    // The file name to come back to after a folder name typed in its place opens that folder.
    property string lastFileName
    property bool finished: false
    // The two buttons are as wide as each other, and a little wider than either needs.
    readonly property real buttonWidth: Math.max(acceptButton.implicitWidth, cancelButton.implicitWidth, 120)

    // One size for every chooser, kept from one time to the next.
    sizeKey: "fileChooser"
    initialWidth: 980
    initialHeight: 680
    padding: 10

    // Opens the chooser in a folder, else where it was last, else in the home folder, with a name
    // filled in for saving.
    function start(folder, name) {
        finished = false
        problem = ""
        if (folder && listing.kindOf(folder) === "folder")
            browse.openPath(folder)
        else if (!browse.directory || listing.kindOf(browse.directory) !== "folder")
            browse.openPath(FolderLists.places()[0].path)
        nameField.text = name ?? ""
        lastFileName = nameField.text
        const type = typeOf(nameField.text)
        typeIndex = Math.max(0, Math.min(types.length - 1, mode === "save" && type >= 0 ? type : typeIndex))
        placesView.model = placeRows()
        placesView.currentIndex = Math.max(0, placesView.model.findIndex(row => row.path === browse.directory))
        open()
    }

    // For what takes a URL rather than a path.
    function urlOf(path) {
        return browse.urlFromPath(path)
    }

    function folderName(path) {
        const trimmed = path.length > 1 && path.endsWith("/") ? path.slice(0, -1) : path
        return trimmed.substring(trimmed.lastIndexOf("/") + 1) || trimmed
    }

    function suffixOf(name) {
        const dot = name.lastIndexOf(".")
        return dot > 0 && dot > name.lastIndexOf("/") ? name.substring(dot + 1).toLowerCase() : ""
    }

    // The type whose extensions include that of the name, or -1.
    function typeOf(name) {
        const suffix = suffixOf(name)
        return suffix ? types.findIndex(type => type.extensions.indexOf(suffix) >= 0) : -1
    }

    // The name with the extension of a type, in place of that of another type or added.
    function withType(name, index) {
        if (!name || index < 0 || index >= types.length || types[index].extensions.length === 0)
            return name
        const base = typeOf(name) >= 0 ? name.substring(0, name.lastIndexOf(".")) : name
        return base + "." + types[index].extensions[0]
    }

    function resolve(name) {
        if (name.startsWith("/") || /^[A-Za-z]:[\\/]/.test(name))
            return name
        const folder = browse.directory
        return folder.endsWith("/") ? folder + name : folder + "/" + name
    }

    function placeRows() {
        const rows = FolderLists.places().map(place => ({ name: place.name, path: place.path, section: "Places" }))
        for (const path of FolderLists.favorites) {
            if (FolderLists.exists(path))
                rows.push({ name: folderName(path), path: path, section: "Favorites" })
        }
        for (const path of FolderLists.recent.slice(0, 8)) {
            if (FolderLists.exists(path))
                rows.push({ name: folderName(path), path: path, section: "Recent" })
        }
        return rows
    }

    function openFolder(path) {
        problem = ""
        browse.openPath(path)
        nameField.text = mode === "save" ? lastFileName : ""
    }

    function finish(path) {
        finished = true
        close()
        chosen(path)
    }

    // The button at the bottom, or Enter in the name.
    function submit() {
        problem = ""
        if (mode === "folder") {
            const entry = fileList.currentEntry()
            finish(entry.isFolder ? entry.path : browse.directory)
            return
        }
        const name = nameField.text.trim()
        if (!name) {
            const entry = fileList.currentEntry()
            if (mode === "open" && entry.path)
                activate(entry)
            return
        }
        let path = resolve(name)
        if (listing.kindOf(path) === "folder") {
            openFolder(path)
            return
        }
        if (mode === "open") {
            if (listing.kindOf(path) === "file")
                finish(path)
            else
                problem = "Nothing is called " + name + " here."
            return
        }
        if (typeOf(name) < 0) {
            const named = withType(name, typeIndex)
            path += named.substring(name.length)
            nameField.text = named
        }
        const parentFolder = path.substring(0, path.lastIndexOf("/")) || "/"
        const kind = listing.kindOf(path)
        if (listing.kindOf(parentFolder) !== "folder") {
            problem = "There is no folder " + parentFolder + "."
        } else if (kind === "folder") {
            openFolder(path)
        } else if (kind === "file") {
            replaceBox.path = path
            replaceBox.open()
        } else {
            finish(path)
        }
    }

    // A double click or Enter in the list.
    function activate(entry) {
        if (entry.isFolder) {
            openFolder(entry.path)
        } else if (mode === "save") {
            nameField.text = entry.name
            lastFileName = entry.name
            submit()
        } else if (mode === "open") {
            finish(entry.path)
        }
    }

    onTypeIndexChanged: typeBox.currentIndex = typeIndex
    onOpenedChanged: {
        if (!opened) {
            if (!finished)
                cancelled()
        } else if (mode === "save") {
            nameField.forceActiveFocus()
            const dot = nameField.text.lastIndexOf(".")
            nameField.select(0, dot > 0 ? dot : nameField.text.length)
        } else {
            fileList.forceActiveFocus()
        }
    }

    Settings {
        category: "FileChooser"
        property alias details: fileList.details
    }

    BrowseContext {
        id: browse
        showHiddenFolders: Preferences.showHiddenFolders
        // A folder made here is where the user means to save or look next.
        onFolderCreated: (path) => chooser.openFolder(path)
        onOperationFinished: (problems) => {
            if (problems.length > 0)
                chooser.problem = problems.join("\n")
        }
    }

    // The same folder actions as the browser's, for this chooser's folders.
    NameDialog {
        id: chooserNameDialog
        objectName: "chooserNameDialog"
    }

    FolderMenu {
        id: chooserFolderMenu
        objectName: "chooserFolderMenu"
        context: browse
        names: chooserNameDialog
        idle: !browse.busy
    }

    Shortcut {
        sequence: "Ctrl+Shift+N"
        enabled: browse.directory !== ""
        onActivated: chooserFolderMenu.createIn(browse.directory)
    }

    FolderListing {
        id: listing
        path: browse.directory
        showFiles: chooser.mode !== "folder"
        showHiddenFolders: Preferences.showHiddenFolders
        extensions: chooser.mode === "folder" || chooser.typeIndex >= chooser.types.length ? []
                                                                                          : chooser.types[chooser.typeIndex].extensions
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            PathBar {
                objectName: "chooserPathBar"
                Layout.fillWidth: true
                context: browse
                recordVisits: false
            }
            IconButton {
                objectName: "newFolderButton"
                text: "New Folder"
                tip: "New Folder (" + KeyNames.text("Ctrl+Shift+N") + ")"
                iconName: "folder-new"
                enabled: browse.directory !== "" && !browse.busy
                onClicked: chooserFolderMenu.createIn(browse.directory)
            }
            IconButton {
                objectName: "briefButton"
                text: "Brief"
                iconName: "view-list-brief"
                checkable: true
                checked: !fileList.details
                onClicked: fileList.details = false
            }
            IconButton {
                objectName: "detailsButton"
                text: "Details"
                iconName: "view-list-details"
                checkable: true
                checked: fileList.details
                onClicked: fileList.details = true
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            SplitView {
                id: side
                orientation: Qt.Vertical
                SplitView.preferredWidth: 220
                SplitView.minimumWidth: 120

                // The places and the folders stand on what the files do, so the three read
                // as lists alike.
                ListFrame {
                    objectName: "placesFrame"
                    SplitView.preferredHeight: side.height * 0.45
                    SplitView.minimumHeight: 60
                    focused: placesView.activeFocus

                    ListView {
                        id: placesView
                        objectName: "placesView"
                        anchors.fill: parent
                        anchors.margins: Theme.paneInset
                        activeFocusOnTab: true
                        keyNavigationEnabled: true
                        Keys.onReturnPressed: if (currentIndex >= 0) chooser.openFolder(model[currentIndex].path)
                        Keys.onEnterPressed: if (currentIndex >= 0) chooser.openFolder(model[currentIndex].path)
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: AppScrollBar {}
                        section.property: "section"
                        section.delegate: PaneHeader {
                            required property string section
                            width: ListView.view.width
                            text: section
                            focused: placesView.activeFocus
                        }
                        delegate: ItemDelegate {
                            id: placeRow
                            required property int index
                            required property var modelData
                            width: ListView.view.width
                            height: 26
                            leftPadding: 14
                            text: modelData.name
                            highlighted: placesView.activeFocus ? index === placesView.currentIndex : modelData.path === browse.directory
                            background: SelectionBackground {
                                anchors.fill: parent
                                anchors.margins: 1
                                selected: placeRow.highlighted
                                focused: placesView.activeFocus
                                hovered: placeRow.hovered
                            }
                            onClicked: {
                                placesView.currentIndex = index
                                placesView.forceActiveFocus()
                                chooser.openFolder(modelData.path)
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: 800
                            ToolTip.text: browse.nativePath(modelData.path)
                        }
                    }
                }

                ListFrame {
                    objectName: "chooserTreeFrame"
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 60
                    focused: treeLoader.item ? treeLoader.item.activeFocus : false

                    PaneHeader {
                        id: chooserFoldersHeader
                        objectName: "chooserFoldersHeader"
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.paneInset
                        text: "Directories"
                        focused: treeLoader.item ? treeLoader.item.activeFocus : false
                    }

                    // Made only while shown, as each tree watches the file system.
                    Loader {
                        id: treeLoader
                        anchors.top: chooserFoldersHeader.bottom
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: Theme.paneInset
                        active: chooser.visible
                        sourceComponent: FolderTree {
                            objectName: "chooserTree"
                            context: browse
                            onFolderMenuRequested: (path) => chooserFolderMenu.popup(path)
                            onRenameRequested: (path) => chooserFolderMenu.rename(path)
                            onTrashRequested: (path) => chooserFolderMenu.trash(path)
                        }
                    }
                }
            }

            FileListView {
                id: fileList
                objectName: "fileList"
                SplitView.fillWidth: true
                listing: listing
                onClicked: (index) => {
                    const entry = listing.get(index)
                    if (chooser.mode === "open") {
                        chooser.nameField.text = entry.isFolder ? "" : entry.name
                    } else if (chooser.mode === "save" && !entry.isFolder) {
                        chooser.nameField.text = entry.name
                        chooser.lastFileName = entry.name
                        const type = chooser.typeOf(entry.name)
                        if (type >= 0)
                            chooser.typeIndex = type
                    }
                }
                onActivated: (index) => chooser.activate(listing.get(index))
                onUpRequested: browse.goUp()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: chooser.problem !== ""
            text: chooser.problem
            color: Theme.danger
            wrapMode: Text.Wrap
        }

        // The name and the type on the left, each with its button at the right of its row, so
        // that the fields and the buttons line up across. Choosing a folder has no fields, and
        // its buttons stand alone at the right.
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 6

            Label {
                text: "Name:"
                visible: chooser.mode !== "folder"
            }
            TextField {
                id: nameField
                objectName: "nameField"
                Layout.fillWidth: true
                visible: chooser.mode !== "folder"
                selectByMouse: true
                onTextEdited: {
                    chooser.problem = ""
                    if (chooser.mode !== "save")
                        return
                    const type = chooser.typeOf(text)
                    if (type >= 0) {
                        chooser.typeIndex = type
                        chooser.lastFileName = text
                    }
                }
                onAccepted: chooser.submit()
            }
            Item {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: chooser.mode === "folder"
            }
            Button {
                id: acceptButton
                objectName: "acceptButton"
                Layout.preferredWidth: chooser.buttonWidth
                highlighted: true
                text: {
                    if (chooser.mode !== "folder")
                        return chooser.acceptText
                    const entry = fileList.currentIndex >= 0 ? listing.get(fileList.currentIndex) : null
                    return "Choose \u201c" + chooser.folderName(entry && entry.isFolder ? entry.path : browse.directory) + "\u201d"
                }
                enabled: chooser.mode !== "open" || nameField.text.trim() !== "" || fileList.currentIndex >= 0
                onClicked: chooser.submit()
            }

            Label {
                text: "Type:"
                visible: chooser.mode !== "folder" && chooser.types.length > 0
            }
            ComboBox {
                id: typeBox
                objectName: "typeBox"
                Layout.fillWidth: true
                visible: chooser.mode !== "folder" && chooser.types.length > 0
                model: chooser.types.map(type => type.extensions.length === 0 ? type.label
                                         : type.label + " (" + type.extensions.map(e => "*." + e).join(" ") + ")")
                onActivated: (index) => {
                    chooser.typeIndex = index
                    if (chooser.mode === "save") {
                        nameField.text = chooser.withType(nameField.text, index)
                        chooser.lastFileName = nameField.text
                    }
                }
                Component.onCompleted: currentIndex = chooser.typeIndex
            }
            Item {
                Layout.columnSpan: 2
                Layout.fillWidth: true
                visible: !typeBox.visible
            }
            Button {
                id: cancelButton
                objectName: "cancelButton"
                Layout.preferredWidth: chooser.buttonWidth
                text: "Cancel"
                onClicked: chooser.close()
            }
        }
    }

    MessageBox {
        id: replaceBox
        objectName: "replaceBox"
        property string path
        kind: "warning"
        title: "Replace the File?"
        text: chooser.folderName(path) + " already exists in\n" + browse.nativePath(path.substring(0, path.lastIndexOf("/")))
              + "\n\nDo you want to replace it?"
        standardButtons: Dialog.Ok | Dialog.Cancel
        focusedButton: Dialog.Cancel
        onAboutToShow: standardButton(Dialog.Ok).text = "Replace"
        onAccepted: chooser.finish(path)
    }
}

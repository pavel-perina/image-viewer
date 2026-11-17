pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import ImageViewer

// Buttons to move around on the left, apart from the path in its frame. There the caret after a
// crumb lists its subfolders, and a click on the empty end, or Ctrl+L, turns it into text to edit.
Rectangle {
    id: bar

    required property BrowseContext context

    objectName: "pathBar"
    implicitHeight: 34
    color: palette.window

    readonly property color hoverColor: Theme.hover
    readonly property int menuRowHeight: 26

    property bool editing: false
    // Editing ended by Enter or Esc: the keys go back to what had them. A hidden field would keep
    // them otherwise, and take the arrows from the thumbnails.
    signal finished()
    // Folders opened here count as visited, for the recent folders; not in a file chooser.
    property bool recordVisits: true
    property string problem: ""
    readonly property alias editText: field.text
    readonly property alias menuFolders: subfolders.folders
    readonly property alias menuOpen: menu.visible
    readonly property alias menu: menu
    readonly property var crumbNames: context.breadcrumbs.map(crumb => crumb.name)
    readonly property string boldCrumb: crumbNames.length > 0 ? crumbNames[crumbNames.length - 1] : ""

    function edit() {
        problem = ""
        field.text = context.nativePath(context.directory)
        editing = true
        field.forceActiveFocus()
        field.selectAll()
    }

    function submit(text) {
        problem = context.openTyped(text)
        if (!problem) {
            editing = false
            finished()
        }
    }

    // Pastes into the field rather than opening at once, if it cannot be opened.
    function pasteAndGo() {
        const text = FileClipboard.text()
        submit(text)
        if (problem) {
            field.text = text
            editing = true
            field.forceActiveFocus()
        }
    }

    function openSubfolders(path, anchorItem) {
        menu.parentPath = path
        // The folder this one leads to in the path, to start the list there.
        const crumbs = context.breadcrumbs
        menu.nextName = ""
        for (let i = 0; i + 1 < crumbs.length; ++i) {
            if (crumbs[i].path === path)
                menu.nextName = crumbs[i + 1].name
        }
        subfolders.path = path
        if (anchorItem) {
            const place = anchorItem.mapToItem(Overlay.overlay, 0, anchorItem.height)
            // Stays inside the window under a caret at the far right.
            menu.x = Math.max(0, Math.min(place.x, Overlay.overlay.width - menu.width - 4))
            menu.y = place.y
        }
        menu.open()
    }

    function chooseSubfolder(name) {
        const path = menu.parentPath
        menu.close()
        context.openPath(path.endsWith("/") ? path + name : path + "/" + name)
    }

    onEditingChanged: if (!editing) problem = ""

    SubfolderList {
        id: subfolders
        showHidden: Preferences.showHiddenFolders
    }

    // What back would pass through, to jump there at once. Most recent first.
    function showBackHistory(anchorItem) {
        const folders = context.backFolders()
        if (folders.length === 0)
            return
        historyMenu.folders = folders
        historyMenu.popup(anchorItem, 0, anchorItem.height)
    }

    Connections {
        target: bar.context
        enabled: bar.recordVisits
        function onDirectoryChanged() {
            FolderLists.noteVisited(bar.context.directory)
        }
    }

    Row {
        id: navigationRow
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        BarButton {
            id: backButton
            kind: "back"
            usable: bar.context.canGoBack
            tip: "Back (Alt+Left), right-click for history"
            menuOpen: historyMenu.visible
            onActivated: bar.context.goBack()
            onSecondaryActivated: bar.showBackHistory(backButton)
        }
        BarButton {
            kind: "forward"
            usable: bar.context.canGoForward
            tip: "Forward (Alt+Right)"
            onActivated: bar.context.goForward()
        }
        BarButton {
            kind: "up"
            usable: bar.context.canGoUp
            tip: "Parent folder (Alt+Up)"
            onActivated: bar.context.goUp()
        }
        BarButton {
            kind: "refresh"
            usable: bar.context.directory !== ""
            tip: "Scan the folder again (F5)"
            onActivated: bar.context.refresh()
        }
        FolderButton {
            objectName: "folderButton"
            context: bar.context
        }
    }

    // Sets the path apart from the buttons, which are not part of it.
    Rectangle {
        anchors.fill: pathArea
        anchors.topMargin: (bar.height - Theme.controlHeight) / 2
        anchors.bottomMargin: (bar.height - Theme.controlHeight) / 2
        radius: Theme.controlRadius
        color: Theme.base
        border.width: 1
        border.color: Theme.mid
        visible: !bar.editing
    }

    Item {
        id: pathArea
        anchors.left: navigationRow.right
        anchors.leftMargin: 6
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        // A click past the last crumb edits. It keeps some room even for a very deep path.
        readonly property int editRoom: 48

        Flickable {
            id: crumbsView
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(crumbsRow.width, parent.width - pathArea.editRoom)
            visible: !bar.editing
            clip: true
            contentWidth: crumbsRow.width
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds

            // The end of a deep path is what matters, so it starts there.
            function showEnd() {
                contentX = Math.max(0, contentWidth - width)
            }
            onContentWidthChanged: showEnd()
            onWidthChanged: showEnd()

            // A wheel has only one direction, and here it can only mean sideways.
            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    const step = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                    crumbsView.contentX = Math.max(0, Math.min(crumbsView.contentWidth - crumbsView.width, crumbsView.contentX - step / 2))
                }
            }

            // A hint that part of a deep path is out of sight. The wheel does the scrolling.
            ScrollBar.horizontal: AppScrollBar {
                thickness: 4
                padding: 1
                interactive: false
            }

            Row {
                id: crumbsRow
                height: crumbsView.height

                Repeater {
                    model: bar.context.breadcrumbs

                    delegate: Row {
                        id: crumb
                        required property var modelData
                        required property int index
                        readonly property bool isLast: index === bar.context.breadcrumbs.length - 1
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        height: 26

                        Rectangle {
                            width: crumbLabel.implicitWidth + 12
                            height: parent.height
                            radius: 3
                            color: crumbHover.hovered ? bar.hoverColor : "transparent"
                            Label {
                                id: crumbLabel
                                anchors.centerIn: parent
                                text: crumb.modelData.name
                                font.weight: crumb.isLast ? Font.DemiBold : Font.Normal
                            }
                            HoverHandler { id: crumbHover }
                            TapHandler {
                                onTapped: bar.context.openPath(crumb.modelData.path)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    pathMenu.path = crumb.modelData.path
                                    pathMenu.popup()
                                }
                            }
                        }

                        Rectangle {
                            id: caretButton
                            width: 18
                            height: parent.height
                            radius: 3
                            readonly property bool isOpen: menu.visible && menu.parentPath === crumb.modelData.path
                            color: caretHover.hovered || isOpen ? bar.hoverColor : "transparent"
                            Caret {
                                anchors.centerIn: parent
                                rotationAngle: caretButton.isOpen ? 90 : 0
                                fill: caretHover.hovered || caretButton.isOpen ? palette.windowText : Theme.caret
                            }
                            HoverHandler { id: caretHover }
                            TapHandler {
                                onTapped: bar.openSubfolders(crumb.modelData.path, caretButton)
                            }
                        }
                    }
                }
            }
        }

        // Beside the crumbs rather than around them, so that a click on a crumb is not also this.
        Item {
            anchors.left: crumbsView.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            visible: !bar.editing
            TapHandler {
                onTapped: bar.edit()
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    pathMenu.path = bar.context.directory
                    pathMenu.popup()
                }
            }
        }

        TextField {
            id: field
            anchors.fill: parent
            anchors.topMargin: (bar.height - Theme.controlHeight) / 2
            anchors.bottomMargin: (bar.height - Theme.controlHeight) / 2
            visible: bar.editing
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            onAccepted: bar.submit(text)
            onActiveFocusChanged: if (!activeFocus) bar.editing = false
            onTextEdited: bar.problem = ""
            Keys.onEscapePressed: {
                bar.editing = false
                bar.finished()
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: Theme.base
                border.width: 1
                border.color: bar.problem ? Theme.danger : Theme.accent
            }
            ToolTip.visible: bar.problem !== ""
            ToolTip.text: bar.problem
        }
    }

    AppMenu {
        id: historyMenu
        objectName: "historyMenu"
        property var folders: []
        parent: Overlay.overlay
        width: 420
        Instantiator {
            model: historyMenu.folders
            delegate: MenuItem {
                id: historyItem
                required property string modelData
                required property int index
                onTriggered: bar.context.goBackBy(index + 1)
                // In the middle, as folders often differ only at the end of a long path.
                contentItem: Label {
                    leftPadding: 8
                    text: bar.context.nativePath(historyItem.modelData)
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
            }
            onObjectAdded: (index, item) => historyMenu.insertItem(index, item)
            onObjectRemoved: (index, item) => historyMenu.removeItem(item)
        }
    }

    AppMenu {
        id: pathMenu
        property string path
        parent: Overlay.overlay
        AppMenuItem {
            text: "&Copy Path"
            onTriggered: FileClipboard.copyPaths([pathMenu.path])
        }
        AppMenuItem {
            text: "&Paste and Go"
            onTriggered: bar.pasteAndGo()
        }
        AppMenuItem {
            text: "&Edit Path"
            onTriggered: bar.edit()
        }
    }

    // Modal so that shortcuts for the grid do not take its arrow keys, without dimming the window.
    Popup {
        id: menu
        property string parentPath
        property string nextName
        parent: Overlay.overlay
        modal: true
        dim: false
        padding: 2
        width: 320
        height: Math.min(Math.max(1, list.count) * bar.menuRowHeight + 2 * padding,
                         Math.max(80, (parent ? parent.height : 400) - y - 8))
        onOpened: list.forceActiveFocus()
        onClosed: subfolders.path = ""

        background: Rectangle {
            color: palette.base
            border.width: 1
            border.color: bar.hoverColor
        }

        ListView {
            id: list
            anchors.fill: parent
            clip: true
            model: subfolders.folders
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            highlightMoveDuration: 0

            // Starts at the folder the path leads through, otherwise at the top.
            onModelChanged: Qt.callLater(() => {
                currentIndex = Math.max(0, subfolders.folders.indexOf(menu.nextName))
                positionViewAtIndex(currentIndex, ListView.Center)
            })

            Keys.onReturnPressed: if (currentIndex >= 0) bar.chooseSubfolder(subfolders.folders[currentIndex])
            Keys.onEnterPressed: if (currentIndex >= 0) bar.chooseSubfolder(subfolders.folders[currentIndex])

            ScrollBar.vertical: AppScrollBar {}

            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                width: ListView.view.width
                height: bar.menuRowHeight
                radius: 2
                color: row.index === list.currentIndex ? palette.highlight : rowHover.hovered ? bar.hoverColor : "transparent"
                Label {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    text: row.modelData
                    color: row.index === list.currentIndex ? palette.highlightedText : palette.text
                    font.weight: row.modelData === menu.nextName ? Font.DemiBold : Font.Normal
                }
                HoverHandler { id: rowHover }
                // Not a TapHandler, which leaves the press to whatever is below the list too.
                MouseArea {
                    anchors.fill: parent
                    onClicked: bar.chooseSubfolder(row.modelData)
                }
            }
        }

        Label {
            anchors.centerIn: parent
            visible: list.count === 0
            text: subfolders.loading ? "Reading..." : "No subfolders"
            opacity: 0.6
        }
    }
}

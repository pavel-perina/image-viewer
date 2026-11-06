pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import ImageViewer

// Where we are: back, forward and up, then the path as flat crumbs. The caret after a crumb lists
// its subfolders. A click on the empty end, or Ctrl+L, turns the path into text to edit or paste.
Rectangle {
    id: bar

    required property BrowseContext context

    objectName: "pathBar"
    implicitHeight: 34
    color: palette.window

    // Carets stay behind the names they separate.
    readonly property color caretColor: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.55)
    readonly property color hoverColor: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.12)
    readonly property int menuRowHeight: 26

    property bool editing: false
    property string problem: ""
    readonly property alias editText: field.text
    readonly property alias menuFolders: subfolders.folders
    readonly property alias menuOpen: menu.visible
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
        if (!problem)
            editing = false
    }

    // Pastes into the field rather than opening at once, if it cannot be opened.
    function pasteAndGo() {
        const text = context.clipboardText()
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
            menu.x = place.x
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
    }

    // A small triangle, pointing the way it leads.
    component Caret: Canvas {
        id: caret
        property int rotationAngle: 0  // 0 points right
        property color fill: bar.caretColor
        implicitWidth: 10
        implicitHeight: 10
        rotation: rotationAngle
        onFillChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = fill
            ctx.beginPath()
            ctx.moveTo(3, 1.5)
            ctx.lineTo(7.5, 5)
            ctx.lineTo(3, 8.5)
            ctx.closePath()
            ctx.fill()
        }
    }

    component NavigationButton: Rectangle {
        id: navigation
        property int direction: 0
        property bool usable: true
        signal activated()
        width: 26
        height: 26
        radius: 3
        color: navigationHover.hovered && usable ? bar.hoverColor : "transparent"
        Caret {
            anchors.centerIn: parent
            scale: 1.3
            rotationAngle: navigation.direction
            fill: navigation.usable ? palette.windowText : Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.25)
        }
        HoverHandler { id: navigationHover }
        TapHandler {
            enabled: navigation.usable
            onTapped: navigation.activated()
        }
    }

    Row {
        id: navigationRow
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        NavigationButton {
            direction: 180
            usable: bar.context.canGoBack
            onActivated: bar.context.goBack()
            ToolTip.visible: backTip.hovered
            ToolTip.delay: 600
            ToolTip.text: "Back (Alt+Left)"
            HoverHandler { id: backTip }
        }
        NavigationButton {
            direction: 0
            usable: bar.context.canGoForward
            onActivated: bar.context.goForward()
            ToolTip.visible: forwardTip.hovered
            ToolTip.delay: 600
            ToolTip.text: "Forward (Alt+Right)"
            HoverHandler { id: forwardTip }
        }
        NavigationButton {
            direction: 270
            usable: bar.context.canGoUp
            onActivated: bar.context.goUp()
            ToolTip.visible: upTip.hovered
            ToolTip.delay: 600
            ToolTip.text: "Parent folder (Alt+Up)"
            HoverHandler { id: upTip }
        }
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

            ScrollIndicator.horizontal: ScrollIndicator {}

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
            anchors.topMargin: 3
            anchors.bottomMargin: 3
            visible: bar.editing
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            onAccepted: bar.submit(text)
            onActiveFocusChanged: if (!activeFocus) bar.editing = false
            onTextEdited: bar.problem = ""
            Keys.onEscapePressed: bar.editing = false
            background: Rectangle {
                radius: 3
                color: palette.base
                border.width: 1
                border.color: bar.problem ? "#d9534f" : palette.highlight
            }
            ToolTip.visible: bar.problem !== ""
            ToolTip.text: bar.problem
        }
    }

    Menu {
        id: pathMenu
        property string path
        parent: Overlay.overlay
        MenuItem {
            text: "&Copy Path"
            onTriggered: bar.context.copyPathsToClipboard([pathMenu.path])
        }
        MenuItem {
            text: "&Paste and Go"
            onTriggered: bar.pasteAndGo()
        }
        MenuItem {
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

            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

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
                TapHandler { onTapped: bar.chooseSubfolder(row.modelData) }
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

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ImageViewer

ApplicationWindow {
    id: window

    required property string initialPath
    property bool viewing: false
    readonly property ImageView activeView: viewing ? viewerPage.view : browserPage.preview

    width: 1280
    height: 800
    visible: true
    title: context.currentName ? context.currentName + " - Image Viewer" : "Image Viewer"

    // Created here rather than in C++, so a tab can later own one context and its pages.
    BrowseContext {
        id: context
    }

    // Opening a file goes straight to the large view; opening a directory shows the browser.
    Component.onCompleted: {
        context.openPath(initialPath)
        window.viewing = context.currentPath !== ""
    }

    function toggleFullScreen() {
        window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    function leaveCurrentMode() {
        if (window.visibility === Window.FullScreen)
            window.visibility = Window.Windowed
        else
            window.viewing = false
    }

    Action { id: openAction; text: "&Open..."; shortcut: StandardKey.Open; onTriggered: openDialog.open() }
    Action { id: quitAction; text: "&Quit"; shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
    Action {
        id: modeAction
        text: window.viewing ? "Browser" : "View"
        enabled: window.viewing || context.currentPath !== ""
        onTriggered: window.viewing = !window.viewing
    }
    Action { id: previousAction; text: "Previous"; enabled: context.hasPrevious; onTriggered: context.previous() }
    Action { id: nextAction; text: "Next"; enabled: context.hasNext; onTriggered: context.next() }
    Action { id: zoomInAction; text: "Zoom In"; shortcut: "+"; onTriggered: window.activeView.zoomIn() }
    Action { id: zoomOutAction; text: "Zoom Out"; shortcut: "-"; onTriggered: window.activeView.zoomOut() }
    Action { id: fitAction; text: "Fit"; shortcut: "F"; onTriggered: window.activeView.fitToView() }
    Action { id: actualSizeAction; text: "1:1"; shortcut: "1"; onTriggered: window.activeView.actualSize() }
    Action { id: fullScreenAction; text: "Full Screen"; shortcut: "F11"; onTriggered: window.toggleFullScreen() }

    Shortcut { sequences: ["Right", "PgDown", "Space"]; onActivated: context.next() }
    Shortcut { sequences: ["Left", "PgUp", "Backspace"]; onActivated: context.previous() }
    Shortcut { sequence: "Up"; enabled: !window.viewing; onActivated: browserPage.moveCurrent(-1) }
    Shortcut { sequence: "Down"; enabled: !window.viewing; onActivated: browserPage.moveCurrent(1) }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !window.viewing && context.currentPath !== ""
        onActivated: window.viewing = true
    }
    Shortcut { sequence: "="; onActivated: window.activeView.zoomIn() }
    Shortcut {
        sequence: "Esc"
        enabled: window.viewing || window.visibility === Window.FullScreen
        onActivated: window.leaveCurrentMode()
    }

    menuBar: MenuBar {
        visible: window.visibility !== Window.FullScreen

        Menu {
            title: "&File"
            MenuItem { action: openAction }
            MenuSeparator {}
            MenuItem { action: quitAction }
        }
        Menu {
            title: "&View"
            MenuItem { action: modeAction }
            MenuSeparator {}
            MenuItem { action: previousAction }
            MenuItem { action: nextAction }
            MenuSeparator {}
            MenuItem { action: zoomInAction }
            MenuItem { action: zoomOutAction }
            MenuItem { action: fitAction }
            MenuItem { action: actualSizeAction }
            MenuSeparator {}
            MenuItem { action: fullScreenAction }
        }
        Menu {
            title: "&Help"
            MenuItem { text: "&About"; onTriggered: aboutDialog.open() }
        }
    }

    header: ToolBar {
        visible: window.visibility !== Window.FullScreen

        RowLayout {
            anchors.fill: parent
            ToolButton { action: modeAction }
            ToolSeparator {}
            ToolButton { action: previousAction }
            ToolButton { action: nextAction }
            ToolSeparator {}
            ToolButton { action: zoomInAction }
            ToolButton { action: zoomOutAction }
            ToolButton { action: fitAction }
            ToolButton { action: actualSizeAction }
            Item { Layout.fillWidth: true }
        }
    }

    footer: ToolBar {
        visible: window.visibility !== Window.FullScreen

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            Label {
                text: context.currentName
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                visible: window.activeView.imageWidth > 0
                text: window.activeView.imageWidth + " x " + window.activeView.imageHeight
            }
            Label {
                visible: window.activeView.imageWidth > 0
                text: Math.round(window.activeView.zoom * 100) + "%"
            }
            Label {
                visible: context.fileCount > 0
                text: (context.currentIndex + 1) + " / " + context.fileCount
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: window.viewing ? 1 : 0

        BrowserPage {
            id: browserPage
            context: context
            active: !window.viewing
            onViewRequested: window.viewing = true
        }

        ViewerPage {
            id: viewerPage
            context: context
            active: window.viewing
        }
    }

    FileDialog {
        id: openDialog
        title: "Open Image"
        nameFilters: ["JPEG images (*.jpg *.jpeg *.jpe *.jfif)", "All files (*)"]
        onAccepted: context.openUrl(selectedFile)
    }

    Dialog {
        id: aboutDialog
        anchors.centerIn: parent
        title: "About Image Viewer"
        standardButtons: Dialog.Ok
        Label { text: "A fast, multiplatform image viewer and basic editor." }
    }
}

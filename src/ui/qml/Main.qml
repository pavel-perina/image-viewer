pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ImageViewer

ApplicationWindow {
    id: window

    required property string initialPath

    width: 1280
    height: 800
    visible: true
    title: context.currentName ? context.currentName + " - Image Viewer" : "Image Viewer"

    // Created here rather than in C++, so a tab can later own one context and one view.
    BrowseContext {
        id: context
    }

    Component.onCompleted: {
        if (initialPath)
            context.openPath(initialPath)
    }

    function toggleFullScreen() {
        window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Action { id: openAction; text: "&Open..."; shortcut: StandardKey.Open; onTriggered: openDialog.open() }
    Action { id: quitAction; text: "&Quit"; shortcut: StandardKey.Quit; onTriggered: Qt.quit() }
    Action { id: previousAction; text: "Previous"; enabled: context.hasPrevious; onTriggered: context.previous() }
    Action { id: nextAction; text: "Next"; enabled: context.hasNext; onTriggered: context.next() }
    Action { id: zoomInAction; text: "Zoom In"; shortcut: "+"; onTriggered: view.zoomIn() }
    Action { id: zoomOutAction; text: "Zoom Out"; shortcut: "-"; onTriggered: view.zoomOut() }
    Action { id: fitAction; text: "Fit"; shortcut: "F"; onTriggered: view.fitToView() }
    Action { id: actualSizeAction; text: "1:1"; shortcut: "1"; onTriggered: view.actualSize() }
    Action { id: fullScreenAction; text: "Full Screen"; shortcut: "F11"; onTriggered: window.toggleFullScreen() }

    Shortcut { sequences: ["Right", "PgDown", "Space"]; onActivated: context.next() }
    Shortcut { sequences: ["Left", "PgUp", "Backspace"]; onActivated: context.previous() }
    Shortcut { sequence: "="; onActivated: view.zoomIn() }
    Shortcut {
        sequence: "Esc"
        enabled: window.visibility === Window.FullScreen
        onActivated: window.visibility = Window.Windowed
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
                visible: view.imageWidth > 0
                text: view.imageWidth + " x " + view.imageHeight
            }
            Label {
                visible: view.imageWidth > 0
                text: Math.round(view.zoom * 100) + "%"
            }
            Label {
                visible: context.fileCount > 0
                text: (context.currentIndex + 1) + " / " + context.fileCount
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#202020"

        ImageView {
            id: view
            anchors.fill: parent
            clip: true
            source: context.currentPath
        }

        Label {
            anchors.centerIn: parent
            color: "#e0e0e0"
            visible: view.status === ImageView.Error || !context.currentPath
            text: view.status === ImageView.Error
                  ? "Cannot open " + context.currentName + ": " + view.errorString
                  : "Open an image with File > Open"
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

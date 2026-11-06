pragma ComponentBehavior: Bound

import QtCore
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
    // Text fields do not claim keys such as Return and Up, so these shortcuts would take them.
    readonly property bool typing: browserPage.pathBar.editing

    width: 1280
    height: 800
    visible: true
    title: context.currentName ? context.currentName + " - Image Viewer" : "Image Viewer"

    // Created here rather than in C++, so a tab can later own one context and its pages.
    BrowseContext {
        id: context
        onEmptied: window.viewing = false
    }

    FileActions {
        id: fileActions
        context: context
        viewing: window.viewing
    }

    Settings {
        id: browserSettings
        category: "Browser"
        property int sortKey: BrowseContext.SortByName
        property bool sortDescending: false
    }

    ActionGroup { id: sortGroup }

    // Opening a file goes straight to the large view; opening a directory shows the browser.
    Component.onCompleted: {
        context.sortKey = browserSettings.sortKey
        context.sortDescending = browserSettings.sortDescending
        context.openPath(initialPath)
        window.viewing = context.currentPath !== ""
    }

    function toggleFullScreen() {
        window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    function sortBy(key) {
        browserSettings.sortKey = key
        context.sortKey = key
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
    Action {
        id: previousPageAction
        text: "Prev Page"
        enabled: window.activeView.page > 0
        onTriggered: window.activeView.page = window.activeView.page - 1
    }
    Action {
        id: nextPageAction
        text: "Next Page"
        enabled: window.activeView.page + 1 < window.activeView.pageCount
        onTriggered: window.activeView.page = window.activeView.page + 1
    }
    Action {
        id: autoContrastAction
        text: "Auto Contrast"
        checkable: true
        enabled: window.activeView.contrastAdjustable
        checked: window.activeView.autoContrast
        onTriggered: window.activeView.autoContrast = checked
    }
    Action {
        id: colorManagedAction
        text: "&Colour Management"
        shortcut: "C"
        checkable: true
        checked: ColorSettings.enabled
        onTriggered: ColorSettings.enabled = checked
    }
    Action {
        id: infoAction
        text: "Info"
        shortcut: "I"
        checkable: true
        enabled: window.viewing && window.visibility !== Window.FullScreen
        checked: viewerPage.panelPinned
        onTriggered: viewerPage.panelPinned = checked
    }
    Action {
        id: animatePanelAction
        text: "Animate Info Panel"
        checkable: true
        checked: viewerPage.panelAnimated
        onTriggered: viewerPage.panelAnimated = checked
    }
    Action {
        id: sharpResamplingAction
        text: "&Sharp Resampling"
        shortcut: "M"
        checkable: true
        checked: viewerPage.sharpResampling
        onTriggered: viewerPage.sharpResampling = checked
    }
    Action { id: zoomInAction; text: "Zoom In"; shortcut: "+"; onTriggered: window.activeView.zoomIn() }
    Action { id: zoomOutAction; text: "Zoom Out"; shortcut: "-"; onTriggered: window.activeView.zoomOut() }
    Action { id: fitAction; text: "Fit"; shortcut: "F"; onTriggered: window.activeView.fitToView() }
    Action { id: actualSizeAction; text: "1:1"; shortcut: "1"; onTriggered: window.activeView.actualSize() }
    Action { id: fullScreenAction; text: "Full Screen"; shortcut: "F11"; onTriggered: window.toggleFullScreen() }

    Shortcut { sequences: ["Right", "PgDown", "Space"]; enabled: !window.typing; onActivated: context.next() }
    Shortcut { sequences: ["Left", "PgUp", "Backspace"]; enabled: !window.typing; onActivated: context.previous() }
    Shortcut { sequence: "Ctrl+PgUp"; onActivated: previousPageAction.trigger() }
    Shortcut { sequence: "Ctrl+PgDown"; onActivated: nextPageAction.trigger() }
    Shortcut { sequence: "Up"; enabled: !window.viewing && !window.typing; onActivated: browserPage.moveCurrent(-1) }
    Shortcut { sequence: "Down"; enabled: !window.viewing && !window.typing; onActivated: browserPage.moveCurrent(1) }
    Shortcut { sequence: "Shift+Left"; enabled: !window.viewing; onActivated: context.extendSelectionTo(context.currentIndex - 1) }
    Shortcut { sequence: "Shift+Right"; enabled: !window.viewing; onActivated: context.extendSelectionTo(context.currentIndex + 1) }
    Shortcut { sequence: "Shift+Up"; enabled: !window.viewing; onActivated: browserPage.extendSelection(-1) }
    Shortcut { sequence: "Shift+Down"; enabled: !window.viewing; onActivated: browserPage.extendSelection(1) }
    Shortcut { sequence: "Ctrl+A"; enabled: !window.viewing; onActivated: context.selectAll() }
    Action { id: backAction; text: "&Back"; shortcut: "Alt+Left"; enabled: !window.viewing && context.canGoBack; onTriggered: context.goBack() }
    Action { id: forwardAction; text: "&Forward"; shortcut: "Alt+Right"; enabled: !window.viewing && context.canGoForward; onTriggered: context.goForward() }
    Action { id: upAction; text: "&Up"; shortcut: "Alt+Up"; enabled: !window.viewing && context.canGoUp; onTriggered: context.goUp() }
    Action { id: editPathAction; text: "&Edit Location"; shortcut: "Ctrl+L"; enabled: !window.viewing; onTriggered: browserPage.pathBar.edit() }
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: !window.viewing && !window.typing && context.currentPath !== ""
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
            MenuItem { action: fileActions.renameAction }
            MenuItem { action: fileActions.trashAction }
            MenuItem { action: fileActions.deleteAction }
            MenuItem { action: fileActions.copyToAction }
            MenuItem { action: fileActions.moveToAction }
            MenuSeparator {}
            MenuItem { action: fileActions.newFolderAction }
            MenuItem { action: fileActions.openFolderAction }
            MenuSeparator {}
            MenuItem { action: quitAction }
        }
        Menu {
            title: "&Edit"
            MenuItem { action: fileActions.copyAction }
            MenuItem { action: fileActions.pasteAction }
            MenuItem { action: fileActions.copyPathAction }
        }
        Menu {
            title: "&View"
            MenuItem { action: modeAction }
            MenuItem { action: fileActions.refreshAction }
            Menu {
                title: "&Sort By"
                Action { text: "&Name"; checkable: true; ActionGroup.group: sortGroup; checked: context.sortKey === BrowseContext.SortByName; onTriggered: window.sortBy(BrowseContext.SortByName) }
                Action { text: "&Date Modified"; checkable: true; ActionGroup.group: sortGroup; checked: context.sortKey === BrowseContext.SortByDate; onTriggered: window.sortBy(BrowseContext.SortByDate) }
                Action { text: "&Size"; checkable: true; ActionGroup.group: sortGroup; checked: context.sortKey === BrowseContext.SortBySize; onTriggered: window.sortBy(BrowseContext.SortBySize) }
                Action { text: "&Type"; checkable: true; ActionGroup.group: sortGroup; checked: context.sortKey === BrowseContext.SortByType; onTriggered: window.sortBy(BrowseContext.SortByType) }
                MenuSeparator {}
                Action {
                    text: "D&escending"
                    checkable: true
                    checked: context.sortDescending
                    onTriggered: {
                        browserSettings.sortDescending = checked
                        context.sortDescending = checked
                    }
                }
            }
            MenuSeparator {}
            MenuItem { action: previousAction }
            MenuItem { action: nextAction }
            MenuSeparator {}
            MenuItem { action: previousPageAction }
            MenuItem { action: nextPageAction }
            MenuSeparator {}
            MenuItem { action: zoomInAction }
            MenuItem { action: zoomOutAction }
            MenuItem { action: fitAction }
            MenuItem { action: actualSizeAction }
            MenuItem { action: autoContrastAction }
            MenuItem { action: sharpResamplingAction }
            MenuSeparator {}
            MenuItem { action: colorManagedAction }
            MenuItem { text: "Display &Profile: " + ColorSettings.displayProfileName + "..."; onTriggered: displayProfileDialog.open() }
            MenuItem { text: "Use s&RGB Display"; enabled: ColorSettings.hasDisplayProfile; onTriggered: ColorSettings.useSrgbDisplay() }
            MenuSeparator {}
            MenuItem { action: infoAction }
            MenuItem { action: animatePanelAction }
            MenuSeparator {}
            MenuItem { action: fullScreenAction }
        }
        Menu {
            title: "&Go"
            MenuItem { action: backAction }
            MenuItem { action: forwardAction }
            MenuItem { action: upAction }
            MenuSeparator {}
            MenuItem { action: editPathAction }
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
            ToolButton { action: previousPageAction; visible: window.activeView.pageCount > 1 }
            ToolButton { action: nextPageAction; visible: window.activeView.pageCount > 1 }
            ToolSeparator { visible: window.activeView.pageCount > 1 }
            ToolButton { action: zoomInAction }
            ToolButton { action: zoomOutAction }
            ToolButton { action: fitAction }
            ToolButton { action: actualSizeAction }
            ToolButton { action: autoContrastAction; visible: window.activeView.contrastAdjustable }
            ToolButton { action: infoAction; visible: window.viewing }
            Item { Layout.fillWidth: true }
        }
    }

    footer: ToolBar {
        visible: window.visibility !== Window.FullScreen

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            // Only for jobs long enough to notice, so a quick delete does not flicker.
            Timer {
                id: jobDelay
                interval: 300
                running: context.busy
                onRunningChanged: if (running) fileActions.showProgress = false
                onTriggered: fileActions.showProgress = true
            }
            Label {
                visible: context.busy && fileActions.showProgress
                text: context.jobName + " " + context.jobDone + " / " + context.jobTotal
            }
            ProgressBar {
                visible: context.busy && fileActions.showProgress
                from: 0
                to: Math.max(1, context.jobTotal)
                value: context.jobDone
            }
            ToolButton {
                visible: context.busy && fileActions.showProgress
                text: "Cancel"
                onClicked: context.cancelJob()
            }
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
                visible: window.activeView.pageCount > 1
                text: "page " + (window.activeView.page + 1) + " / " + window.activeView.pageCount
            }
            Label {
                visible: !window.viewing && context.selectedCount > 1
                text: context.selectedCount + " selected"
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
            onFileMenuRequested: fileActions.fileMenu.popup()
            onFolderMenuRequested: (path) => fileActions.showFolderMenu(path)
        }

        ViewerPage {
            id: viewerPage
            context: context
            active: window.viewing
            fullScreen: window.visibility === Window.FullScreen
            onFileMenuRequested: fileActions.fileMenu.popup()
        }
    }

    FileDialog {
        id: displayProfileDialog
        title: "Choose Display Profile"
        nameFilters: ["ICC profiles (*.icc *.icm)", "All files (*)"]
        onAccepted: {
            const problem = ColorSettings.useDisplayProfile(selectedFile)
            if (problem) {
                profileProblem.text = problem
                profileProblemDialog.open()
            }
        }
    }

    Dialog {
        id: profileProblemDialog
        anchors.centerIn: parent
        title: "Cannot Use This Profile"
        standardButtons: Dialog.Ok
        Label { id: profileProblem }
    }

    FileDialog {
        id: openDialog
        title: "Open Image"
        nameFilters: ["Images (*.jpg *.jpeg *.jpe *.jfif *.tif *.tiff *.png *.webp *.avif *.jxl)", "All files (*)"]
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

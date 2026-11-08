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
    readonly property bool cropping: viewerPage.cropping
    readonly property bool probing: viewerPage.probing
    readonly property bool straightening: viewerPage.straightening
    readonly property bool adjusting: viewerPage.adjusting
    // A tool of the large view has the mouse and keys, so moving to another image waits.
    readonly property bool toolActive: viewerPage.toolActive
    readonly property bool edited: window.viewing && viewerPage.view.modified
    // What was to happen when the unsaved changes were asked about, until they are saved.
    property var pendingLeave: null
    // An edit started in the browser opens the image first and starts once it has loaded.
    property var pendingTool: null
    // Crop, straighten and resize work from the browser on the one image chosen there.
    readonly property bool canEdit: window.viewing ? viewerPage.view.editable && viewerPage.view.status === ImageView.Ready
                                                   : context.currentPath !== "" && context.selectedPaths.length <= 1

    width: 1280
    height: 800
    visible: true
    title: (window.edited ? "*" : "") + (context.currentName ? context.currentName + " - Image Viewer" : "Image Viewer")

    // needsEdit: the tool edits the image, so it starts only on one that can be edited.
    function startTool(start, needsEdit = true) {
        if (window.viewing) {
            start()
            return
        }
        window.pendingTool = start
        window.pendingToolNeedsEdit = needsEdit
        window.viewing = true
    }
    property bool pendingToolNeedsEdit: true

    // Of two images chosen in the browser, the one not shown.
    readonly property string compareTarget: {
        const paths = context.selectedPaths
        if (paths.length !== 2)
            return ""
        return paths[0] === context.currentPath ? paths[1] : paths[1] === context.currentPath ? paths[0] : ""
    }

    Connections {
        target: viewerPage.view
        function onStatusChanged() {
            const view = viewerPage.view
            if (!window.pendingTool || view.status === ImageView.Loading || view.status === ImageView.Null)
                return
            const start = window.pendingTool
            window.pendingTool = null
            if (window.viewing && view.status === ImageView.Ready && (view.editable || !window.pendingToolNeedsEdit))
                start()
        }
    }

    // Leaving an edited image asks first: save, discard or stay.
    function leaveImage(proceed) {
        if (!window.edited) {
            proceed()
            return
        }
        window.pendingLeave = proceed
        unsavedDialog.text = "Save the changes to " + context.currentName + " before leaving it?"
        unsavedDialog.open()
    }

    // The window closes only once nothing is left unsaved.
    property bool closingConfirmed: false
    onClosing: (close) => {
        if (window.edited && !window.closingConfirmed) {
            close.accepted = false
            window.leaveImage(() => {
                window.closingConfirmed = true
                window.close()
            })
        }
    }

    // Created here rather than in C++, so a tab can later own one context and its pages.
    BrowseContext {
        id: context
        onEmptied: window.viewing = false
    }

    FileActions {
        id: fileActions
        context: context
        viewing: window.viewing
        page: window.activeView.source === context.currentPath ? window.activeView.page : 0
        editView: window.viewing ? viewerPage.view : null
        guard: window.leaveImage
        copyTakenByTool: window.probing
        toolActive: window.toolActive
        onSaveCancelled: window.pendingLeave = null
    }

    Connections {
        target: context
        function onEditedImageSaved(edited) {
            viewerPage.view.markSaved(edited)
            if (window.pendingLeave && !viewerPage.view.modified) {
                const proceed = window.pendingLeave
                window.pendingLeave = null
                proceed()
            }
        }
    }

    ResizeDialog {
        id: resizeDialog
        view: viewerPage.view
    }

    MessageBox {
        id: unsavedDialog
        objectName: "unsavedDialog"
        kind: "question"
        title: "Unsaved Changes"
        standardButtons: Dialog.Save | Dialog.Discard | Dialog.Cancel
        focusedButton: Dialog.Save
        onAccepted: fileActions.save()
        onDiscarded: {
            close()
            const proceed = window.pendingLeave
            window.pendingLeave = null
            viewerPage.view.discardEdits()
            if (proceed)
                proceed()
        }
        onRejected: window.pendingLeave = null
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
        if (window.cropping)
            viewerPage.cancelCrop()
        else if (window.straightening)
            viewerPage.cancelStraighten()
        else if (window.adjusting)
            viewerPage.cancelAdjust()
        else if (viewerPage.comparing !== "")
            viewerPage.stopCompare()
        else if (window.probing)
            viewerPage.stopProbe()
        else if (window.visibility === Window.FullScreen)
            window.visibility = Window.Windowed
        else
            window.leaveImage(() => window.viewing = false)
    }

    // Moves to another image, which leaves the edited one.
    function next() { window.leaveImage(() => context.next()) }
    function previous() { window.leaveImage(() => context.previous()) }

    Action { id: openAction; text: "&Open..."; shortcut: StandardKey.Open; onTriggered: openDialog.open() }
    Action { id: quitAction; text: "&Quit"; shortcut: StandardKey.Quit; onTriggered: window.close() }
    Action {
        id: modeAction
        text: window.viewing ? "Browser" : "View"
        enabled: (window.viewing || context.currentPath !== "") && !window.toolActive
        onTriggered: window.viewing ? window.leaveImage(() => window.viewing = false) : window.viewing = true
    }
    Action { id: previousAction; text: "Previous"; enabled: context.hasPrevious && !window.toolActive; onTriggered: window.previous() }
    Action { id: nextAction; text: "Next"; enabled: context.hasNext && !window.toolActive; onTriggered: window.next() }
    Action {
        id: previousPageAction
        text: "Prev Page"
        enabled: window.activeView.page > 0 && !window.toolActive
        onTriggered: window.leaveImage(() => window.activeView.page = window.activeView.page - 1)
    }
    Action {
        id: nextPageAction
        text: "Next Page"
        enabled: window.activeView.page + 1 < window.activeView.pageCount && !window.toolActive
        onTriggered: window.leaveImage(() => window.activeView.page = window.activeView.page + 1)
    }
    Action {
        id: cropAction
        text: "&Crop..."
        shortcut: "X"
        enabled: window.canEdit && !window.cropping
        onTriggered: window.startTool(() => viewerPage.startCrop())
    }
    Action {
        id: straightenAction
        text: "&Straighten..."
        shortcut: "S"
        enabled: window.canEdit && !window.straightening
        onTriggered: window.startTool(() => viewerPage.startStraighten())
    }
    Action {
        id: adjustAction
        text: "&Adjust Colours..."
        shortcut: "A"
        enabled: window.canEdit && !window.adjusting
        onTriggered: window.startTool(() => viewerPage.startAdjust())
    }
    Action {
        id: beforeAfterAction
        text: "&Before and After"
        shortcut: "B"
        checkable: true
        checked: viewerPage.comparing === "edit"
        enabled: window.viewing && (viewerPage.canCompareEdit || viewerPage.comparing === "edit")
        onTriggered: viewerPage.toggleBeforeAfter()
    }
    Action {
        id: compareAction
        text: "Compare T&wo Images"
        checkable: true
        checked: viewerPage.comparing === "files"
        enabled: viewerPage.comparing === "files" || (window.compareTarget !== "" && !window.toolActive)
        onTriggered: {
            if (viewerPage.comparing === "files") {
                viewerPage.stopCompare()
                return
            }
            const other = window.compareTarget
            window.startTool(() => viewerPage.compareWith(other), false)
        }
    }
    // Holding O, or the toolbar's Original, shows the picture before editing.
    KeyHold {
        id: originalKey
        window: window
        key: Qt.Key_O
        enabled: window.viewing && !window.typing
    }
    Binding {
        target: viewerPage
        property: "holdingOriginal"
        value: originalKey.held || originalButton.pressed
    }
    Action {
        id: resizeAction
        text: "&Resize..."
        shortcut: "Ctrl+R"
        enabled: window.canEdit && !window.toolActive
        onTriggered: window.startTool(() => resizeDialog.open())
    }
    // Gray by the weights JPEG uses for Y, or by the lightness of Oklab with no chroma.
    Action {
        id: grayLumaAction
        text: "&Luma, as in JPEG"
        enabled: window.canEdit && !window.toolActive
        onTriggered: window.startTool(() => viewerPage.view.grayscale(0))
    }
    Action {
        id: grayLightnessAction
        text: "Perceptual &Lightness (Oklab)"
        enabled: window.canEdit && !window.toolActive
        onTriggered: window.startTool(() => viewerPage.view.grayscale(1))
    }
    Action {
        id: probeAction
        text: "&Probe Colours"
        shortcut: "P"
        checkable: true
        checked: window.probing
        enabled: window.viewing && !window.cropping && viewerPage.view.status === ImageView.Ready
        onTriggered: window.probing ? viewerPage.stopProbe() : viewerPage.startProbe()
    }
    Shortcut { sequences: [StandardKey.Copy]; enabled: window.probing; onActivated: viewerPage.copyProbedHex() }
    Action {
        id: undoAction
        text: "&Undo"
        shortcut: StandardKey.Undo
        enabled: window.viewing && !window.cropping && !window.straightening && !window.adjusting && viewerPage.view.canUndo
        onTriggered: viewerPage.view.undo()
    }
    Action {
        id: redoAction
        text: "&Redo"
        shortcut: StandardKey.Redo
        enabled: window.viewing && !window.cropping && !window.straightening && !window.adjusting && viewerPage.view.canRedo
        onTriggered: viewerPage.view.redo()
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

    // Space pans while cropping, with the left button.
    Shortcut { sequences: ["Right", "Space"]; enabled: !window.typing && !window.toolActive; onActivated: window.next() }
    Shortcut { sequences: ["Left", "Backspace"]; enabled: !window.typing && !window.toolActive; onActivated: window.previous() }
    Shortcut { sequence: "PgDown"; enabled: !window.typing && !window.toolActive; onActivated: window.viewing ? window.next() : browserPage.movePage(1) }
    Shortcut { sequence: "PgUp"; enabled: !window.typing && !window.toolActive; onActivated: window.viewing ? window.previous() : browserPage.movePage(-1) }
    Shortcut { sequence: "Shift+PgDown"; enabled: !window.viewing && !window.typing; onActivated: browserPage.extendSelectionByPage(1) }
    Shortcut { sequence: "Shift+PgUp"; enabled: !window.viewing && !window.typing; onActivated: browserPage.extendSelectionByPage(-1) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: window.cropping && !viewerPage.cropPanelEditing; onActivated: viewerPage.applyCrop() }
    Shortcut { sequences: ["Return", "Enter"]; enabled: window.straightening && !viewerPage.straightenPanelEditing; onActivated: viewerPage.applyStraighten() }
    Shortcut { sequences: ["Return", "Enter"]; enabled: window.adjusting; onActivated: viewerPage.applyAdjust() }
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

        AppMenu {
            title: "&File"
            AppMenuItem { action: openAction }
            AppMenuItem { action: fileActions.saveAction }
            AppMenuItem { action: fileActions.saveAsAction }
            MenuSeparator {}
            AppMenuItem { action: fileActions.renameAction }
            AppMenuItem { action: fileActions.trashAction }
            AppMenuItem { action: fileActions.deleteAction }
            AppMenuItem { action: fileActions.copyToAction }
            AppMenuItem { action: fileActions.moveToAction }
            MenuSeparator {}
            AppMenuItem { action: fileActions.newFolderAction }
            AppMenuItem { action: fileActions.openFolderAction }
            MenuSeparator {}
            AppMenuItem { action: quitAction }
        }
        AppMenu {
            title: "&Edit"
            AppMenuItem { action: undoAction }
            AppMenuItem { action: redoAction }
            MenuSeparator {}
            AppMenuItem { action: cropAction }
            AppMenuItem { action: straightenAction }
            AppMenuItem { action: resizeAction }
            AppMenu {
                title: "&Grayscale"
                enabled: window.canEdit && !window.toolActive
                AppMenuItem { action: grayLumaAction }
                AppMenuItem { action: grayLightnessAction }
            }
            AppMenuItem { action: adjustAction }
            AppMenuItem { action: probeAction }
            MenuSeparator {}
            AppMenuItem { action: fileActions.copyAction }
            AppMenuItem { action: fileActions.pasteAction }
            AppMenuItem { action: fileActions.copyPathAction }
            MenuSeparator {}
            JpegMenu { actions: fileActions; enabled: fileActions.idle && fileActions.jpegTargets }
        }
        AppMenu {
            title: "&View"
            AppMenuItem { action: modeAction }
            AppMenuItem { action: fileActions.refreshAction }
            AppMenu {
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
            AppMenuItem { action: beforeAfterAction }
            AppMenuItem { action: compareAction }
            MenuSeparator {}
            AppMenuItem { action: previousAction }
            AppMenuItem { action: nextAction }
            MenuSeparator {}
            AppMenuItem { action: previousPageAction }
            AppMenuItem { action: nextPageAction }
            MenuSeparator {}
            AppMenuItem { action: zoomInAction }
            AppMenuItem { action: zoomOutAction }
            AppMenuItem { action: fitAction }
            AppMenuItem { action: actualSizeAction }
            AppMenuItem { action: autoContrastAction }
            AppMenuItem { action: sharpResamplingAction }
            MenuSeparator {}
            AppMenuItem { action: colorManagedAction }
            AppMenuItem { text: "Display &Profile: " + ColorSettings.displayProfileName + "..."; onTriggered: displayProfileDialog.open() }
            AppMenuItem { text: "Use s&RGB Display"; enabled: ColorSettings.hasDisplayProfile; onTriggered: ColorSettings.useSrgbDisplay() }
            MenuSeparator {}
            AppMenuItem { text: "Clear &Thumbnail Cache..."; onTriggered: ThumbnailStore.measure() }
            MenuSeparator {}
            AppMenuItem { action: infoAction }
            AppMenuItem { action: animatePanelAction }
            MenuSeparator {}
            AppMenuItem { action: fullScreenAction }
        }
        AppMenu {
            title: "&Go"
            AppMenuItem { action: backAction }
            AppMenuItem { action: forwardAction }
            AppMenuItem { action: upAction }
            MenuSeparator {}
            AppMenuItem { action: editPathAction }
        }
        AppMenu {
            title: "&Help"
            AppMenuItem { text: "&About"; onTriggered: aboutDialog.open() }
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
            ToolSeparator { visible: window.viewing }
            ToolButton { action: cropAction; visible: window.viewing }
            ToolButton { action: straightenAction; visible: window.viewing }
            ToolButton { action: adjustAction; text: "Adjust"; visible: window.viewing }
            ToolSeparator { visible: window.viewing }
            ToolButton { action: beforeAfterAction; text: "Before/After"; visible: window.viewing }
            // Shows the original while held down.
            ToolButton {
                id: originalButton
                objectName: "originalButton"
                text: "Original"
                visible: window.viewing
                enabled: viewerPage.canCompareEdit || viewerPage.comparing === "edit"
                focusPolicy: Qt.NoFocus
            }
            ToolButton { action: probeAction; text: "Probe"; visible: window.viewing }
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
            sharpResampling: viewerPage.sharpResampling
            onViewRequested: window.viewing = true
            onFileMenuRequested: fileActions.fileMenu.popup()
            onFolderMenuRequested: (path) => fileActions.showFolderMenu(path)
        }

        ViewerPage {
            id: viewerPage
            context: context
            active: window.viewing
            fullScreen: window.visibility === Window.FullScreen
            guard: window.leaveImage
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
                profileProblemDialog.text = problem
                profileProblemDialog.open()
            }
        }
    }

    MessageBox {
        id: profileProblemDialog
        kind: "error"
        title: "Cannot Use This Profile"
        standardButtons: Dialog.Ok
    }

    FileDialog {
        id: openDialog
        title: "Open Image"
        nameFilters: ["Images (*.jpg *.jpeg *.jpe *.jfif *.tif *.tiff *.png *.webp *.avif *.jxl)", "All files (*)"]
        onAccepted: {
            const file = selectedFile
            window.leaveImage(() => context.openUrl(file))
        }
    }

    // Asks first with what there is, as clearing makes every folder slow to show once more.
    Connections {
        target: ThumbnailStore
        function onMeasured(available, count, bytes) {
            if (!available) {
                clearCacheResult.kind = "error"
                clearCacheResult.text = "The thumbnail cache cannot be opened, perhaps because another copy of the program is using it."
                clearCacheResult.open()
            } else if (count === 0) {
                clearCacheResult.kind = "info"
                clearCacheResult.text = "There are no stored thumbnails."
                clearCacheResult.open()
            } else {
                clearCacheDialog.text = "Remove all " + count.toLocaleString(Qt.locale(), "f", 0) + " stored thumbnails ("
                        + context.dataSizeText(bytes) + ")? They are made again when their folders are shown."
                clearCacheDialog.open()
            }
        }
        function onCleared(done) {
            if (!done) {
                clearCacheResult.kind = "error"
                clearCacheResult.text = "The thumbnails could not be removed."
                clearCacheResult.open()
            }
        }
    }

    MessageBox {
        id: clearCacheDialog
        objectName: "clearCacheDialog"
        kind: "question"
        title: "Clear Thumbnail Cache"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: ThumbnailStore.clear()
    }

    MessageBox {
        id: clearCacheResult
        objectName: "clearCacheResult"
        title: "Clear Thumbnail Cache"
        standardButtons: Dialog.Ok
    }

    MessageBox {
        id: aboutDialog
        kind: "info"
        title: "About Image Viewer"
        text: "A fast, multiplatform image viewer and basic editor."
        standardButtons: Dialog.Ok
    }
}

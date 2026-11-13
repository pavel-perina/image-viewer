import QtCore
import QtQuick
import QtQuick.Controls
import ImageViewer

// The large image view. Loads only while active, so it costs nothing while browsing.
Rectangle {
    id: page

    required property BrowseContext context
    required property bool active
    // At full screen the panel follows the mouse at the right edge of the screen. Otherwise
    // it is toggled, and panelPinned holds that choice.
    required property bool fullScreen
    // Runs what would leave an edited image once its changes are saved or given up.
    property var guard: (proceed) => proceed()
    readonly property alias view: surface.view
    property alias panelPinned: settings.panelPinned
    property alias panelAnimated: settings.panelAnimated
    property alias sharpResampling: settings.sharpResampling
    property bool panelRevealed: false
    readonly property bool panelShown: fullScreen ? panelRevealed : panelPinned
    // The tool at work, one at a time: "crop", "probe", "straighten", "adjust" or "annotate",
    // or none. Its panel takes the place of the info panel, at full screen too.
    property string tool: ""
    readonly property bool toolActive: tool !== ""
    readonly property bool cropping: tool === "crop"
    readonly property bool probing: tool === "probe"
    readonly property bool straightening: tool === "straighten"
    readonly property bool adjusting: tool === "adjust"
    readonly property bool annotating: tool === "annotate"
    // The panel at the right edge: the tool's, or the info panel with no tool.
    readonly property Item sidePanel: cropping ? cropPanel : probing ? probePanel : straightening ? straightenPanel
                                    : adjusting ? adjustPanel : annotating ? annotatePanel : panel
    readonly property bool toolPanelShown: fullScreen ? panelRevealed : true
    // Whether Enter applies the tool at work. The probe has nothing to apply, and a field of
    // the tool's panel that has the keys takes Enter for its own value; Adjust has no fields.
    readonly property bool enterApplies: toolActive && !probing && (adjusting || !sidePanel.editing)
    // Comparing two pictures of the same size, one each side of a line: "edit" for before and
    // after editing, "files" for the image shown against another file. Not a tool, so it goes
    // with Adjust Colours.
    property string comparing: ""
    // Shows the picture before editing alone while it is true, as while holding a key.
    property bool holdingOriginal: false
    property bool comparingForHold: false  // started by holding, so letting go ends it
    property string compareNotice: ""
    // Before is the picture as it was before the colour preview while adjusting, otherwise the
    // file without edits.
    readonly property bool canCompareEdit: view.status === ImageView.Ready && (view.modified || adjusting)
                                           && !cropping && !straightening && !annotating

    function startBeforeAfter() {
        if (!canCompareEdit)
            return false
        stopProbe()
        otherSurface.view.showPictureOf(view, !adjusting)
        compareSplitter.firstLabel = "Before"
        compareSplitter.secondLabel = "After"
        comparing = "edit"
        return true
    }

    function toggleBeforeAfter() {
        if (comparing === "edit")
            stopCompare()
        else
            startBeforeAfter()
    }

    // The image shown against another file, which has to be the same size.
    function compareWith(path) {
        if (view.status !== ImageView.Ready || path === "" || path === view.source)
            return
        if (!adjusting)
            leaveTool()  // comparing goes with Adjust Colours, and with no other tool
        otherSurface.source = path
        compareSplitter.firstLabel = path.substring(path.lastIndexOf("/") + 1)
        compareSplitter.secondLabel = page.context.currentName
        comparing = "files"
    }

    function stopCompare() {
        comparing = ""
        comparingForHold = false
        otherSurface.source = ""
    }

    onHoldingOriginalChanged: {
        if (holdingOriginal && comparing === "")
            comparingForHold = startBeforeAfter()
        else if (!holdingOriginal && comparingForHold)
            stopCompare()
    }

    // Pictures of different sizes cannot be lined up, which shows only once both have loaded.
    function checkCompareSizes() {
        const a = surface.view
        const b = otherSurface.view
        if (comparing === "" || a.status === ImageView.Loading || b.status === ImageView.Loading)
            return
        if (a.status !== ImageView.Ready || b.status !== ImageView.Ready) {
            stopCompare()
        } else if (a.imageWidth !== b.imageWidth || a.imageHeight !== b.imageHeight) {
            compareNotice = comparing === "edit" ? "The edited picture differs in size from the original, so they cannot be compared."
                                                 : "The pictures differ in size, so they cannot be compared."
            stopCompare()
        }
    }

    // Ends the tool at work, if any, without applying it. Starting a tool leaves the one
    // before it this way, so there is never more than one.
    function leaveTool() {
        cancelAdjust()
        cancelAnnotate()
        tool = ""
    }

    // No tool, if it was this one.
    function drop(name) {
        if (tool === name)
            tool = ""
    }

    function startAdjust() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        leaveTool()
        tool = "adjust"
        adjustPanel.start()
    }

    function applyAdjust() {
        drop("adjust")
        if (adjustPanel.changes)
            adjustPanel.apply()
        else
            view.endColorPreview()
    }

    function cancelAdjust() {
        if (adjusting && comparing === "edit")
            stopCompare()  // before and after would be the same
        drop("adjust")
        view.endColorPreview()
    }

    function startStraighten() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        stopCompare()
        leaveTool()
        tool = "straighten"
        straightenPanel.start()
        straightenOverlay.forceActiveFocus()
    }

    function applyStraighten() {
        const p = straightenPanel
        drop("straighten")
        if (p.changes)
            view.straighten(p.roll, p.pitch, p.yaw, p.focal, straightenOverlay.keepRatio)
    }

    function cancelStraighten() {
        drop("straighten")
    }

    function startProbe() {
        if (view.status !== ImageView.Ready)
            return
        leaveTool()
        tool = "probe"
        probeArea.forceActiveFocus()
    }

    function stopProbe() {
        drop("probe")
    }

    function copyProbedHex() {
        if (probePanel.shown)
            probePanel.copy(probePanel.shown.hex)
    }

    function startCrop() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        stopCompare()
        leaveTool()
        tool = "crop"
        cropPanel.start()
        cropOverlay.forceActiveFocus()
    }

    function applyCrop() {
        const rect = cropOverlay.cropRect
        drop("crop")
        view.crop(rect)
    }

    function cancelCrop() {
        drop("crop")
    }

    function startAnnotate() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        stopCompare()
        leaveTool()
        tool = "annotate"
        view.prepareDraw()
        annotateOverlay.forceActiveFocus()
    }

    function applyAnnotate() {
        drop("annotate")
        view.endDraw()
        annotateOverlay.apply()
    }

    // Leaves without drawing anything into the picture.
    function cancelAnnotate() {
        drop("annotate")
        view.endDraw()
        annotateOverlay.clear()
    }

    // Applies the tool at work, as Enter does.
    function applyTool() {
        if (cropping)
            applyCrop()
        else if (straightening)
            applyStraighten()
        else if (adjusting)
            applyAdjust()
        else if (annotating)
            applyAnnotate()
    }

    // What Esc does on this page, one step at a time: a tool that edits is given up, then a
    // comparison ends, then the probe. False when there was nothing to leave.
    function stepBack() {
        if (annotating)
            escapeAnnotate()
        else if (toolActive && !probing)
            leaveTool()
        else if (comparing !== "")
            stopCompare()
        else if (probing)
            stopProbe()
        else
            return false
        return true
    }

    // Esc lets go of the shape selected first, and leaves only with none.
    function escapeAnnotate() {
        if (annotateOverlay.selected >= 0)
            annotateOverlay.deselect()
        else
            cancelAnnotate()
    }

    signal fileMenuRequested()

    color: Theme.imageBackground

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: page.fileMenuRequested()
    }

    // The other picture of a comparison, under the image and shown left of the line.
    ImageSurface {
        id: otherSurface
        objectName: "otherSurface"
        anchors.fill: surface
        visible: page.comparing !== ""
        scrollBarsVisible: false
        wheelNavigates: false
        sharp: settings.sharpResampling
    }

    ImageSurface {
        id: surface
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // The panel takes room from the image, except at full screen where it comes and goes
        // with the mouse, and refitting the image each time would be restless.
        anchors.right: page.fullScreen ? parent.right : page.sidePanel.left
        source: page.active ? page.context.currentPath : ""
        histogramEnabled: (page.panelShown && !page.toolActive) || page.adjusting
        sharp: settings.sharpResampling
        wheelNavigates: !page.toolActive && page.comparing === ""
    }

    CompareSplitter {
        id: compareSplitter
        objectName: "compareSplitter"
        anchors.fill: surface
        first: otherSurface
        second: surface
        active: page.comparing !== "" && otherSurface.view.status === ImageView.Ready
                && surface.view.status === ImageView.Ready
                && otherSurface.view.imageWidth === surface.view.imageWidth
                && otherSurface.view.imageHeight === surface.view.imageHeight
        firstOnly: page.holdingOriginal
    }

    Connections {
        target: surface.view
        function onStatusChanged() { page.checkCompareSizes() }
    }
    Connections {
        target: otherSurface.view
        function onStatusChanged() { page.checkCompareSizes() }
    }

    Label {
        objectName: "compareNotice"
        anchors.horizontalCenter: surface.horizontalCenter
        anchors.top: surface.top
        anchors.topMargin: 12
        visible: page.compareNotice !== ""
        text: page.compareNotice
        color: Theme.onImageBright
        padding: 8
        background: Rectangle { color: Theme.bubble; radius: 4 }
        Timer {
            running: page.compareNotice !== ""
            interval: 4000
            onTriggered: page.compareNotice = ""
        }
    }

    ProbeOverlay {
        id: probeArea
        objectName: "probeArea"
        anchors.fill: surface
        view: surface.view
        style: overlayStyle
        visible: page.probing
        enabled: page.probing
        onPixelChanged: probePanel.probed = pixel
        onPicked: (pixel) => probePanel.remember(pixel)
    }

    OverlayStyle {
        id: overlayStyle
        line: Theme.toolLine
        outline: Theme.toolOutline
        dim: Theme.toolDim
        faint: Theme.toolFaint
        surround: Theme.imageBackground
    }

    CropOverlay {
        id: cropOverlay
        objectName: "cropOverlay"
        anchors.fill: surface
        view: surface.view
        style: overlayStyle
        visible: page.cropping
        enabled: page.cropping
    }

    StraightenOverlay {
        id: straightenOverlay
        objectName: "straightenOverlay"
        anchors.fill: surface
        view: surface.view
        style: overlayStyle
        verticalColor: Theme.guideVertical
        horizontalColor: Theme.guideHorizontal
        visible: page.straightening
        enabled: page.straightening
    }

    AnnotateOverlay {
        id: annotateOverlay
        objectName: "annotateOverlay"
        anchors.fill: surface
        view: surface.view
        style: overlayStyle
        visible: page.annotating
        enabled: page.annotating
    }

    // Leaving the image ends any tool. An edit that changes it ends all but the probe, which
    // reads whatever is shown.
    Connections {
        target: surface.view
        function onSourceChanged() {
            // The view has dropped its colour preview with the picture.
            page.cancelAnnotate()
            page.tool = ""
            page.stopCompare()
        }
        function onStatusChanged() {
            if (surface.view.status !== ImageView.Ready) {
                // Adjust that was applied has left already, and its preview stays up until
                // the edited picture takes its place: only one still at work is cancelled.
                if (page.adjusting)
                    page.cancelAdjust()
                page.cancelAnnotate()
                if (!page.probing)
                    page.tool = ""
                probeArea.clear()  // its pixel may be elsewhere in what comes
            }
        }
    }
    onActiveChanged: if (!active) { leaveTool(); stopCompare() }

    Connections {
        target: page.context
        function onFilesEdited(paths) { surface.reloadIfAmong(paths) }
    }

    Connections {
        target: surface.view
        function onNavigateRequested(steps) {
            const last = page.context.fileCount - 1
            const index = Math.max(0, Math.min(last, page.context.currentIndex + steps))
            if (index !== page.context.currentIndex)
                page.guard(() => page.context.setCurrentIndex(index))
        }
    }

    Settings {
        id: settings
        category: "InfoPanel"
        property bool panelPinned: false
        property bool panelAnimated: true
        property bool sharpResampling: true
        property alias logScale: panel.logScale
        property alias fitRange: panel.fitRange
    }

    readonly property int edgeMargin: 12

    // Passive, so it does not take the mouse away from panning and zooming in the view.
    HoverHandler {
        onPointChanged: {
            const x = point.position.x
            if (x >= page.width - page.edgeMargin)
                page.panelRevealed = true
            else if (x < page.width - page.sidePanel.width - page.edgeMargin)
                page.panelRevealed = false
        }
    }

    onFullScreenChanged: panelRevealed = false

    InfoPanel {
        id: panel
        view: surface.view
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.panelShown && !page.toolActive ? page.width - width : page.width
        visible: x < page.width

        Behavior on x {
            enabled: page.panelAnimated
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    CropPanel {
        id: cropPanel
        objectName: "cropPanel"
        overlay: cropOverlay
        view: surface.view
        revealed: page.cropping && page.toolPanelShown
        animated: page.panelAnimated
        onApplyRequested: page.applyCrop()
        onCancelRequested: page.cancelCrop()
        onDoneEditing: cropOverlay.forceActiveFocus()
    }

    AnnotatePanel {
        id: annotatePanel
        objectName: "annotatePanel"
        overlay: annotateOverlay
        revealed: page.annotating && page.toolPanelShown
        animated: page.panelAnimated
        onApplyRequested: page.applyAnnotate()
        onCancelRequested: page.cancelAnnotate()
        onDoneEditing: annotateOverlay.forceActiveFocus()
    }

    StraightenPanel {
        id: straightenPanel
        objectName: "straightenPanel"
        overlay: straightenOverlay
        view: surface.view
        revealed: page.straightening && page.toolPanelShown
        animated: page.panelAnimated
        onApplyRequested: page.applyStraighten()
        onCancelRequested: page.cancelStraighten()
        onDoneEditing: straightenOverlay.forceActiveFocus()
    }

    AdjustPanel {
        id: adjustPanel
        objectName: "adjustPanel"
        view: surface.view
        active: page.adjusting
        revealed: page.adjusting && page.toolPanelShown
        animated: page.panelAnimated
        onApplyRequested: page.applyAdjust()
        onCancelRequested: page.cancelAdjust()
    }

    ProbePanel {
        id: probePanel
        objectName: "probePanel"
        context: page.context
        revealed: page.probing && page.toolPanelShown
        animated: page.panelAnimated
    }

    // Large images without reduced versions take a moment. Quick switches between ordinary
    // images finish before this delay, so they do not flicker.
    property bool loadingIsSlow: false

    Timer {
        interval: 300
        running: surface.view.status === ImageView.Loading
        onRunningChanged: if (running) page.loadingIsSlow = false
        onTriggered: page.loadingIsSlow = true
    }

    BusyIndicator {
        anchors.centerIn: surface
        running: page.loadingIsSlow && surface.view.status === ImageView.Loading
    }

    Label {
        anchors.centerIn: surface
        color: Theme.onImage
        visible: surface.view.status === ImageView.Error
        text: "Cannot open " + page.context.currentName + ": " + surface.view.errorString
    }
}

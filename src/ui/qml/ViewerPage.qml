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
    // Choosing a crop: its panel takes the place of the info panel, at full screen too.
    property bool cropping: false
    readonly property bool cropPanelShown: cropping && (fullScreen ? panelRevealed : true)
    readonly property bool cropPanelEditing: cropPanel.editing
    // Reading pixels: the probe panel takes the same place.
    property bool probing: false
    readonly property bool probePanelShown: probing && (fullScreen ? panelRevealed : true)
    // Straightening: its panel takes the same place.
    property bool straightening: false
    readonly property bool straightenPanelShown: straightening && (fullScreen ? panelRevealed : true)
    readonly property bool straightenPanelEditing: straightenPanel.editing
    readonly property bool toolActive: cropping || probing || straightening

    function startStraighten() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        probing = false
        cropping = false
        straightening = true
        straightenPanel.start()
        straightenOverlay.forceActiveFocus()
    }

    function applyStraighten() {
        const p = straightenPanel
        straightening = false
        if (p.changes)
            view.straighten(p.roll, p.pitch, p.yaw, p.focal, straightenOverlay.keepRatio)
    }

    function cancelStraighten() {
        straightening = false
    }

    function startProbe() {
        if (view.status !== ImageView.Ready)
            return
        cropping = false
        straightening = false
        probing = true
        probeArea.forceActiveFocus()
    }

    function stopProbe() {
        probing = false
    }

    function copyProbedHex() {
        if (probePanel.shown)
            probePanel.copy(probePanel.shown.hex)
    }

    function startCrop() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
        probing = false
        straightening = false
        cropping = true
        cropPanel.start()
        cropOverlay.forceActiveFocus()
    }

    function applyCrop() {
        const rect = cropOverlay.cropRect
        cropping = false
        view.crop(rect)
    }

    function cancelCrop() {
        cropping = false
    }

    signal fileMenuRequested()

    color: "#202020"

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: page.fileMenuRequested()
    }

    ImageSurface {
        id: surface
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // The panel takes room from the image, except at full screen where it comes and goes
        // with the mouse, and refitting the image each time would be restless.
        anchors.right: page.fullScreen ? parent.right : page.cropping ? cropPanel.left : page.probing ? probePanel.left
                     : page.straightening ? straightenPanel.left : panel.left
        source: page.active ? page.context.currentPath : ""
        histogramEnabled: page.panelShown && !page.toolActive
        sharp: settings.sharpResampling
        wheelNavigates: !page.toolActive
    }

    // Reads the pixel under a press and while dragging; arrow keys step by a pixel. The right or
    // middle button pans and the wheel goes on to the view.
    Item {
        id: probeArea
        objectName: "probeArea"
        anchors.fill: surface
        visible: page.probing
        enabled: page.probing

        property point requested: Qt.point(-1, -1)  // the pixel asked for last, as shown
        property bool rememberWhenRead: false
        property int viewRevision: 0  // for the marker to follow zooming and panning

        function probeImagePoint(point) {
            requested = Qt.point(Math.floor(point.x), Math.floor(point.y))
            surface.view.probe(point)
        }

        function probeAt(x, y) {
            probeImagePoint(surface.view.itemToImage(mapToItem(surface.view, x, y)))
        }

        // Once the pixel asked for has been read.
        function rememberIfRead() {
            const read = probePanel.probed
            if (rememberWhenRead && read && read.x === requested.x && read.y === requested.y) {
                rememberWhenRead = false
                probePanel.remember(read)
            }
        }

        Connections {
            target: surface.view
            function onZoomChanged() { probeArea.viewRevision++ }
            function onProbed(pixel) {
                probePanel.probed = pixel
                probeArea.rememberIfRead()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            cursorShape: panning ? Qt.ClosedHandCursor : Qt.CrossCursor
            property bool panning: false
            property point last
            onPressed: (mouse) => {
                probeArea.forceActiveFocus()
                last = Qt.point(mouse.x, mouse.y)
                panning = mouse.button !== Qt.LeftButton
                if (!panning)
                    probeArea.probeAt(mouse.x, mouse.y)
            }
            onPositionChanged: (mouse) => {
                if (panning) {
                    surface.view.panBy(mouse.x - last.x, mouse.y - last.y)
                    last = Qt.point(mouse.x, mouse.y)
                } else {
                    probeArea.probeAt(mouse.x, mouse.y)
                }
            }
            onReleased: {
                if (!panning) {
                    probeArea.rememberWhenRead = true
                    probeArea.rememberIfRead()
                }
                panning = false
            }
            onWheel: (wheel) => wheel.accepted = false
        }

        Keys.onPressed: (event) => {
            const read = probePanel.probed
            const steps = { [Qt.Key_Left]: [-1, 0], [Qt.Key_Right]: [1, 0], [Qt.Key_Up]: [0, -1], [Qt.Key_Down]: [0, 1] }
            const step = steps[event.key]
            if (step && read) {
                probeArea.probeImagePoint(Qt.point(read.x + step[0] + 0.5, read.y + step[1] + 0.5))
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && read) {
                probePanel.remember(read)
                event.accepted = true
            }
        }

        // Around the pixel read, never smaller than a few screen pixels.
        Item {
            id: marker
            readonly property var pixel: probePanel.probed
            readonly property rect bounds: {
                probeArea.viewRevision
                if (!pixel)
                    return Qt.rect(0, 0, 0, 0)
                const a = probeArea.mapFromItem(surface.view, surface.view.imageToItem(Qt.point(pixel.x, pixel.y)))
                const b = probeArea.mapFromItem(surface.view, surface.view.imageToItem(Qt.point(pixel.x + 1, pixel.y + 1)))
                const size = Math.max(7, b.x - a.x)
                return Qt.rect((a.x + b.x - size) / 2, (a.y + b.y - size) / 2, size, size)
            }
            visible: !!pixel
            x: bounds.x - 1
            y: bounds.y - 1
            width: bounds.width + 2
            height: bounds.height + 2
            Rectangle { anchors.fill: parent; color: "transparent"; border.color: "black" }
            Rectangle { anchors.fill: parent; anchors.margins: 1; color: "transparent"; border.color: "white" }
        }
    }

    CropOverlay {
        id: cropOverlay
        objectName: "cropOverlay"
        anchors.fill: surface
        view: surface.view
        visible: page.cropping
        enabled: page.cropping
    }

    StraightenOverlay {
        id: straightenOverlay
        objectName: "straightenOverlay"
        anchors.fill: surface
        view: surface.view
        visible: page.straightening
        enabled: page.straightening
    }

    // Leaving the image, or an edit that changes it, ends choosing a crop.
    Connections {
        target: surface.view
        function onSourceChanged() { page.cropping = false; page.probing = false; page.straightening = false }
        function onStatusChanged() {
            if (surface.view.status !== ImageView.Ready) {
                page.cropping = false
                page.straightening = false
                probePanel.probed = null  // its pixel may be elsewhere in what comes
            }
        }
    }
    onActiveChanged: if (!active) { cropping = false; probing = false; straightening = false }

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
        property bool sharpResampling: false
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
            else if (x < page.width - (page.cropping ? cropPanel.width : page.probing ? probePanel.width
                                                  : page.straightening ? straightenPanel.width : panel.width) - page.edgeMargin)
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
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.cropPanelShown ? page.width - width : page.width
        visible: x < page.width
        onApplyRequested: page.applyCrop()
        onCancelRequested: page.cancelCrop()
        onDoneEditing: cropOverlay.forceActiveFocus()

        Behavior on x {
            enabled: page.panelAnimated
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    StraightenPanel {
        id: straightenPanel
        objectName: "straightenPanel"
        overlay: straightenOverlay
        view: surface.view
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.straightenPanelShown ? page.width - width : page.width
        visible: x < page.width
        onApplyRequested: page.applyStraighten()
        onCancelRequested: page.cancelStraighten()
        onDoneEditing: straightenOverlay.forceActiveFocus()

        Behavior on x {
            enabled: page.panelAnimated
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
    }

    ProbePanel {
        id: probePanel
        objectName: "probePanel"
        context: page.context
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.probePanelShown ? page.width - width : page.width
        visible: x < page.width

        Behavior on x {
            enabled: page.panelAnimated
            NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
        }
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
        color: "#e0e0e0"
        visible: surface.view.status === ImageView.Error
        text: "Cannot open " + page.context.currentName + ": " + surface.view.errorString
    }
}

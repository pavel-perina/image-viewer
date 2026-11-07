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

    function startCrop() {
        if (!view.editable || view.status !== ImageView.Ready)
            return
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
        anchors.right: page.fullScreen ? parent.right : page.cropping ? cropPanel.left : panel.left
        source: page.active ? page.context.currentPath : ""
        histogramEnabled: page.panelShown && !page.cropping
        sharp: settings.sharpResampling
        wheelNavigates: !page.cropping
    }

    CropOverlay {
        id: cropOverlay
        objectName: "cropOverlay"
        anchors.fill: surface
        view: surface.view
        visible: page.cropping
        enabled: page.cropping
    }

    // Leaving the image, or an edit that changes it, ends choosing a crop.
    Connections {
        target: surface.view
        function onSourceChanged() { page.cropping = false }
        function onStatusChanged() { if (surface.view.status !== ImageView.Ready) page.cropping = false }
    }
    onActiveChanged: if (!active) cropping = false

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
            else if (x < page.width - (page.cropping ? cropPanel.width : panel.width) - page.edgeMargin)
                page.panelRevealed = false
        }
    }

    onFullScreenChanged: panelRevealed = false

    InfoPanel {
        id: panel
        view: surface.view
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.panelShown && !page.cropping ? page.width - width : page.width
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

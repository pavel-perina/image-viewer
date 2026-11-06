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
    readonly property alias view: view
    property alias panelPinned: settings.panelPinned
    property alias panelAnimated: settings.panelAnimated
    property alias sharpResampling: settings.sharpResampling
    property bool panelRevealed: false
    readonly property bool panelShown: fullScreen ? panelRevealed : panelPinned

    signal fileMenuRequested()

    color: "#202020"

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: page.fileMenuRequested()
    }

    ImageView {
        id: view
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // The panel takes room from the image, except at full screen where it comes and goes
        // with the mouse, and refitting the image each time would be restless.
        anchors.right: page.fullScreen ? parent.right : panel.left
        clip: true
        source: page.active ? page.context.currentPath : ""
        histogramEnabled: page.panelShown
        resampling: settings.sharpResampling
    }

    Loader {
        anchors.fill: view
        active: view.resampleActive
        sourceComponent: ResampledView { view: page.view }
    }

    ImageScrollBars {
        objectName: "imageScrollBars"
        anchors.fill: view
        view: page.view
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
            else if (x < page.width - panel.width - page.edgeMargin)
                page.panelRevealed = false
        }
    }

    onFullScreenChanged: panelRevealed = false

    InfoPanel {
        id: panel
        view: view
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: page.panelShown ? page.width - width : page.width
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
        running: view.status === ImageView.Loading
        onRunningChanged: if (running) page.loadingIsSlow = false
        onTriggered: page.loadingIsSlow = true
    }

    BusyIndicator {
        anchors.centerIn: view
        running: page.loadingIsSlow && view.status === ImageView.Loading
    }

    Label {
        anchors.centerIn: view
        color: "#e0e0e0"
        visible: view.status === ImageView.Error
        text: "Cannot open " + page.context.currentName + ": " + view.errorString
    }
}

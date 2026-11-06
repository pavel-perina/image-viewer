import QtQuick
import QtQuick.Controls
import ImageViewer

// The large image view. Loads only while active, so it costs nothing while browsing.
Rectangle {
    id: page

    required property BrowseContext context
    required property bool active
    readonly property alias view: view

    color: "#202020"

    ImageView {
        id: view
        anchors.fill: parent
        clip: true
        source: page.active ? page.context.currentPath : ""
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
        anchors.centerIn: parent
        running: page.loadingIsSlow && view.status === ImageView.Loading
    }

    Label {
        anchors.centerIn: parent
        color: "#e0e0e0"
        visible: view.status === ImageView.Error
        text: "Cannot open " + page.context.currentName + ": " + view.errorString
    }
}

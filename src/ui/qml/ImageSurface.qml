import QtQuick
import ImageViewer

// An image as the user sees it, wherever it is shown: the view, its position handles and sharp
// resampling.
Item {
    id: surface

    property alias source: imageView.source
    property alias histogramEnabled: imageView.histogramEnabled
    property bool sharp: false
    readonly property alias view: imageView

    ImageView {
        id: imageView
        anchors.fill: parent
        clip: true
        resampling: surface.sharp
    }

    Loader {
        anchors.fill: imageView
        active: imageView.resampleActive
        sourceComponent: ResampledView { view: imageView }
    }

    ImageScrollBars {
        objectName: "imageScrollBars"
        anchors.fill: imageView
        view: imageView
    }
}

import QtQuick
import QtQuick.Window
import ImageViewer

// An image as the user sees it, wherever it is shown: the view, its position handles and sharp
// resampling.
Item {
    id: surface

    property alias source: imageView.source
    property alias histogramEnabled: imageView.histogramEnabled
    property alias wheelNavigates: imageView.wheelNavigates
    property bool sharp: true
    readonly property alias view: imageView
    // The software renderer has no shaders, so it shows the picture unadjusted while adjusting.
    readonly property bool colorPreviewShown: imageView.colorPreviewActive && GraphicsInfo.api !== GraphicsInfo.Software
    // Comparing: the picture is drawn and takes the mouse only right of this line, in item
    // coordinates, while the view keeps its whole size so it zooms and scrolls as before.
    property real clipLeft: 0
    property bool scrollBarsVisible: true

    // Called with files that changed on disk.
    function reloadIfAmong(paths) {
        if (imageView.source !== "" && paths.indexOf(imageView.source) >= 0)
            imageView.reload()
    }

    Item {
        id: clipper
        x: surface.clipLeft
        width: Math.max(0, surface.width - x)
        height: surface.height
        clip: surface.clipLeft > 0

        Item {
            x: -clipper.x
            width: surface.width
            height: surface.height

            // The picture, which a colour preview draws through its shader.
            Item {
                id: content
                anchors.fill: parent

                ImageView {
                    id: imageView
                    anchors.fill: parent
                    clip: true
                    resampling: surface.sharp
                    keepAutoContrast: ViewSettings.keepAutoContrast
                    backdropFirst: ViewSettings.backdropFirst
                    backdropSecond: ViewSettings.backdropSecond
                }

                Loader {
                    anchors.fill: imageView
                    active: imageView.resampleActive
                    sourceComponent: ResampledView {
                        view: imageView
                        colorPreview: surface.colorPreviewShown
                    }
                }
            }

            // Always there rather than loaded, as its source would keep the picture hidden until the
            // garbage collector came for it. Sharp resampling draws its own, before the kernel,
            // and dithers at the end. Images with more than 8 bits pass through it to be dithered
            // when nothing is adjusted.
            ColorPreviewEffect {
                objectName: "colorPreview"
                anchors.fill: content
                visible: (surface.colorPreviewShown || (imageView.highPrecision && GraphicsInfo.api !== GraphicsInfo.Software))
                         && !imageView.resampleActive
                adjusting: surface.colorPreviewShown
                view: imageView
                sourceItem: content
                textureSize: Qt.size(Math.ceil(content.width * Screen.devicePixelRatio),
                                     Math.ceil(content.height * Screen.devicePixelRatio))
            }
        }
    }

    // Not made at all where they are not wanted.
    Loader {
        anchors.fill: parent
        active: surface.scrollBarsVisible
        sourceComponent: ImageScrollBars {
            objectName: "imageScrollBars"
            view: imageView
        }
    }
}

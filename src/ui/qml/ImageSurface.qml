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
    property bool sharp: false
    readonly property alias view: imageView
    // The software renderer has no shaders, so it shows the picture unadjusted while adjusting.
    readonly property bool colorPreviewShown: imageView.colorPreviewActive && GraphicsInfo.api !== GraphicsInfo.Software

    // Called with files that changed on disk.
    function reloadIfAmong(paths) {
        if (imageView.source !== "" && paths.indexOf(imageView.source) >= 0)
            imageView.reload()
    }

    // The picture, which a colour preview draws through its shader.
    Item {
        id: content
        anchors.fill: parent

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
    }

    // Always there rather than loaded, as its source would keep the picture hidden until the
    // garbage collector came for it.
    ShaderEffect {
        objectName: "colorPreview"
        anchors.fill: content
        visible: surface.colorPreviewShown
        fragmentShader: "qrc:/shaders/color_preview.frag.qsb"
        property var source: ShaderEffectSource {
            sourceItem: content
            hideSource: surface.colorPreviewShown
            live: surface.colorPreviewShown
            smooth: false
            textureSize: Qt.size(Math.ceil(content.width * Screen.devicePixelRatio),
                                 Math.ceil(content.height * Screen.devicePixelRatio))
        }
        property var lut: lutTexture
        property real cubeSize: imageView.colorPreviewCubeSize
        property real chroma: imageView.colorPreviewChroma
        property real useTone: imageView.colorPreviewTone ? 1 : 0
        property real useLightness: imageView.colorPreviewLightness ? 1 : 0
        property real gamutMap: imageView.colorPreviewGamutMap ? 1 : 0
        property vector4d domainMin: imageView.colorPreviewDomainMin
        property vector4d domainMax: imageView.colorPreviewDomainMax

        ColorLutTexture {
            id: lutTexture
            image: imageView.colorPreviewLut
        }
    }

    ImageScrollBars {
        objectName: "imageScrollBars"
        anchors.fill: content
        view: imageView
    }
}

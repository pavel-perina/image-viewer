import QtQuick
import ImageViewer

// Draws an ImageView through Magic Kernel Sharp 2013. The view renders its tiles into a texture
// at the resolution of its level, which is what lets the kernel reach across tile edges, and
// two passes take that to the screen.
ShaderEffect {
    id: vertical

    required property ImageView view
    property bool colorPreview: false  // the view's colour adjustment, drawn before the kernel

    readonly property size composite: view.resampleTextureSize
    readonly property point phasePoint: view.resamplePhase
    readonly property rect bounds: view.resampleBounds
    readonly property real deviceWidth: width * view.resampleDevicePixelRatio
    readonly property real deviceHeight: height * view.resampleDevicePixelRatio

    fragmentShader: "qrc:/shaders/mks_vertical.frag.qsb"
    property var source: ShaderEffectSource {
        sourceItem: horizontal
        hideSource: true
        smooth: false
        format: ShaderEffectSource.RGBA16F
        textureSize: Qt.size(vertical.deviceWidth, vertical.composite.height)
    }
    property real destSize: deviceHeight
    property real texSize: composite.height
    property real ratio: view.resampleRatio
    property real phase: phasePoint.y
    property real spacing: view.resampleSpacing
    property real ditherStep: ViewSettings.ditherStep
    property real boundLow: bounds.y
    property real boundHigh: bounds.y + bounds.height
    property vector4d imageRect: Qt.vector4d(view.resampleImageRect.x, view.resampleImageRect.y,
                                             view.resampleImageRect.x + view.resampleImageRect.width,
                                             view.resampleImageRect.y + view.resampleImageRect.height)

    // The level's pixels adjusted one for one, at the size the kernel reads them.
    ColorPreviewEffect {
        id: colored
        anchors.fill: parent
        visible: vertical.colorPreview
        view: vertical.view
        sourceItem: vertical.view
        textureSize: vertical.composite
        ditherStep: 0  // the kernel follows
    }

    ShaderEffect {
        id: horizontal
        anchors.fill: parent
        blending: false
        fragmentShader: "qrc:/shaders/mks_horizontal.frag.qsb"
        // Shown again by hand when the effect goes, as the garbage collector may take its time
        // with this source and the view would stay hidden until then.
        Component.onDestruction: source.hideSource = false
        property var source: ShaderEffectSource {
            sourceItem: vertical.colorPreview ? colored : vertical.view
            hideSource: true
            smooth: false
            format: ShaderEffectSource.RGBA16F
            textureSize: vertical.composite
        }
        property real destSize: vertical.deviceWidth
        property real texSize: vertical.composite.width
        property real ratio: vertical.ratio
        property real phase: vertical.phasePoint.x
        property real spacing: vertical.spacing
        property real boundLow: vertical.bounds.x
        property real boundHigh: vertical.bounds.x + vertical.bounds.width
    }
}

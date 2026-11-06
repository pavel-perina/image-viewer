import QtQuick
import ImageViewer

// Draws an ImageView through Magic Kernel Sharp 2013. The view renders its tiles into a texture
// at the resolution of its level, which is what lets the kernel reach across tile edges, and
// two passes take that to the screen.
ShaderEffect {
    id: vertical

    required property ImageView view

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
    property real boundLow: bounds.y
    property real boundHigh: bounds.y + bounds.height
    property vector4d imageRect: Qt.vector4d(view.resampleImageRect.x, view.resampleImageRect.y,
                                             view.resampleImageRect.x + view.resampleImageRect.width,
                                             view.resampleImageRect.y + view.resampleImageRect.height)

    ShaderEffect {
        id: horizontal
        anchors.fill: parent
        blending: false
        fragmentShader: "qrc:/shaders/mks_horizontal.frag.qsb"
        property var source: ShaderEffectSource {
            sourceItem: vertical.view
            hideSource: true
            smooth: false
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

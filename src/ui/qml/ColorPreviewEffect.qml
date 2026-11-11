import QtQuick
import ImageViewer

// A colour adjustment drawn by the shader over what an item shows, with the view's settings.
// Before sharp resampling when that is on, so that the kernel sharpens adjusted colours rather
// than the adjustment stretching its halos; at full precision either way.
ShaderEffect {
    id: effect

    required property ImageView view
    required property Item sourceItem
    property size textureSize: Qt.size(width, height)

    fragmentShader: "qrc:/shaders/color_preview.frag.qsb"
    property var source: ShaderEffectSource {
        sourceItem: effect.sourceItem
        hideSource: effect.visible
        live: effect.visible
        smooth: false
        format: ShaderEffectSource.RGBA16F
        textureSize: effect.textureSize
    }
    property var lut: lutTexture
    property real cubeSize: view.colorPreviewCubeSize
    property real chroma: view.colorPreviewChroma
    property real useTone: view.colorPreviewTone ? 1 : 0
    property real useLightness: view.colorPreviewLightness ? 1 : 0
    property real gamutMap: view.colorPreviewGamutMap ? 1 : 0
    property vector4d domainMin: view.colorPreviewDomainMin
    property vector4d domainMax: view.colorPreviewDomainMax

    ColorLutTexture {
        id: lutTexture
        image: effect.view.colorPreviewLut
    }
}

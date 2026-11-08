pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Levels, lightness, saturation and a colour table from a .cube file, shown over the picture by
// the view's shader while they change and applied as one edit. A gray picture takes the levels
// and lightness only.
Rectangle {
    id: panel

    required property ImageView view
    // While the tool is open; only then do changes show over the picture.
    property bool active: false

    signal applyRequested()
    signal cancelRequested()

    // Levels from 0 to 255 of the samples as stored, whatever their depth: the input range that
    // becomes the output range, which can turn black gray or white darker.
    property real black: 0
    property real white: 255
    property real gamma: 1
    property real outBlack: 0
    property real outWhite: 255
    // On Oklab lightness, in percent of its scale.
    property real brightness: 0
    property real contrast: 100
    property real midtones: 1
    property real saturation: 100  // percent
    // Kept between images, as it is a way of working rather than part of one picture's look.
    property bool gamutMap: true
    property string message: ""

    readonly property var settings: ({
        black: black / 255, white: white / 255, gamma: gamma, outBlack: outBlack / 255, outWhite: outWhite / 255,
        brightness: brightness / 100, contrast: contrast / 100, lightness: midtones, saturation: saturation / 100,
        gamutMap: gamutMap
    })
    readonly property bool changes: black !== 0 || white !== 255 || gamma !== 1 || outBlack !== 0 || outWhite !== 255
                                    || brightness !== 0 || contrast !== 100 || midtones !== 1
                                    || (!view.grayImage && (saturation !== 100 || view.colorCubeName !== ""))

    width: 280
    color: "#e6181818"

    function start() {
        view.clearColorCube()
        reset()
    }

    function reset() {
        black = 0
        white = 255
        gamma = 1
        outBlack = 0
        outWhite = 255
        brightness = 0
        contrast = 100
        midtones = 1
        saturation = 100
        message = ""
        view.clearColorCube()
        preview()
    }

    function preview() {
        if (active)
            view.previewColors(settings)
    }

    function apply() {
        view.adjustColors(settings)
    }

    onSettingsChanged: preview()

    // Made on first use, and kept so that it opens where it was left.
    Loader {
        id: cubeDialog
        active: false
        sourceComponent: FileChooser {
            title: "Load a Colour Table"
            types: [{ label: "Colour tables", extensions: ["cube"] }, { label: "All files", extensions: [] }]
            onChosen: (path) => {
                panel.message = panel.view.loadColorCube(urlOf(path))
                panel.preview()
            }
        }
        function start() {
            active = true
            item.start("", "")
        }
    }

    // Keeps the mouse from reaching the image underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: "Adjust Colours"
            font.bold: true
            font.pointSize: Qt.application.font.pointSize + 1
        }

        // One grid, so the sliders of both groups line up.
        GridLayout {
            columns: 3
            columnSpacing: 8
            Layout.fillWidth: true

            Label { text: "Levels"; color: "#a0a0a0"; Layout.columnSpan: 3 }
            LevelsControl {
                objectName: "levels"
                Layout.columnSpan: 3
                Layout.fillWidth: true
                histogram: panel.view.histogram
                black: panel.black
                white: panel.white
                gamma: panel.gamma
                outBlack: panel.outBlack
                outWhite: panel.outWhite
                onInputPicked: (black, gamma, white) => {
                    panel.black = black
                    panel.gamma = gamma
                    panel.white = white
                }
                onOutputPicked: (outBlack, outWhite) => {
                    panel.outBlack = outBlack
                    panel.outWhite = outWhite
                }
            }

            Label { text: "In Oklab, keeping the colours"; color: "#a0a0a0"; Layout.columnSpan: 3; Layout.topMargin: 6 }
            Label { text: "Brightness" }
            Slider {
                objectName: "adjustBrightness"
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                from: -50
                to: 50
                stepSize: 1
                value: panel.brightness
                onMoved: panel.brightness = value
            }
            ValueLabel { text: (panel.brightness > 0 ? "+" : "") + Math.round(panel.brightness) }

            Label { text: "Contrast" }
            Slider {
                objectName: "adjustContrast"
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                from: 0
                to: 200
                stepSize: 1
                value: panel.contrast
                onMoved: panel.contrast = value
            }
            ValueLabel { text: Math.round(panel.contrast) + "%" }

            Label { text: "Midtones" }
            PowerSlider {
                objectName: "adjustMidtones"
                power: panel.midtones
                onPicked: (v) => panel.midtones = v
            }
            ValueLabel { text: panel.midtones.toFixed(2) }

            Label { text: "Saturation"; enabled: !panel.view.grayImage }
            Slider {
                objectName: "adjustSaturation"
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                enabled: !panel.view.grayImage
                from: 0
                to: 200
                stepSize: 1
                value: panel.saturation
                onMoved: panel.saturation = value
            }
            ValueLabel {
                text: Math.round(panel.saturation) + "%"
                enabled: !panel.view.grayImage
            }
        }

        CheckBox {
            objectName: "adjustGamutMap"
            focusPolicy: Qt.NoFocus
            enabled: !panel.view.grayImage
            text: "Fit strong colours by chroma"
            checked: panel.gamutMap
            onToggled: panel.gamutMap = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: "Colours pushed beyond what the image can hold lose saturation at the same hue and "
                          + "lightness. Off, they are clipped per channel, which shifts hue and flattens detail."
        }

        Label { text: "Colour table"; color: "#a0a0a0" }
        RowLayout {
            Layout.fillWidth: true
            enabled: !panel.view.grayImage
            Button {
                objectName: "adjustLoadCube"
                focusPolicy: Qt.NoFocus
                text: "Load .cube..."
                onClicked: cubeDialog.start()
            }
            Button {
                focusPolicy: Qt.NoFocus
                text: "Clear"
                enabled: panel.view.colorCubeName !== ""
                onClicked: {
                    panel.view.clearColorCube()
                    panel.preview()
                }
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: "#a0a0a0"
            text: panel.view.grayImage ? "A gray picture takes levels and lightness only."
                                       : panel.view.colorCubeName !== "" ? panel.view.colorCubeName : "None"
        }
        Label {
            visible: panel.message !== ""
            text: panel.message
            color: "#e0a050"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            Button {
                focusPolicy: Qt.NoFocus
                text: "Reset"
                onClicked: panel.reset()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                onClicked: panel.cancelRequested()
            }
            Button {
                objectName: "adjustApply"
                text: "Apply"
                highlighted: true
                enabled: panel.changes
                onClicked: panel.applyRequested()
            }
        }
    }

    // An exponent from 1/4 to 4, even steps either side of 1.
    component PowerSlider: Slider {
        id: slider
        property real power: 1
        signal picked(real value)
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
        from: -2
        to: 2
        value: Math.log2(power)
        onMoved: slider.picked(Math.round(Math.pow(2, value) * 100) / 100)
    }

    component ValueLabel: Label {
        Layout.preferredWidth: 36
        horizontalAlignment: Text.AlignRight
    }
}

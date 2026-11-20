pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// White balance, levels, lightness, saturation and a colour table from a .cube file, shown over
// the picture by the view's shader while they change and applied as one edit. The sections are
// stacked in the order they apply; each can be collapsed, reset alone, or switched off while
// keeping its values. A gray picture takes the levels and lightness only.
ToolPanel {
    id: panel

    title: "Adjust Colours"
    applyName: "adjustApply"
    applyEnabled: panel.changes
    extraText: "Reset"
    onExtraRequested: panel.reset()

    required property ImageView view
    // While the tool is open; only then do changes show over the picture.
    property bool active: false

    // White balance: the temperature of the light in kelvins and its tint, 6500 and 0 leaving
    // the picture as it is. The slider moves evenly in mireds, a million over kelvins, as the eye
    // sees about the same change for each.
    property real temperature: 6500
    property real tint: 0
    readonly property real coolest: 15000
    readonly property real warmest: 2000
    // Picking something neutral on the picture, and the patch a click takes, in pixels.
    property bool picking: false
    property int patchSize: kept.patchSize
    property string pickMessage: ""

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

    // Sections switched off keep their values but are left out.
    property bool whiteOn: true
    property bool levelsOn: true
    property bool oklabOn: true
    property bool lookOn: true

    readonly property bool whiteInUse: !view.grayImage && (temperature !== 6500 || tint !== 0)
    readonly property bool levelsInUse: black !== 0 || white !== 255 || gamma !== 1 || outBlack !== 0 || outWhite !== 255
    readonly property bool oklabInUse: brightness !== 0 || contrast !== 100 || midtones !== 1
                                       || (!view.grayImage && saturation !== 100)
    readonly property bool lookInUse: !view.grayImage && view.colorCubeName !== ""

    // A section switched off gives the values that leave the picture alone.
    readonly property var settings: ({
        temperature: whiteOn ? temperature : 6500, tint: whiteOn ? tint : 0,
        black: levelsOn ? black / 255 : 0, white: levelsOn ? white / 255 : 1, gamma: levelsOn ? gamma : 1,
        outBlack: levelsOn ? outBlack / 255 : 0, outWhite: levelsOn ? outWhite / 255 : 1,
        brightness: oklabOn ? brightness / 100 : 0, contrast: oklabOn ? contrast / 100 : 1,
        lightness: oklabOn ? midtones : 1, saturation: oklabOn ? saturation / 100 : 1,
        gamutMap: gamutMap, cube: lookOn
    })
    readonly property bool changes: (whiteOn && whiteInUse) || (levelsOn && levelsInUse) || (oklabOn && oklabInUse)
                                    || (lookOn && lookInUse)

    // Where the slider is, from 0 at the warm end to 1 at the cool one, even in mireds; and back,
    // to 50 K, which is finer than the eye tells apart at any temperature.
    function positionOf(kelvin) {
        return (1e6 / warmest - 1e6 / kelvin) / (1e6 / warmest - 1e6 / coolest)
    }
    function kelvinAt(position) {
        const mired = 1e6 / warmest - position * (1e6 / warmest - 1e6 / coolest)
        return Math.round(1e6 / mired / 50) * 50
    }

    function start() {
        view.clearColorCube()
        reset()
    }

    function reset() {
        resetWhite()
        resetLevels()
        resetOklab()
        resetLook()
        whiteOn = true
        levelsOn = true
        oklabOn = true
        lookOn = true
        picking = false
    }

    function resetWhite() {
        temperature = 6500
        tint = 0
        pickMessage = ""
    }

    function resetLevels() {
        black = 0
        white = 255
        gamma = 1
        outBlack = 0
        outWhite = 255
    }

    function resetOklab() {
        brightness = 0
        contrast = 100
        midtones = 1
        saturation = 100
    }

    function resetLook() {
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
    onActiveChanged: if (!active) picking = false

    Connections {
        target: panel.view
        function onGrayPicked(result) {
            if (!panel.picking)
                return
            if (result.error) {
                panel.pickMessage = result.error
                return
            }
            panel.temperature = Math.round(result.temperature)
            panel.tint = Math.round(result.tint * 10) / 10
            panel.whiteOn = true
            panel.pickMessage = result.clamped ? "Beyond what white balance reaches: set as near as it goes." : ""
        }
    }

    // Kept as a way of working: which sections are folded, and the size of the patch.
    Settings {
        id: kept
        category: "Adjust"
        property bool whiteCollapsed: false
        property bool levelsCollapsed: false
        property bool oklabCollapsed: false
        property bool lookCollapsed: false
        property int patchSize: 5
    }

    // The chooser of a colour table: made on first use, and kept so that it opens where it was
    // left. It is made by hand, so that it is not one of the rows of the panel.
    property FileChooser cubeDialog: null

    function chooseCube() {
        if (!cubeDialog)
            cubeDialog = cubeChooser.createObject(panel)
        cubeDialog.start("", "")
    }

    Component {
        id: cubeChooser
        FileChooser {
            title: "Load a Colour Table"
            types: [{ label: "Colour tables", extensions: ["cube"] }, { label: "All files", extensions: [] }]
            onChosen: (path) => {
                panel.message = panel.view.loadColorCube(urlOf(path))
                panel.preview()
            }
        }
    }

    ToolSection {
        objectName: "adjustWhite"
        title: "White balance"
        enabled: !panel.view.grayImage
        collapsed: kept.whiteCollapsed
        onCollapseToggled: kept.whiteCollapsed = !kept.whiteCollapsed
        inUse: panel.whiteInUse
        switchable: true
        on: panel.whiteOn
        switchTip: "Off leaves white balance out and keeps its values"
        onSwitched: (on) => panel.whiteOn = on
        onResetRequested: panel.resetWhite()

        GridLayout {
            columns: 3
            columnSpacing: 8
            Layout.fillWidth: true

            Label { text: "Temperature" }
            Slider {
                objectName: "adjustTemperature"
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                from: 0
                to: 1
                value: panel.positionOf(panel.temperature)
                onMoved: panel.temperature = panel.kelvinAt(value)
            }
            ValueLabel {
                Layout.preferredWidth: 56
                text: Math.round(panel.temperature) + " K"
            }

            Label { text: "Tint" }
            Slider {
                objectName: "adjustTint"
                focusPolicy: Qt.NoFocus
                Layout.fillWidth: true
                from: -100
                to: 100
                stepSize: 1
                value: panel.tint
                onMoved: panel.tint = value
            }
            ValueLabel {
                Layout.preferredWidth: 56
                text: (panel.tint > 0 ? "+" : "") + Math.round(panel.tint)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "adjustPickGray"
                focusPolicy: Qt.NoFocus
                text: "Pick Gray"
                checkable: true
                checked: panel.picking
                onToggled: {
                    panel.picking = checked
                    panel.pickMessage = ""
                }
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: "Click something that should be neutral, or drag a rectangle over it"
            }
            ComboBox {
                objectName: "adjustPatchSize"
                focusPolicy: Qt.NoFocus
                model: ["1 px", "5 px", "11 px", "21 px"]
                readonly property var sizes: [1, 5, 11, 21]
                currentIndex: Math.max(0, sizes.indexOf(panel.patchSize))
                onActivated: (index) => kept.patchSize = sizes[index]
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            objectName: "adjustPickMessage"
            visible: panel.pickMessage !== ""
            text: panel.pickMessage
            color: Theme.warning
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    ToolSection {
        objectName: "adjustLevels"
        title: "Levels"
        collapsed: kept.levelsCollapsed
        onCollapseToggled: kept.levelsCollapsed = !kept.levelsCollapsed
        inUse: panel.levelsInUse
        switchable: true
        on: panel.levelsOn
        switchTip: "Off leaves levels out and keeps their values"
        onSwitched: (on) => panel.levelsOn = on
        onResetRequested: panel.resetLevels()

        LevelsControl {
            objectName: "levels"
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
    }

    ToolSection {
        objectName: "adjustOklab"
        title: "Lightness and colour, in Oklab"
        collapsed: kept.oklabCollapsed
        onCollapseToggled: kept.oklabCollapsed = !kept.oklabCollapsed
        inUse: panel.oklabInUse
        switchable: true
        on: panel.oklabOn
        switchTip: "Off leaves these out and keeps their values"
        onSwitched: (on) => panel.oklabOn = on
        onResetRequested: panel.resetOklab()

        GridLayout {
            columns: 3
            columnSpacing: 8
            Layout.fillWidth: true

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
            ToolTip.text: "Colours pushed beyond what the image can hold, here or by white balance, lose saturation "
                          + "and keep their lightness. Off, they are clipped per channel, which shifts hue and "
                          + "flattens detail."
        }
    }

    ToolSection {
        objectName: "adjustLook"
        title: "Colour table"
        enabled: !panel.view.grayImage
        collapsed: kept.lookCollapsed
        onCollapseToggled: kept.lookCollapsed = !kept.lookCollapsed
        inUse: panel.lookInUse
        switchable: true
        on: panel.lookOn
        switchTip: "Off leaves the table out and keeps it loaded"
        onSwitched: (on) => panel.lookOn = on
        onResetRequested: panel.resetLook()

        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "adjustLoadCube"
                focusPolicy: Qt.NoFocus
                text: "Load .cube..."
                onClicked: panel.chooseCube()
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: Theme.textMuted
            text: panel.view.colorCubeName !== "" ? panel.view.colorCubeName : "None"
        }
        Label {
            visible: panel.message !== ""
            text: panel.message
            color: Theme.warning
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Label {
        visible: panel.view.grayImage
        text: "A gray picture takes levels and lightness only."
        color: Theme.textMuted
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
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

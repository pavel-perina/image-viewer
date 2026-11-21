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
    property real vibrance: 0
    property bool protectSkin: true
    property real cubeStrength: 100

    readonly property bool whiteInUse: !view.grayImage && (temperature !== 6500 || tint !== 0)
    readonly property bool levelsInUse: black !== 0 || white !== 255 || gamma !== 1 || outBlack !== 0 || outWhite !== 255
    readonly property bool oklabInUse: brightness !== 0 || contrast !== 100 || midtones !== 1
                                       || (!view.grayImage && (saturation !== 100 || vibrance !== 0))
    readonly property bool lookInUse: !view.grayImage && view.colorCubeName !== "" && cubeStrength > 0

    // A section switched off gives the values that leave the picture alone.
    readonly property var settings: ({
        temperature: whiteOn ? temperature : 6500, tint: whiteOn ? tint : 0,
        black: levelsOn ? black / 255 : 0, white: levelsOn ? white / 255 : 1, gamma: levelsOn ? gamma : 1,
        outBlack: levelsOn ? outBlack / 255 : 0, outWhite: levelsOn ? outWhite / 255 : 1,
        brightness: oklabOn ? brightness / 100 : 0, contrast: oklabOn ? contrast / 100 : 1,
        lightness: oklabOn ? midtones : 1, saturation: oklabOn ? saturation / 100 : 1,
        vibrance: oklabOn ? vibrance / 100 : 0, protectSkin: protectSkin,
        gamutMap: gamutMap, cube: lookOn, cubeStrength: cubeStrength / 100
    })
    readonly property bool changes: (whiteOn && whiteInUse) || (levelsOn && levelsInUse) || (oklabOn && oklabInUse)
                                    || (lookOn && lookInUse)

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
        vibrance = 0
        protectSkin = true
    }

    function resetLook() {
        message = ""
        cubeStrength = 100
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
        available: !panel.view.grayImage
        unavailableText: "Not for gray pictures"
        collapsed: kept.whiteCollapsed
        onCollapseToggled: kept.whiteCollapsed = !kept.whiteCollapsed
        inUse: panel.whiteInUse
        switchable: true
        on: panel.whiteOn
        switchTip: "Off leaves white balance out and keeps its values"
        onSwitched: (on) => panel.whiteOn = on
        onResetRequested: panel.resetWhite()

        // Kelvins grow to the right, as in other programs, and the slider moves evenly in mireds.
        SliderRow {
            objectName: "adjustTemperature"
            label: "Temperature (K)"
            value: panel.temperature
            defaultValue: 6500
            from: 2000
            to: 15000
            stepSize: 50
            kind: SliderScale.Reciprocal
            onModified: (value) => panel.temperature = value
            onDoneEditing: panel.doneEditing()
        }
        SliderRow {
            objectName: "adjustTint"
            label: "Tint"
            value: panel.tint
            from: -100
            to: 100
            decimals: 1
            onModified: (value) => panel.tint = value
            onDoneEditing: panel.doneEditing()
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                objectName: "adjustPickGray"
                focusPolicy: Qt.NoFocus
                text: "Pick Gray"
                // Shown pressed while picking, by the panel's state alone: a checkable button
                // would set its own state and lose the binding, and then look pressed after a
                // reset had stopped the picking.
                checked: panel.picking
                onClicked: {
                    panel.picking = !panel.picking
                    panel.pickMessage = ""
                }
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: panel.picking ? "Stop picking (Esc)"
                                            : "Click something that should be neutral, or drag a rectangle over it; click again or press Esc to stop"
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
        title: "Lightness and colour"
        collapsed: kept.oklabCollapsed
        onCollapseToggled: kept.oklabCollapsed = !kept.oklabCollapsed
        inUse: panel.oklabInUse
        switchable: true
        on: panel.oklabOn
        switchTip: "Off leaves these out and keeps their values"
        onSwitched: (on) => panel.oklabOn = on
        onResetRequested: panel.resetOklab()

        SliderRow {
            objectName: "adjustBrightness"
            label: "Brightness"
            value: panel.brightness
            from: -50
            to: 50
            onModified: (value) => panel.brightness = value
            onDoneEditing: panel.doneEditing()
        }
        SliderRow {
            objectName: "adjustContrast"
            label: "Contrast (%)"
            value: panel.contrast
            defaultValue: 100
            from: 0
            to: 200
            onModified: (value) => panel.contrast = value
            onDoneEditing: panel.doneEditing()
        }
        // An exponent from 1/4 to 4, even steps either side of 1.
        SliderRow {
            objectName: "adjustMidtones"
            label: "Midtones"
            value: panel.midtones
            defaultValue: 1
            from: 0.25
            to: 4
            stepSize: 0.01
            decimals: 2
            kind: SliderScale.Logarithmic
            onModified: (value) => panel.midtones = value
            onDoneEditing: panel.doneEditing()
        }
        SliderRow {
            objectName: "adjustSaturation"
            label: "Saturation (%)"
            enabled: !panel.view.grayImage
            value: panel.saturation
            defaultValue: 100
            from: 0
            to: 200
            onModified: (value) => panel.saturation = value
            onDoneEditing: panel.doneEditing()
        }

        // Livelier dull colours and the strong ones left alone, so nothing is pushed out of range;
        // lowering tames the strong ones most.
        SliderRow {
            objectName: "adjustVibrance"
            label: "Vibrance"
            enabled: !panel.view.grayImage
            value: panel.vibrance
            from: -100
            to: 100
            onModified: (value) => panel.vibrance = value
            onDoneEditing: panel.doneEditing()
        }
        CheckBox {
            objectName: "adjustProtectSkin"
            focusPolicy: Qt.NoFocus
            enabled: !panel.view.grayImage && panel.vibrance !== 0
            text: "Protect skin tones"
            checked: panel.protectSkin
            onToggled: panel.protectSkin = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: "Vibrance acts at a third on the hues and dull colours of skin, light or dark. Off "
                          + "for pictures without people, where it would hold back wood, brick and autumn leaves."
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
        available: !panel.view.grayImage
        unavailableText: "Not for gray pictures"
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
        // Looks are often made strong, to be toned down: the table's result mixed with the colour
        // before it.
        SliderRow {
            objectName: "adjustCubeStrength"
            label: "Strength (%)"
            enabled: panel.view.colorCubeName !== ""
            value: panel.cubeStrength
            defaultValue: 100
            from: 0
            to: 100
            onModified: (value) => panel.cubeStrength = value
            onDoneEditing: panel.doneEditing()
        }
        Label {
            visible: panel.message !== ""
            text: panel.message
            color: Theme.warning
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}

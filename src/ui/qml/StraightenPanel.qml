pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Straightening as if the camera had been turned: the angles come from lines drawn or found on
// the image, and can be typed instead. Applying keeps the largest rectangle with no empty corners,
// or, with cropping off, the whole turned picture with transparent corners.
ToolPanel {
    id: panel

    title: "Straighten"
    applyEnabled: panel.changes && panel.resultSize.width > 0
    extraText: "Reset"
    onExtraRequested: panel.reset()

    required property StraightenOverlay overlay
    required property ImageView view

    property real roll: 0
    property real pitch: 0
    property real yaw: 0
    // The lines were found rather than drawn, so the ones that disagree are left out.
    property bool detected: false
    // The reduced copy for finding lines and the preview has been read.
    property bool ready: false
    property string message: ""
    // From the camera data of the image being straightened, read when the tool starts, as the
    // view has no signal for it.
    property real cameraFocal: 0
    readonly property real focal: {
        const typed = parseFloat(focalField.text)
        return typed > 0 ? typed : 0
    }
    readonly property bool changes: roll !== 0 || pitch !== 0 || yaw !== 0
    readonly property size resultSize: changes ? view.straightenedSize(roll, pitch, yaw, focal, settings.keepRatio, settings.crop)
                                               : Qt.size(view.imageWidth, view.imageHeight)

    Settings {
        id: settings
        category: "Straighten"
        property int mode: 2
        property bool keepRatio: true
        property bool crop: true
    }

    Binding { target: panel.overlay; property: "roll"; value: panel.roll }
    Binding { target: panel.overlay; property: "pitch"; value: panel.pitch }
    Binding { target: panel.overlay; property: "yaw"; value: panel.yaw }
    Binding { target: panel.overlay; property: "focal35mm"; value: panel.focal }
    Binding { target: panel.overlay; property: "keepRatio"; value: settings.keepRatio }
    Binding { target: panel.overlay; property: "crop"; value: settings.crop }

    // A new image starts straight, with no lines.
    function start() {
        roll = 0
        pitch = 0
        yaw = 0
        detected = false
        ready = false
        message = ""
        cameraFocal = view.focalLength35mm()
        focalField.text = cameraFocal > 0 ? String(Math.round(cameraFocal)) : ""
        overlay.lines = []
        overlay.showResult = false
        view.prepareStraighten()
    }

    function reset() {
        roll = 0
        pitch = 0
        yaw = 0
        detected = false
        message = ""
        overlay.lines = []
    }

    // The angles from the lines, unless there are none, which leaves what was typed.
    function refit() {
        const lines = overlay.lines
        if (lines.length === 0)
            return
        const fit = view.fitStraightLines(lines, settings.mode, focal, detected)
        roll = Math.round(fit.roll * 100) / 100
        pitch = Math.round(fit.pitch * 100) / 100
        yaw = Math.round(fit.yaw * 100) / 100
        overlay.used = fit.used
    }

    // Found lines that disagree with the rest are dropped rather than shown, as they are mostly
    // edges that were never meant to be straight; drawing a line brings in what is missing.
    function detect() {
        const found = view.detectStraightLines()
        detected = true
        message = found.length === 0 ? "No long straight edges were found. Draw lines instead." : ""
        overlay.lines = found
        refit()
        const kept = found.filter((line, i) => overlay.used[i] !== false)
        if (kept.length < found.length) {
            overlay.lines = kept
            refit()
        }
    }

    Connections {
        target: panel.overlay
        function onLinesEdited() { panel.refit() }
    }
    Connections {
        target: panel.view
        function onStraightenReady() { panel.ready = true }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pointSize: Theme.smallSize
        text: "Draw lines along edges that should be vertical or horizontal: one levels the picture; "
              + "two of either kind also correct the perspective. Detect finds them. The right button, "
              + "or Space with the left one, pans; Delete removes the line chosen; V shows the result."
    }

    RowLayout {
        Layout.fillWidth: true
        Button {
            objectName: "straightenDetect"
            focusPolicy: Qt.NoFocus
            text: "Detect"
            enabled: panel.ready
            onClicked: panel.detect()
        }
        Button {
            focusPolicy: Qt.NoFocus
            text: "Remove Line"
            enabled: panel.overlay.selected >= 0
            onClicked: panel.overlay.removeSelected()
        }
        Item { Layout.fillWidth: true }
    }

    Label {
        visible: panel.message !== ""
        text: panel.message
        color: Theme.warning
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    Label { text: "Lines correct"; color: Theme.textMuted }
    ComboBox {
        objectName: "straightenMode"
        focusPolicy: Qt.NoFocus
        Layout.fillWidth: true
        model: ["Rotation only", "Rotation and verticals", "Rotation and perspective"]
        currentIndex: Math.max(0, Math.min(2, settings.mode))
        onActivated: (index) => {
            settings.mode = index
            if (index < 1)
                panel.pitch = 0
            if (index < 2)
                panel.yaw = 0
            panel.refit()
        }
    }

    Label { text: "Angles, in degrees"; color: Theme.textMuted }
    GridLayout {
        columns: 2
        columnSpacing: 10
        Layout.fillWidth: true
        Label { text: "Rotate" }
        AngleField { objectName: "straightenRoll"; value: panel.roll; onPicked: (v) => panel.roll = v }
        Label { text: "Vertical" }
        AngleField { objectName: "straightenPitch"; value: panel.pitch; onPicked: (v) => panel.pitch = v }
        Label { text: "Horizontal" }
        AngleField { objectName: "straightenYaw"; value: panel.yaw; onPicked: (v) => panel.yaw = v }
        Label { text: "Focal length" }
        TextField {
            id: focalField
            objectName: "straightenFocal"
            Layout.fillWidth: true
            placeholderText: "28 mm, assumed"
            validator: DoubleValidator { bottom: 1; top: 2000; decimals: 1; notation: DoubleValidator.StandardNotation; locale: "C" }
            selectByMouse: true
            onEditingFinished: panel.refit()
            onAccepted: panel.doneEditing()
        }
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pointSize: Theme.smallSize
        text: panel.cameraFocal > 0 ? "Focal length as on 35mm film, from the camera."
                                    : "The camera data has no focal length; it matters for perspective only."
    }

    // Off keeps the whole turned picture, its empty corners transparent.
    CheckBox {
        objectName: "straightenCrop"
        focusPolicy: Qt.NoFocus
        text: "Crop the empty corners"
        checked: settings.crop
        onToggled: settings.crop = checked
    }
    CheckBox {
        focusPolicy: Qt.NoFocus
        text: "Keep the image's ratio"
        checked: settings.keepRatio
        enabled: settings.crop
        onToggled: settings.keepRatio = checked
    }
    CheckBox {
        objectName: "straightenShowResult"
        focusPolicy: Qt.NoFocus
        text: "Show the result"
        checked: panel.overlay.showResult
        enabled: panel.ready
        onToggled: panel.overlay.showResult = checked
    }

    Label {
        objectName: "straightenResultSize"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        // A turn that puts part of the picture beyond the horizon has no whole picture to keep.
        text: panel.resultSize.width > 0 ? "Result " + Math.round(panel.resultSize.width) + " x " + Math.round(panel.resultSize.height) + " pixels"
                                         : "Part of the image would lie beyond the horizon"
        color: Theme.textMuted
    }

    // An angle to two decimals; typing one replaces what the lines gave.
    component AngleField: TextField {
        id: field
        property real value: 0
        signal picked(real value)
        Layout.fillWidth: true
        selectByMouse: true
        text: value.toFixed(2)
        validator: DoubleValidator { bottom: -45; top: 45; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
        onEditingFinished: {
            const typed = parseFloat(text)
            if (!isNaN(typed))
                field.picked(Math.max(-45, Math.min(45, typed)))
            text = Qt.binding(() => field.value.toFixed(2))
        }
        onAccepted: panel.doneEditing()
    }
}

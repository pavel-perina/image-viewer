import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// A slider whose track shows the colours it passes through, as stops of {colour, outside}
// from ColorChoice, with what sRGB cannot show hatched; and a number box beside it.
RowLayout {
    id: row

    required property string label
    property var stops: []
    property real value: 0
    property real from: 0
    property real to: 255
    property real stepSize: 1
    property int decimals: 0
    // The name the number box is found by; the slider is found by it with "Slider" added.
    property string boxName

    // The user set a value, by the slider or the box.
    signal modified(real value)

    spacing: Theme.panelSpacing

    Label {
        text: row.label
        Layout.preferredWidth: 14
    }
    Slider {
        id: slider
        objectName: row.boxName + "Slider"
        Layout.fillWidth: true
        from: row.from
        to: row.to
        stepSize: row.stepSize
        value: row.value
        onMoved: row.modified(value)
        background: ColorRamp {
            x: slider.leftPadding
            y: slider.topPadding + (slider.availableHeight - height) / 2
            implicitWidth: 160
            implicitHeight: 14
            width: slider.availableWidth
            height: 14
            radius: Theme.controlRadius
            stops: row.stops
            hatch: Theme.rampHatch
        }
    }
    DecimalSpinBox {
        objectName: row.boxName
        Layout.preferredWidth: 96
        decimals: row.decimals
        realFrom: row.from
        realTo: row.to
        realStepSize: row.stepSize
        realValue: row.value
        onRealValueModified: (value) => row.modified(value)
    }
}

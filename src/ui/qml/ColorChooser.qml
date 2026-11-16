import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Choosing a colour by R, G and B, or by lightness, chroma and hue, each a slider whose track
// shows what moving it gives, with a number box; by hex; or from the swatches. An item, to put
// in a panel or a dialog.
ColumnLayout {
    id: chooser

    // The colour chosen; set it to start from one.
    property alias colour: choice.colour
    // What it was before, shown beside it.
    property color original: choice.colour

    spacing: Theme.panelSpacing

    ColorChoice { id: choice }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.panelSpacing
        // Before and after, side by side.
        Row {
            Rectangle {
                width: 36; height: 28
                color: chooser.original
                border.width: 1; border.color: Theme.swatchFrame
            }
            Rectangle {
                objectName: "chooserNew"
                width: 36; height: 28
                color: choice.colour
                border.width: 1; border.color: Theme.swatchFrame
            }
        }
        Item { Layout.fillWidth: true }
        Label { text: "Hex" }
        TextField {
            objectName: "chooserHex"
            Layout.preferredWidth: 96
            text: choice.hex
            validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
            onEditingFinished: {
                choice.hex = text
                text = Qt.binding(() => choice.hex)
            }
        }
    }

    GradientSlider {
        Layout.fillWidth: true
        label: "R"; boxName: "chooserRed"
        stops: choice.ramps[ColorChoice.Red]
        value: choice.red
        onModified: (value) => choice.red = Math.round(value)
    }
    GradientSlider {
        Layout.fillWidth: true
        label: "G"; boxName: "chooserGreen"
        stops: choice.ramps[ColorChoice.Green]
        value: choice.green
        onModified: (value) => choice.green = Math.round(value)
    }
    GradientSlider {
        Layout.fillWidth: true
        label: "B"; boxName: "chooserBlue"
        stops: choice.ramps[ColorChoice.Blue]
        value: choice.blue
        onModified: (value) => choice.blue = Math.round(value)
    }

    Label {
        Layout.topMargin: Theme.panelSpacing
        text: "Lightness, chroma and hue, in Oklch"
        color: Theme.textMuted
    }
    GradientSlider {
        Layout.fillWidth: true
        label: "L"; boxName: "chooserLightness"
        stops: choice.ramps[ColorChoice.Lightness]
        to: 1; stepSize: 0.01; decimals: 3
        value: choice.lightness
        onModified: (value) => choice.lightness = value
    }
    GradientSlider {
        Layout.fillWidth: true
        label: "C"; boxName: "chooserChroma"
        stops: choice.ramps[ColorChoice.Chroma]
        to: choice.maxChroma; stepSize: 0.005; decimals: 3
        value: choice.chroma
        onModified: (value) => choice.chroma = value
    }
    GradientSlider {
        Layout.fillWidth: true
        label: "H"; boxName: "chooserHue"
        stops: choice.ramps[ColorChoice.Hue]
        to: 360; stepSize: 1; decimals: 0
        value: choice.hue
        onModified: (value) => choice.hue = value
    }
    Label {
        objectName: "chooserOutside"
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        // Kept in the layout when hidden, so the dialog does not change height.
        opacity: choice.outside ? 1 : 0
        color: Theme.warning
        text: "sRGB cannot show this; it has the most chroma that fits."
    }

    ColorSwatches {
        Layout.fillWidth: true
        current: choice.colour
        onPicked: (colour) => choice.colour = colour
    }
}

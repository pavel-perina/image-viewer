import QtQuick
import QtQuick.Controls
import ImageViewer

// A colour typed as hex, such as #c0c0c0, with a swatch of it at the start. A caret at the end
// will open the colour chooser; until there is one, it is not shown.
TextField {
    id: field

    property color colour: "white"
    // The user set a colour, by typing it.
    signal picked(color colour)

    leftPadding: swatch.width + 2 * Theme.controlPadding / 2 + 4
    text: field.colour.toString()
    validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
    onEditingFinished: {
        const hex = text.startsWith("#") ? text : "#" + text
        if (acceptableInput) {
            field.colour = hex
            field.picked(field.colour)
        }
        text = Qt.binding(() => field.colour.toString())
    }

    Rectangle {
        id: swatch
        anchors.left: parent.left
        anchors.leftMargin: Theme.controlPadding / 2
        anchors.verticalCenter: parent.verticalCenter
        width: parent.height - 10
        height: width
        radius: Theme.controlRadius / 2
        color: field.colour
        border.width: 1
        border.color: Theme.swatchFrame
    }
}

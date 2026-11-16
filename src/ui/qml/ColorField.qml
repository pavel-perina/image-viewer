import QtQuick
import QtQuick.Controls
import ImageViewer

// A colour typed as hex, such as #c0c0c0, with a swatch of it at the start; a click on the
// swatch opens the colour chooser. The owner keeps the colour: the field reports a new one by
// picked, and shows what colour is set to.
TextField {
    id: field

    property color colour: "white"
    // The user chose a colour, typed or in the chooser.
    signal picked(color colour)

    leftPadding: swatch.width + Theme.controlPadding
    text: field.colour.toString()
    validator: RegularExpressionValidator { regularExpression: /#?[0-9a-fA-F]{6}/ }
    onEditingFinished: {
        if (acceptableInput)
            field.picked(text.startsWith("#") ? text : "#" + text)
        text = Qt.binding(() => field.colour.toString())
    }

    Rectangle {
        id: swatch
        objectName: "colourFieldSwatch"
        anchors.left: parent.left
        anchors.leftMargin: Theme.controlPadding / 2
        anchors.verticalCenter: parent.verticalCenter
        width: parent.height - 10
        height: width
        radius: Theme.controlRadius / 2
        color: field.colour
        border.width: 1
        border.color: Theme.swatchFrame

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: field.openChooser()
        }
    }

    // Made the first time it is wanted, as most fields are never opened.
    function openChooser() {
        chooserLoader.active = true
        chooserLoader.item.colour = field.colour
        chooserLoader.item.open()
    }

    Loader {
        id: chooserLoader
        active: false
        sourceComponent: ColorChooserDialog {
            onChosen: (colour) => field.picked(colour)
        }
    }
}

import QtQuick
import QtQuick.Controls
import ImageViewer

// The colour chooser on its own, for a colour field with no room for it.
AppDialog {
    id: dialog
    objectName: "colorChooserDialog"

    // The colour to start from.
    property color colour
    // OK was pressed on this colour.
    signal chosen(color colour)

    title: "Colour"
    width: Math.min(460, parent ? parent.width - 32 : 460)
    standardButtons: Dialog.Ok | Dialog.Cancel

    onAboutToShow: {
        chooser.colour = dialog.colour
        chooser.original = dialog.colour
    }
    onAccepted: dialog.chosen(chooser.colour)

    ColorChooser {
        id: chooser
        anchors.fill: parent
    }
}
